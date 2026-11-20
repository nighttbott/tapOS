#include <stddef.h>
#include "header/stdlib/string.h"
#include "user/commands.h"
#include "user/syscall.h"

static uint8_t listing[FSU_DIR_BUFFER];  // fsu_list
static uint8_t probe[FSU_DIR_BUFFER];    // lookups, so they never clobber a listing in use
static char file_buffer[FSU_FILE_MAX];   // fsu_read_file

bool fsu_is_dot(const char *name, uint32_t len) {
    return (len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.');
}

void fsu_error(struct ShellIO *io, const char *cmd, const char *path, const char *message) {
    shell_out_str(io, cmd);
    shell_out_str(io, ": ");
    shell_out_str(io, path);
    shell_out_str(io, ": ");
    shell_out_str(io, message);
    shell_out_str(io, "\n");
}

/* read_directory with name "." reads the folder parent_inode itself. */
static bool read_dir(uint32_t dir, const char *name, uint8_t name_len, uint8_t *table) {
    struct EXT2DriverRequest request = {
        .buf = table,
        .name = (char *)name,
        .name_len = name_len,
        .parent_inode = dir,
        .buffer_size = FSU_DIR_BUFFER,
        .is_directory = true,
    };

    // Unused space must read as rec_len 0, which is where fsu_next_entry stops.
    memset(table, 0, FSU_DIR_BUFFER);
    return sys_read_dir(&request) == 0;
}

const uint8_t *fsu_list(uint32_t dir) {
    return read_dir(dir, ".", 1, listing) ? listing : NULL;
}

bool fsu_next_entry(const uint8_t *table, uint32_t *offset, struct FsuEntry *entry) {
    while (*offset + sizeof(struct EXT2DirectoryEntry) <= FSU_DIR_BUFFER) {
        struct EXT2DirectoryEntry raw;
        const char *name = (const char *)table + *offset + sizeof(raw);

        memcpy(&raw, table + *offset, sizeof(raw));
        if (raw.rec_len == 0 ||
            *offset + sizeof(raw) + raw.name_len > FSU_DIR_BUFFER) {
            return false;
        }
        *offset += raw.rec_len;
        if (raw.inode == 0 || raw.name_len == 0 || raw.name_len > SHELL_NAME_MAX ||
            fsu_is_dot(name, raw.name_len)) {
            continue;
        }
        entry->inode = raw.inode;
        entry->type = raw.file_type;
        entry->name = name;
        entry->name_len = raw.name_len;
        return true;
    }
    return false;
}

int fsu_lookup(uint32_t dir, const char *name, uint32_t *inode, uint8_t *type) {
    uint32_t len = shell_strlen(name);
    uint32_t offset = 0;
    struct FsuEntry entry;

    if (!read_dir(dir, ".", 1, probe)) {
        return -1;
    }
    while (fsu_next_entry(probe, &offset, &entry)) {
        if (entry.name_len == len && memcmp(entry.name, name, len) == 0) {
            if (inode != NULL) {
                *inode = entry.inode;
            }
            if (type != NULL) {
                *type = entry.type;
            }
            return 0;
        }
    }
    return -1;
}

int fsu_stat(const char *path, struct FsuTarget *target) {
    if (shell_resolve_parent(path, &target->parent, target->name) != 0 ||
        fsu_is_dot(target->name, shell_strlen(target->name))) {
        return -1;
    }
    return fsu_lookup(target->parent, target->name, &target->inode, &target->type);
}

uint32_t fsu_parent(uint32_t dir) {
    struct EXT2DirectoryEntry dot;

    // The first entry of any folder is "." with its own inode, so reading ".." of dir yields the parent.
    if (!read_dir(dir, "..", 2, probe)) {
        return 0;
    }
    memcpy(&dot, probe, sizeof(dot));
    return dot.inode;
}

bool fsu_is_inside(uint32_t dir, uint32_t ancestor) {
    // A path cannot hold more folders than this, so a broken ".." chain cannot loop forever.
    for (uint32_t step = 0; step < SHELL_PATH_MAX; step++) {
        uint32_t up;

        if (dir == ancestor) {
            return true;
        }
        if (dir == SHELL_ROOT_INODE) {
            return false;
        }
        up = fsu_parent(dir);
        if (up == 0 || up == dir) {
            return false;
        }
        dir = up;
    }
    return false;
}

int32_t fsu_read_file(uint32_t parent, const char *name, const char **data, uint32_t *size) {
    struct EXT2DriverRequest request = {
        .buf = file_buffer,
        .name = (char *)name,
        .name_len = (uint8_t)shell_strlen(name),
        .parent_inode = parent,
        .buffer_size = FSU_FILE_MAX,
    };
    int32_t code = sys_read_file(&request);
    uint32_t low = 0;
    uint32_t high = FSU_FILE_MAX;

    if (code != 0) {
        return code;
    }

    /*
     * read() does not report the file size, but it answers 2 when buffer_size is smaller than
     * the file. The smallest buffer_size it accepts is therefore the exact size, so trailing
     * NUL bytes survive cp and mv.
     */
    while (low < high) {
        uint32_t middle = low + (high - low) / 2;

        request.buffer_size = middle;
        code = sys_read_file(&request);
        if (code == 0) {
            high = middle;
        } else if (code == 2) {
            low = middle + 1;
        } else {
            return code;
        }
    }
    *data = file_buffer;
    *size = low;
    return 0;
}

const char *fsu_read_path(const char *path, const char **data, uint32_t *size) {
    char name[SHELL_NAME_MAX + 1];
    uint32_t parent;
    uint32_t dir;
    char absolute[SHELL_PATH_MAX];

    if (shell_resolve_dir(path, &dir, absolute) == 0) {
        return "is a directory";
    }
    if (shell_resolve_parent(path, &parent, name) != 0) {
        return "not found";
    }
    switch (fsu_read_file(parent, name, data, size)) {
        case 0:
            return NULL;
        case 1:
            return "is a directory";
        case 2:
            return "file too large";
        case 3:
        case 4:
            return "not found";
        default:
            return "read failed";
    }
}

static int32_t remove_entry(uint32_t parent, const char *name, uint32_t len, bool is_dir) {
    struct EXT2DriverRequest request = {
        .buf = NULL,
        .name = (char *)name,
        .name_len = (uint8_t)len,
        .parent_inode = parent,
        .is_directory = is_dir,
    };

    return sys_delete(&request);
}

/*
 * Deletes everything inside folder dir. Each round lists dir again and removes its first child,
 * so no names have to be kept across the recursion. A child folder that is not empty is emptied
 * first and removed in the next round.
 */
static bool clear_dir(uint32_t dir, uint32_t depth) {
    uint32_t emptied = 0;

    for (;;) {
        const uint8_t *table = fsu_list(dir);
        struct FsuEntry entry;
        uint32_t offset = 0;
        int32_t code;

        if (table == NULL) {
            return false;
        }
        if (!fsu_next_entry(table, &offset, &entry)) {
            return true;
        }
        if (entry.type != EXT2_FT_DIR && entry.type != EXT2_FT_REG_FILE) {
            return false;
        }
        code = remove_entry(dir, entry.name, entry.name_len, entry.type == EXT2_FT_DIR);
        if (code == 0) {
            continue;
        }
        // Only "folder not empty" is worth a retry, and only once per folder.
        if (code != 2 || entry.inode == emptied || depth >= FSU_DEPTH_MAX ||
            !clear_dir(entry.inode, depth + 1)) {
            return false;
        }
        emptied = entry.inode;
    }
}

int32_t fsu_remove(uint32_t parent, const char *name, bool is_dir) {
    return remove_entry(parent, name, shell_strlen(name), is_dir);
}

int32_t fsu_remove_tree(uint32_t parent, const char *name) {
    uint32_t len = shell_strlen(name);
    int32_t code = remove_entry(parent, name, len, true);
    uint32_t inode;
    uint8_t type;

    if (code != 2) {
        return code;
    }
    if (fsu_lookup(parent, name, &inode, &type) != 0 || type != EXT2_FT_DIR ||
        !clear_dir(inode, 1)) {
        return -1;
    }
    return remove_entry(parent, name, len, true);
}

int32_t fsu_write_file(uint32_t parent, const char *name, const void *data, uint32_t size,
                       bool is_dir) {
    struct EXT2DriverRequest request = {
        .buf = (void *)data,
        .name = (char *)name,
        .name_len = (uint8_t)shell_strlen(name),
        .parent_inode = parent,
        .buffer_size = is_dir ? 0 : size,
        .is_directory = is_dir,
    };

    return sys_write_file(&request);
}

static char copy_name[SHELL_NAME_MAX + 1];

/*
 * Copies the children of folder src into folder dst. The listing of src is read again only after
 * a recursion, which reuses the listing buffer; offset stays valid because src does not change.
 */
static bool copy_children(uint32_t src, uint32_t dst, uint32_t depth) {
    const uint8_t *table = NULL;
    uint32_t offset = 0;

    for (;;) {
        struct FsuEntry entry;

        if (table == NULL && (table = fsu_list(src)) == NULL) {
            return false;
        }
        if (!fsu_next_entry(table, &offset, &entry)) {
            return true;
        }
        memcpy(copy_name, entry.name, entry.name_len);
        copy_name[entry.name_len] = '\0';

        if (entry.type == EXT2_FT_DIR) {
            uint32_t made;

            if (depth >= FSU_DEPTH_MAX ||
                fsu_write_file(dst, copy_name, NULL, 0, true) != 0 ||
                fsu_lookup(dst, copy_name, &made, NULL) != 0 ||
                !copy_children(entry.inode, made, depth + 1)) {
                return false;
            }
            table = NULL;
        } else if (entry.type == EXT2_FT_REG_FILE) {
            const char *data;
            uint32_t size;

            if (fsu_read_file(src, copy_name, &data, &size) != 0 ||
                fsu_write_file(dst, copy_name, data, size, false) != 0) {
                return false;
            }
        } else {
            return false;
        }
    }
}

int32_t fsu_copy_tree(uint32_t src, uint32_t dst_parent, const char *name) {
    int32_t code = fsu_write_file(dst_parent, name, NULL, 0, true);
    uint32_t made;

    if (code != 0) {
        return code;
    }
    if (fsu_lookup(dst_parent, name, &made, NULL) != 0 || !copy_children(src, made, 1)) {
        // Leave nothing half copied behind.
        fsu_remove_tree(dst_parent, name);
        return -1;
    }
    return 0;
}

const char *fsu_destination(const char *dest, const char *src_name, bool many,
                            struct FsuDest *out) {
    char absolute[SHELL_PATH_MAX];
    uint32_t dest_len = shell_strlen(dest);
    uint32_t name_len = shell_strlen(src_name);
    struct FsuTarget existing;

    if (shell_resolve_dir(dest, &out->parent, absolute) == 0) {
        // An existing folder: the source keeps its name inside it.
        memcpy(out->name, src_name, name_len + 1);
        memcpy(out->shown, dest, dest_len);
        if (dest_len > 0 && dest[dest_len - 1] != '/') {
            out->shown[dest_len++] = '/';
        }
        memcpy(out->shown + dest_len, src_name, name_len + 1);
        return NULL;
    }
    memcpy(out->shown, dest, dest_len + 1);
    if (many) {
        return fsu_stat(dest, &existing) == 0 ? "not a directory" : "no such directory";
    }
    if (shell_resolve_parent(dest, &out->parent, out->name) != 0 ||
        fsu_is_dot(out->name, shell_strlen(out->name))) {
        return "no such directory";
    }
    return NULL;
}
