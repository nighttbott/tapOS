#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"

const uint8_t fs_signature[BLOCK_SIZE] = {
    'C', 'o', 'u', 'r', 's', 'e', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',  ' ',
    'D', 'e', 's', 'i', 'g', 'n', 'e', 'd', ' ', 'b', 'y', ' ', ' ', ' ', ' ',  ' ',
    'L', 'a', 'b', ' ', 'S', 'i', 's', 't', 'e', 'r', ' ', 'I', 'T', 'B', ' ',  ' ',
    'M', 'a', 'd', 'e', ' ', 'w', 'i', 't', 'h', ' ', '<', '3', ' ', ' ', ' ',  ' ',
    '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '2', '0', '2', '5', '\n',
    [BLOCK_SIZE-2] = 'O',
    [BLOCK_SIZE-1] = 'k',
};

/* Cached filesystem metadata used after initialization. */
static struct EXT2Superblock superblock;
static struct EXT2BlockGroupDescriptorTable bgd_table;

#define EXT2_ROOT_INODE 1u
#define EXT2_SUPERBLOCK_BLOCK 1u
#define EXT2_BGD_TABLE_BLOCK 2u
#define EXT2_FIRST_METADATA_BLOCK 3u
#define EXT2_METADATA_BLOCKS_PER_GROUP \
    (2u + INODES_TABLE_BLOCK_COUNT)
#define EXT2_TOTAL_BLOCKS (DISK_SPACE / BLOCK_SIZE)
/* Data starts after the signature, superblock, BGD, and group metadata. */
#define EXT2_FIRST_DATA_BLOCK \
    (EXT2_FIRST_METADATA_BLOCK + \
     GROUPS_COUNT * EXT2_METADATA_BLOCKS_PER_GROUP)

#define EXT2_DIRECT_BLOCK_COUNT 12u
#define EXT2_POINTERS_PER_BLOCK (BLOCK_SIZE / sizeof(uint32_t))
#define EXT2_SINGLE_INDIRECT_START EXT2_DIRECT_BLOCK_COUNT
#define EXT2_DOUBLE_INDIRECT_START \
    (EXT2_SINGLE_INDIRECT_START + EXT2_POINTERS_PER_BLOCK)
#define EXT2_MAX_DATA_BLOCKS \
    (EXT2_DOUBLE_INDIRECT_START + EXT2_POINTERS_PER_BLOCK * EXT2_POINTERS_PER_BLOCK)

struct EXT2ReadContext {
    struct EXT2Superblock superblock;
    struct EXT2BlockGroupDescriptorTable bgd_table;
};

static void ext2_set_bitmap_bit(struct BlockBuffer *bitmap,
                                uint32_t index) {
    bitmap->buf[index / 8u] |= (uint8_t)(1u << (index % 8u));
}

uint32_t inode_to_bgd(uint32_t inode) {
    /* Inode numbering starts from 1, while array indexes start from 0. */
    return (inode - 1u) / INODES_PER_GROUP;
}

uint32_t inode_to_local(uint32_t inode) {
    return (inode - 1u) % INODES_PER_GROUP;
}

void sync_node(struct EXT2Inode *node, uint32_t inode) {
    struct BlockBuffer inode_table_block;
    uint32_t bgd_index;
    uint32_t local_index;
    uint32_t table_block;
    uint32_t table_offset;

    if (node == NULL || inode == 0 ||
        inode > INODES_PER_GROUP * GROUPS_COUNT) {
        return;
    }

    bgd_index = inode_to_bgd(inode);
    local_index = inode_to_local(inode);
    table_block = bgd_table.table[bgd_index].bg_inode_table +
                  local_index / INODES_PER_TABLE;
    table_offset = (local_index % INODES_PER_TABLE) * INODE_SIZE;

    read_blocks(&inode_table_block, table_block, 1);
    memcpy(inode_table_block.buf + table_offset, node, sizeof(*node));
    write_blocks(&inode_table_block, table_block, 1);
}

void init_directory_table(struct EXT2Inode *node, uint32_t inode,
                          uint32_t parent_inode) {
    struct BlockBuffer directory_block;
    struct EXT2DirectoryEntry entry;
    uint32_t offset = 0;

    if (node == NULL || node->i_block[0] == 0 || inode == 0 ||
        parent_inode == 0) {
        return;
    }

    memset(&directory_block, 0, sizeof(directory_block));

    memset(&entry, 0, sizeof(entry));
    entry.inode = inode;
    entry.rec_len = get_entry_record_len(1);
    entry.name_len = 1;
    entry.file_type = EXT2_FT_DIR;
    memcpy(directory_block.buf + offset, &entry, sizeof(entry));
    directory_block.buf[offset + sizeof(entry)] = '.';
    offset += entry.rec_len;

    memset(&entry, 0, sizeof(entry));
    entry.inode = parent_inode;
    entry.rec_len = get_entry_record_len(2);
    entry.name_len = 2;
    entry.file_type = EXT2_FT_DIR;
    memcpy(directory_block.buf + offset, &entry, sizeof(entry));
    memcpy(directory_block.buf + offset + sizeof(entry), "..", 2);
    offset += entry.rec_len;

    memset(&entry, 0, sizeof(entry));
    /* The unused record fills the rest of the block for future children. */
    entry.rec_len = (uint16_t)(BLOCK_SIZE - offset);
    memcpy(directory_block.buf + offset, &entry, sizeof(entry));

    node->i_mode = EXT2_S_IFDIR;
    node->i_size = BLOCK_SIZE;
    node->i_blocks = 1;
    write_blocks(&directory_block, node->i_block[0], 1);
}

bool is_empty_storage(void) {
    struct BlockBuffer boot_sector;

    read_blocks(&boot_sector, BOOT_SECTOR, 1);
    return memcmp(boot_sector.buf, fs_signature, BLOCK_SIZE) != 0;
}

void create_ext2(void) {
    struct BlockBuffer block;
    struct EXT2Inode root_inode;
    uint32_t used_blocks = EXT2_FIRST_DATA_BLOCK + 1u;

    memset(&superblock, 0, sizeof(superblock));
    superblock.s_inodes_count = INODES_PER_GROUP * GROUPS_COUNT;
    superblock.s_blocks_count = EXT2_TOTAL_BLOCKS;
    superblock.s_free_blocks_count = EXT2_TOTAL_BLOCKS - used_blocks;
    superblock.s_free_inodes_count = superblock.s_inodes_count - 1u;
    superblock.s_first_data_block = 0;
    superblock.s_first_ino = EXT2_ROOT_INODE + 1u;
    superblock.s_blocks_per_group = BLOCKS_PER_GROUP;
    superblock.s_frags_per_group = BLOCKS_PER_GROUP;
    superblock.s_inodes_per_group = INODES_PER_GROUP;
    superblock.s_magic = EXT2_SUPER_MAGIC;

    memset(&bgd_table, 0, sizeof(bgd_table));
    for (uint32_t group = 0; group < GROUPS_COUNT; group++) {
        struct EXT2BlockGroupDescriptor *descriptor =
            &bgd_table.table[group];
        uint32_t metadata_start = EXT2_FIRST_METADATA_BLOCK +
                                  group * EXT2_METADATA_BLOCKS_PER_GROUP;
        uint32_t group_start = group * BLOCKS_PER_GROUP;
        uint32_t group_end = group_start + BLOCKS_PER_GROUP;
        uint32_t group_used_blocks = 0;

        /* Each group owns one block bitmap, one inode bitmap, and an inode table. */
        descriptor->bg_block_bitmap = metadata_start;
        descriptor->bg_inode_bitmap = metadata_start + 1u;
        descriptor->bg_inode_table = metadata_start + 2u;

        if (used_blocks > group_start) {
            uint32_t used_end = used_blocks < group_end ?
                                used_blocks : group_end;
            group_used_blocks = used_end - group_start;
        }
        descriptor->bg_free_blocks_count =
            (uint16_t)(BLOCKS_PER_GROUP - group_used_blocks);
        descriptor->bg_free_inodes_count = INODES_PER_GROUP;
    }
    bgd_table.table[0].bg_free_inodes_count--;
    bgd_table.table[0].bg_used_dirs_count = 1;

    /* Initialize allocation bitmaps and inode tables for every group. */
    for (uint32_t group = 0; group < GROUPS_COUNT; group++) {
        const struct EXT2BlockGroupDescriptor *descriptor =
            &bgd_table.table[group];
        uint32_t group_start = group * BLOCKS_PER_GROUP;

        memset(&block, 0, sizeof(block));
        /* A block bitmap bit represents an offset inside its block group. */
        for (uint32_t absolute_block = group_start;
             absolute_block < used_blocks &&
             absolute_block < group_start + BLOCKS_PER_GROUP;
             absolute_block++) {
            ext2_set_bitmap_bit(&block, absolute_block - group_start);
        }
        write_blocks(&block, descriptor->bg_block_bitmap, 1);

        memset(&block, 0, sizeof(block));
        if (group == 0) {
            ext2_set_bitmap_bit(&block, EXT2_ROOT_INODE - 1u);
        }
        write_blocks(&block, descriptor->bg_inode_bitmap, 1);

        memset(&block, 0, sizeof(block));
        for (uint32_t table_index = 0;
             table_index < INODES_TABLE_BLOCK_COUNT;
             table_index++) {
            write_blocks(&block, descriptor->bg_inode_table + table_index, 1);
        }
    }

    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.i_block[0] = EXT2_FIRST_DATA_BLOCK;
    init_directory_table(&root_inode, EXT2_ROOT_INODE, EXT2_ROOT_INODE);
    sync_node(&root_inode, EXT2_ROOT_INODE);

    memset(&block, 0, sizeof(block));
    memcpy(block.buf, &superblock, sizeof(superblock));
    write_blocks(&block, EXT2_SUPERBLOCK_BLOCK, 1);

    memset(&block, 0, sizeof(block));
    memcpy(block.buf, &bgd_table, sizeof(bgd_table));
    write_blocks(&block, EXT2_BGD_TABLE_BLOCK, 1);

    /* Write the signature last so an interrupted format is retried. */
    write_blocks(fs_signature, BOOT_SECTOR, 1);
}

void initialize_filesystem_ext2(void) {
    struct BlockBuffer block;

    if (is_empty_storage()) {
        create_ext2();
        return;
    }

    read_blocks(&block, EXT2_SUPERBLOCK_BLOCK, 1);
    memcpy(&superblock, block.buf, sizeof(superblock));
    read_blocks(&block, EXT2_BGD_TABLE_BLOCK, 1);
    memcpy(&bgd_table, block.buf, sizeof(bgd_table));
}

static bool ext2_read_context_init(struct EXT2ReadContext *context) {
    struct BlockBuffer block;

    if (context == NULL ||
        GROUPS_COUNT * sizeof(struct EXT2BlockGroupDescriptor) > BLOCK_SIZE) {
        return false;
    }

    read_blocks(&block, 1, 1);
    memcpy(&context->superblock, block.buf, sizeof(context->superblock));

    if (context->superblock.s_magic != EXT2_SUPER_MAGIC ||
        context->superblock.s_blocks_count <= 2 ||
        context->superblock.s_blocks_count > DISK_SPACE / BLOCK_SIZE ||
        context->superblock.s_inodes_count == 0 ||
        context->superblock.s_inodes_count > INODES_PER_GROUP * GROUPS_COUNT ||
        context->superblock.s_inodes_per_group != INODES_PER_GROUP) {
        return false;
    }

    read_blocks(&block, 2, 1);
    memcpy(&context->bgd_table, block.buf, sizeof(context->bgd_table));
    return true;
}

static bool ext2_valid_name(const char *name, uint8_t name_len) {
    if (name == NULL || name_len == 0) {
        return false;
    }

    for (uint32_t i = 0; i < name_len; i++) {
        if (name[i] == '\0' || name[i] == '/') {
            return false;
        }
    }
    return true;
}

static bool ext2_read_inode(const struct EXT2ReadContext *context,
                            uint32_t inode_number,
                            struct EXT2Inode *inode_out) {
    struct BlockBuffer block;
    uint32_t inode_index;
    uint32_t group_index;
    uint32_t local_inode;
    uint32_t inode_table_offset;
    uint32_t inode_table_block;
    uint32_t inode_offset;

    if (context == NULL || inode_out == NULL || inode_number == 0 ||
        inode_number > context->superblock.s_inodes_count) {
        return false;
    }

    inode_index = inode_number - 1;
    group_index = inode_index / INODES_PER_GROUP;
    local_inode = inode_index % INODES_PER_GROUP;
    if (group_index >= GROUPS_COUNT) {
        return false;
    }

    inode_table_block = context->bgd_table.table[group_index].bg_inode_table;
    inode_table_offset = local_inode / INODES_PER_TABLE;
    if (inode_table_block == 0 ||
        inode_table_block >= context->superblock.s_blocks_count ||
        inode_table_block >= DISK_SPACE / BLOCK_SIZE ||
        INODES_TABLE_BLOCK_COUNT >
            context->superblock.s_blocks_count - inode_table_block ||
        INODES_TABLE_BLOCK_COUNT >
            DISK_SPACE / BLOCK_SIZE - inode_table_block ||
        inode_table_offset >= context->superblock.s_blocks_count -
                              inode_table_block ||
        inode_table_offset >= DISK_SPACE / BLOCK_SIZE - inode_table_block) {
        return false;
    }
    inode_table_block += inode_table_offset;

    inode_offset = (local_inode % INODES_PER_TABLE) * INODE_SIZE;
    if (inode_offset + INODE_SIZE > BLOCK_SIZE) {
        return false;
    }

    read_blocks(&block, inode_table_block, 1);
    memcpy(inode_out, block.buf + inode_offset, sizeof(*inode_out));
    return true;
}

static bool ext2_valid_data_block(const struct EXT2ReadContext *context,
                                  uint32_t block_number) {
    return block_number != 0 &&
           block_number < context->superblock.s_blocks_count &&
           block_number < DISK_SPACE / BLOCK_SIZE;
}

static bool ext2_read_pointer(const struct EXT2ReadContext *context,
                              uint32_t pointer_block,
                              uint32_t pointer_index,
                              uint32_t *value_out) {
    struct BlockBuffer block;

    if (value_out == NULL || pointer_index >= EXT2_POINTERS_PER_BLOCK ||
        !ext2_valid_data_block(context, pointer_block)) {
        return false;
    }

    read_blocks(&block, pointer_block, 1);
    memcpy(value_out, block.buf + pointer_index * sizeof(uint32_t),
           sizeof(*value_out));
    return true;
}

static bool ext2_get_data_block(const struct EXT2ReadContext *context,
                                const struct EXT2Inode *inode,
                                uint32_t logical_block,
                                uint32_t *block_out) {
    uint32_t block_number;

    if (context == NULL || inode == NULL || block_out == NULL ||
        logical_block >= EXT2_MAX_DATA_BLOCKS) {
        return false;
    }

    if (logical_block < EXT2_DIRECT_BLOCK_COUNT) {
        block_number = inode->i_block[logical_block];
    } else if (logical_block < EXT2_DOUBLE_INDIRECT_START) {
        if (!ext2_read_pointer(context, inode->i_block[12],
                               logical_block - EXT2_SINGLE_INDIRECT_START,
                               &block_number)) {
            return false;
        }
    } else {
        uint32_t double_index = logical_block - EXT2_DOUBLE_INDIRECT_START;
        uint32_t first_level;

        if (!ext2_read_pointer(context, inode->i_block[13],
                               double_index / EXT2_POINTERS_PER_BLOCK,
                               &first_level) ||
            !ext2_read_pointer(context, first_level,
                               double_index % EXT2_POINTERS_PER_BLOCK,
                               &block_number)) {
            return false;
        }
    }

    if (!ext2_valid_data_block(context, block_number)) {
        return false;
    }
    *block_out = block_number;
    return true;
}

static int8_t ext2_find_entry(const struct EXT2ReadContext *context,
                              uint32_t parent_inode,
                              const char *name,
                              uint8_t name_len,
                              struct EXT2DirectoryEntry *entry_out) {
    struct EXT2Inode parent;
    struct BlockBuffer directory_block;
    uint32_t directory_blocks;

    if (context == NULL || entry_out == NULL ||
        !ext2_valid_name(name, name_len)) {
        return -1;
    }
    if (parent_inode == 0 ||
        parent_inode > context->superblock.s_inodes_count) {
        return 2;
    }
    if (!ext2_read_inode(context, parent_inode, &parent)) {
        return -1;
    }
    if ((parent.i_mode & 0xF000u) != EXT2_S_IFDIR) {
        return 2;
    }

    if (parent.i_size > DISK_SPACE) {
        return -1;
    }
    directory_blocks = parent.i_size / BLOCK_SIZE;
    if (parent.i_size % BLOCK_SIZE != 0) {
        directory_blocks++;
    }
    if (directory_blocks > EXT2_MAX_DATA_BLOCKS) {
        return -1;
    }

    for (uint32_t logical_block = 0;
         logical_block < directory_blocks;
         logical_block++) {
        uint32_t disk_block;
        if (!ext2_get_data_block(context, &parent, logical_block,
                                 &disk_block)) {
            return -1;
        }
        read_blocks(&directory_block, disk_block, 1);

        uint32_t directory_offset = logical_block * BLOCK_SIZE;
        uint32_t bytes_in_block = parent.i_size - directory_offset;
        uint32_t offset = 0;
        if (bytes_in_block > BLOCK_SIZE) {
            bytes_in_block = BLOCK_SIZE;
        }
        while (offset < bytes_in_block) {
            struct EXT2DirectoryEntry entry;
            uint16_t minimum_record_length;
            uint32_t name_offset = offset + sizeof(struct EXT2DirectoryEntry);

            if (bytes_in_block - offset < sizeof(struct EXT2DirectoryEntry)) {
                return -1;
            }

            memcpy(&entry, directory_block.buf + offset, sizeof(entry));
            minimum_record_length =
                get_entry_record_len((uint8_t)entry.name_len);
            if (entry.rec_len < sizeof(struct EXT2DirectoryEntry) ||
                entry.rec_len < minimum_record_length ||
                (entry.rec_len & 3u) != 0 ||
                entry.rec_len > bytes_in_block - offset ||
                entry.name_len > UINT8_MAX ||
                name_offset + entry.name_len > offset + entry.rec_len) {
                return -1;
            }

            if (entry.inode > context->superblock.s_inodes_count) {
                return -1;
            }
            if (entry.inode != 0 && entry.name_len == name_len &&
                memcmp(directory_block.buf + name_offset, name, name_len) == 0) {
                *entry_out = entry;
                return 0;
            }

            offset += entry.rec_len;
        }
    }

    return 1;
}

char *get_entry_name(void *entry) {
    if (entry == NULL) {
        return NULL;
    }
    return (char *)((uint8_t *)entry + sizeof(struct EXT2DirectoryEntry));
}

struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset) {
    if (ptr == NULL ||
        offset > BLOCK_SIZE - sizeof(struct EXT2DirectoryEntry)) {
        return NULL;
    }
    return (struct EXT2DirectoryEntry *)((uint8_t *)ptr + offset);
}

struct EXT2DirectoryEntry *get_next_directory_entry(
    struct EXT2DirectoryEntry *entry) {
    if (entry == NULL || entry->rec_len == 0) {
        return NULL;
    }
    return (struct EXT2DirectoryEntry *)((uint8_t *)entry + entry->rec_len);
}

uint16_t get_entry_record_len(uint8_t name_len) {
    uint16_t unaligned_length =
        (uint16_t)(sizeof(struct EXT2DirectoryEntry) + name_len);
    return (uint16_t)((unaligned_length + 3u) & ~3u);
}

uint32_t get_dir_first_child_offset(void *ptr) {
    struct EXT2DirectoryEntry dot;
    struct EXT2DirectoryEntry dotdot;
    uint32_t offset;

    if (ptr == NULL || BLOCK_SIZE < 2 * sizeof(struct EXT2DirectoryEntry)) {
        return BLOCK_SIZE;
    }

    memcpy(&dot, ptr, sizeof(dot));
    if (dot.rec_len < sizeof(dot) || dot.rec_len > BLOCK_SIZE ||
        (dot.rec_len & 3u) != 0) {
        return BLOCK_SIZE;
    }
    offset = dot.rec_len;

    if (BLOCK_SIZE - offset < sizeof(dotdot)) {
        return BLOCK_SIZE;
    }
    memcpy(&dotdot, (uint8_t *)ptr + offset, sizeof(dotdot));
    if (dotdot.rec_len < sizeof(dotdot) ||
        dotdot.rec_len > BLOCK_SIZE - offset ||
        (dotdot.rec_len & 3u) != 0) {
        return BLOCK_SIZE;
    }
    return offset + dotdot.rec_len;
}

int8_t read_directory(struct EXT2DriverRequest *request) {
    struct EXT2ReadContext context;
    struct EXT2DirectoryEntry entry;
    struct EXT2Inode parent_inode;
    struct EXT2Inode directory_inode;
    uint32_t directory_inode_number;
    uint32_t directory_blocks;
    int8_t result;

    if (request == NULL ||
        !ext2_valid_name(request->name, request->name_len)) {
        return -1;
    }
    if (!ext2_read_context_init(&context)) {
        return -1;
    }

    if (request->parent_inode == 0 ||
        request->parent_inode > context.superblock.s_inodes_count) {
        return 3;
    }
    if (!ext2_read_inode(&context, request->parent_inode, &parent_inode)) {
        return -1;
    }
    if ((parent_inode.i_mode & 0xF000u) != EXT2_S_IFDIR) {
        return 1;
    }

    if (request->name_len == 1 && request->name[0] == '.') {
        directory_inode_number = request->parent_inode;
    } else if (request->name_len == 2 &&
               request->name[0] == '.' && request->name[1] == '.') {
        struct BlockBuffer first_block;
        struct EXT2DirectoryEntry dot;
        struct EXT2DirectoryEntry dotdot;
        uint32_t first_block_number;

        if (!ext2_get_data_block(&context, &parent_inode, 0,
                                 &first_block_number)) {
            return -1;
        }
        read_blocks(&first_block, first_block_number, 1);
        memcpy(&dot, first_block.buf, sizeof(dot));
        if (dot.rec_len < sizeof(dot) ||
            dot.rec_len > BLOCK_SIZE - sizeof(dot) ||
            dot.rec_len < get_entry_record_len(1) ||
            (dot.rec_len & 3u) != 0) {
            return -1;
        }
        memcpy(&dotdot, first_block.buf + dot.rec_len, sizeof(dotdot));
        if (dot.inode != request->parent_inode ||
            dot.name_len != 1 ||
            first_block.buf[sizeof(dot)] != '.' ||
            dotdot.name_len != 2 ||
            dotdot.rec_len < get_entry_record_len(2) ||
            (dotdot.rec_len & 3u) != 0 ||
            memcmp(first_block.buf + dot.rec_len +
                       sizeof(struct EXT2DirectoryEntry),
                   "..", 2) != 0 ||
            dotdot.rec_len > BLOCK_SIZE - dot.rec_len ||
            dotdot.inode == 0 ||
            dotdot.inode > context.superblock.s_inodes_count) {
            return -1;
        }
        directory_inode_number = dotdot.inode;
    } else {
        result = ext2_find_entry(&context, request->parent_inode,
                                 request->name, request->name_len, &entry);
        if (result == 1) {
            return 2;
        }
        if (result == 2) {
            return 3;
        }
        if (result != 0) {
            return -1;
        }
        if (entry.file_type != EXT2_FT_DIR) {
            return 1;
        }
        directory_inode_number = entry.inode;
    }

    if (!ext2_read_inode(&context, directory_inode_number, &directory_inode)) {
        return -1;
    }
    if ((directory_inode.i_mode & 0xF000u) != EXT2_S_IFDIR) {
        return 1;
    }

    if (directory_inode.i_size == 0 ||
        directory_inode.i_size > DISK_SPACE) {
        return -1;
    }
    directory_blocks = directory_inode.i_size / BLOCK_SIZE;
    if (directory_inode.i_size % BLOCK_SIZE != 0) {
        directory_blocks++;
    }
    if (directory_blocks > EXT2_MAX_DATA_BLOCKS ||
        directory_blocks > request->buffer_size / BLOCK_SIZE ||
        request->buf == NULL) {
        return -1;
    }

    for (uint32_t logical_block = 0;
         logical_block < directory_blocks;
         logical_block++) {
        struct BlockBuffer block;
        uint32_t disk_block;
        if (!ext2_get_data_block(&context, &directory_inode, logical_block,
                                 &disk_block)) {
            return -1;
        }
        read_blocks(&block, disk_block, 1);
        memcpy((uint8_t *)request->buf + logical_block * BLOCK_SIZE,
               block.buf, BLOCK_SIZE);
    }

    return 0;
}

int8_t read(struct EXT2DriverRequest request) {
    struct EXT2ReadContext context;
    struct EXT2DirectoryEntry entry;
    struct EXT2Inode inode;
    int8_t result;
    uint32_t bytes_read = 0;

    if (!ext2_valid_name(request.name, request.name_len)) {
        return -1;
    }
    if (!ext2_read_context_init(&context)) {
        return -1;
    }

    result = ext2_find_entry(&context, request.parent_inode, request.name,
                             request.name_len, &entry);
    if (result == 1) {
        return 3;
    }
    if (result == 2) {
        return 4;
    }
    if (result != 0) {
        return -1;
    }
    if (entry.file_type != EXT2_FT_REG_FILE) {
        return 1;
    }
    if (!ext2_read_inode(&context, entry.inode, &inode)) {
        return -1;
    }
    if ((inode.i_mode & 0xF000u) != EXT2_S_IFREG) {
        return 1;
    }
    if (request.buffer_size < inode.i_size) {
        return 2;
    }
    if (inode.i_size != 0 && request.buf == NULL) {
        return -1;
    }
    if (inode.i_size > DISK_SPACE ||
        inode.i_size / BLOCK_SIZE +
            (inode.i_size % BLOCK_SIZE != 0) > EXT2_MAX_DATA_BLOCKS) {
        return -1;
    }

    while (bytes_read < inode.i_size) {
        struct BlockBuffer data_block;
        uint32_t logical_block = bytes_read / BLOCK_SIZE;
        uint32_t block_number;
        uint32_t bytes_to_copy = inode.i_size - bytes_read;

        if (bytes_to_copy > BLOCK_SIZE) {
            bytes_to_copy = BLOCK_SIZE;
        }
        if (!ext2_get_data_block(&context, &inode, logical_block,
                                 &block_number)) {
            return -1;
        }
        read_blocks(&data_block, block_number, 1);
        memcpy((uint8_t *)request.buf + bytes_read, data_block.buf,
               bytes_to_copy);
        bytes_read += bytes_to_copy;
    }

    return 0;
}
