#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "header/filesystem/ext2.h"
#include "header/filesystem/index.h"
#include "header/stdlib/string.h"

#define INDEX_ROOT_INODE   1u
#define INDEX_INODE_COUNT  (GROUPS_COUNT * INODES_PER_GROUP)
#define INDEX_BUCKETS      128u
#define INDEX_NO_NODE      0u
#define INDEX_NAME_MAX     255u
#define INDEX_PATH_DEPTH   64u
#ifndef INDEX_POOL_SIZE
#define INDEX_POOL_SIZE    (48u * 1024u)
#endif

_Static_assert(INDEX_INODE_COUNT < 0x10000u, "inode numbers must fit the 16-bit node links");

struct IndexNode {
    uint16_t parent;                    /* 0 while the slot is unused */
    uint16_t next;                      /* next inode in the same hash bucket */
    uint16_t name_offset;               /* where the name starts in name_pool */
    uint8_t  name_len;
    uint8_t  type;
};

/* Names live in one append-only pool so the index stays small (about 55 KiB) even with
 * 255-character names allowed. Deleted names are only reclaimed by the next rebuild; a full
 * pool simply makes the index rebuild, and an index that cannot hold the disk reports an
 * error so `find` falls back to walking the folders. */
static struct IndexNode nodes[INDEX_INODE_COUNT + 1u];
static char name_pool[INDEX_POOL_SIZE];
static uint32_t pool_used;
static uint16_t buckets[INDEX_BUCKETS];
static bool built;
static int32_t entry_count;

static uint32_t hash_name(const char *name, uint8_t len) {
    uint32_t hash = 2166136261u;
    for (uint8_t i = 0; i < len; i++)
        hash = (hash ^ (uint8_t)name[i]) * 16777619u;
    return hash % INDEX_BUCKETS;
}

static bool is_dot(const char *name, uint8_t len) {
    return (len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.');
}

static bool insert(uint32_t parent, uint32_t inode, const char *name, uint8_t len, uint8_t type) {
    struct IndexNode *node = &nodes[inode];
    uint32_t bucket = hash_name(name, len);

    if (len > INDEX_POOL_SIZE - pool_used)
        return false;
    node->parent = (uint16_t)parent;
    node->name_len = len;
    node->type = type;
    node->name_offset = (uint16_t)pool_used;
    memcpy(name_pool + pool_used, name, len);
    pool_used += len;
    node->next = buckets[bucket];
    buckets[bucket] = (uint16_t)inode;
    entry_count++;
    return true;
}

static void unlink_node(uint32_t inode) {
    struct IndexNode *node = &nodes[inode];
    uint16_t *link = &buckets[hash_name(name_pool + node->name_offset, node->name_len)];

    while (*link != INDEX_NO_NODE && *link != inode)
        link = &nodes[*link].next;
    if (*link == inode)
        *link = node->next;
    node->parent = 0;
    entry_count--;
}

void fs_index_invalidate(void) {
    built = false;
}

void fs_index_add(uint32_t parent_inode, uint32_t inode, const char *name, uint8_t name_len,
                  uint8_t file_type) {
    if (!built || inode == 0 || inode > INDEX_INODE_COUNT || parent_inode == 0 ||
        parent_inode > INDEX_INODE_COUNT || name_len == 0 || is_dot(name, name_len)) {
        return;
    }
    if (nodes[inode].parent != 0)
        unlink_node(inode);
    if (!insert(parent_inode, inode, name, name_len, file_type))
        built = false;   /* pool full: rebuilding drops the names of deleted files */
}

void fs_index_remove(uint32_t inode) {
    if (built && inode != 0 && inode <= INDEX_INODE_COUNT && nodes[inode].parent != 0)
        unlink_node(inode);
}

/* One walk from the root over the real directory blocks. Folders already seen are skipped, so
 * a damaged image with a folder loop cannot run forever. */
static bool build(void) {
    static uint32_t pending[INDEX_INODE_COUNT];
    static bool visited[INDEX_INODE_COUNT + 1u];
    uint32_t depth = 0;
    struct BlockBuffer block;

    memset(nodes, 0, sizeof(nodes));
    memset(buckets, 0, sizeof(buckets));
    memset(visited, 0, sizeof(visited));
    entry_count = 0;
    pool_used = 0;

    visited[INDEX_ROOT_INODE] = true;
    pending[depth++] = INDEX_ROOT_INODE;
    while (depth != 0) {
        uint32_t folder = pending[--depth];

        for (uint32_t index = 0;; index++) {
            int8_t status = ext2_read_directory_block(folder, index, block.buf);
            uint32_t offset = 0;

            if (status < 0)
                return false;
            if (status == 0)
                break;
            while (offset < BLOCK_SIZE) {
                struct EXT2DirectoryEntry raw;
                const char *name;

                if (BLOCK_SIZE - offset < sizeof(raw))
                    return false;
                memcpy(&raw, block.buf + offset, sizeof(raw));
                if (raw.name_len > INDEX_NAME_MAX || raw.rec_len < sizeof(raw) + raw.name_len ||
                    raw.rec_len > BLOCK_SIZE - offset || (raw.rec_len & 3u) != 0)
                    return false;
                name = (const char *)block.buf + offset + sizeof(raw);
                offset += raw.rec_len;
                if (raw.inode == 0 || raw.name_len == 0 || is_dot(name, (uint8_t)raw.name_len))
                    continue;
                if (raw.inode > INDEX_INODE_COUNT)
                    return false;
                if (nodes[raw.inode].parent != 0)   /* the same inode twice: no hard links here */
                    continue;
                if (!insert(folder, raw.inode, name, (uint8_t)raw.name_len, raw.file_type))
                    return false;
                if (raw.file_type == EXT2_FT_DIR && !visited[raw.inode]) {
                    visited[raw.inode] = true;
                    if (depth == INDEX_INODE_COUNT)
                        return false;
                    pending[depth++] = raw.inode;
                }
            }
        }
    }
    return true;
}

static bool ensure_built(void) {
    if (!built)
        built = build();
    return built;
}

/* Builds the path of `inode` back to front into the end of `path`; returns its length, or 0. */
static uint32_t make_path(uint32_t inode, char *path, uint32_t path_size) {
    uint32_t end = path_size;

    for (uint32_t step = 0; inode != INDEX_ROOT_INODE; step++) {
        const struct IndexNode *node;

        if (step == INDEX_PATH_DEPTH || inode > INDEX_INODE_COUNT || nodes[inode].parent == 0)
            return 0;
        node = &nodes[inode];
        if (end < (uint32_t)node->name_len + 1u)
            return 0;
        end -= node->name_len;
        memcpy(path + end, name_pool + node->name_offset, node->name_len);
        path[--end] = '/';
        inode = node->parent;
    }
    if (end == path_size)
        return 0;
    memmove(path, path + end, path_size - end);
    return path_size - end;
}

int32_t fs_index_find(const char *name, uint8_t name_len, char *out, uint32_t out_size,
                      uint32_t *written_out) {
    char path[1024];   /* longer paths do not fit a shell command line anyway */
    uint32_t written = 0;
    int32_t matches = 0;
    bool truncated = false;

    *written_out = 0;
    if (!ensure_built())
        return FS_INDEX_NO_MATCH_ERROR;

    for (uint32_t inode = buckets[hash_name(name, name_len)]; inode != INDEX_NO_NODE;
         inode = nodes[inode].next) {
        const struct IndexNode *node = &nodes[inode];
        uint32_t length;
        bool folder = node->type == EXT2_FT_DIR;

        if (node->name_len != name_len || memcmp(name_pool + node->name_offset, name, name_len) != 0)
            continue;
        length = make_path(inode, path, sizeof(path));
        if (length == 0)
            continue;
        if (written + length + (folder ? 2u : 1u) > out_size) {
            truncated = true;
            break;
        }
        memcpy(out + written, path, length);
        written += length;
        if (folder)
            out[written++] = '/';
        out[written++] = '\n';
        matches++;
    }
    *written_out = written;
    return truncated ? FS_INDEX_TRUNCATED : matches;
}

int32_t fs_index_count(void) {
    return ensure_built() ? entry_count : FS_INDEX_NO_MATCH_ERROR;
}
