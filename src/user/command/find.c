#include "header/stdlib/string.h"
#include "user/commands.h"
#include "user/syscall.h"

#define FIND_MAX_INODES (GROUPS_COUNT * INODES_PER_GROUP)

struct FindDirectory {
    uint32_t inode;
    uint32_t cursor;
    uint32_t path_len;
};

int cmd_find(int argc, char **argv, struct ShellIO *io) {
    static struct FindDirectory stack[FIND_MAX_INODES];
    static bool visited[FIND_MAX_INODES + 1];
    static char path[SHELL_IO_BUFFER];
    char name[SHELL_NAME_MAX + 1];
    uint32_t depth = 1;
    bool invalid_directory = false;

    if (argc != 2) {
        shell_out_str(io, "usage: find NAME\n");
        return 1;
    }
    memset(visited, 0, sizeof(visited));
    visited[SHELL_ROOT_INODE] = true;
    stack[0] = (struct FindDirectory){.inode = SHELL_ROOT_INODE};
    while (depth != 0) {
        struct FindDirectory *folder = &stack[depth - 1];
        struct FsuEntry entry;
        int result = fsu_read_dir_entry(folder->inode, &folder->cursor, &entry, name);
        if (result != 1) {
            invalid_directory |= result < 0;
            depth--;
            continue;
        }

        uint32_t length = folder->path_len + 1 + entry.name_len;
        if (length <= sizeof(path)) {
            path[folder->path_len] = '/';
            memcpy(path + folder->path_len + 1, name, entry.name_len);
        }
        if (shell_strcmp(name, argv[1]) == 0) {
            if (length > sizeof(path)) {
                io->overflow = true;
            } else {
                shell_out(io, path, length);
                shell_out_str(io, entry.type == EXT2_FT_DIR ? "/\n" : "\n");
            }
        }
        if (entry.type == EXT2_FT_DIR) {
            if (entry.inode > FIND_MAX_INODES || visited[entry.inode] ||
                depth == FIND_MAX_INODES) {
                invalid_directory = true;
            } else {
                visited[entry.inode] = true;
                stack[depth++] = (struct FindDirectory){entry.inode, 0, length};
            }
        }
    }
    if (invalid_directory) {
        shell_out_str(io, "find: some folders could not be searched\n");
        return 1;
    }
    return 0;
}
