#include <stddef.h>
#include "header/stdlib/string.h"
#include "user/commands.h"
#include "user/syscall.h"

/* Folder commands: cd, ls, mkdir. */

/* Explains why path is not a folder: it is a file, or it does not exist. */
static void not_a_folder(struct ShellIO *io, const char *cmd, const char *path) {
    struct FsuTarget target;

    if (fsu_stat(path, &target) == 0 && target.type != EXT2_FT_DIR) {
        fsu_error(io, cmd, path, "not a directory");
    } else {
        fsu_error(io, cmd, path, "no such directory");
    }
}

int cmd_cd(int argc, char **argv, struct ShellIO *io) {
    char path[SHELL_PATH_MAX];
    uint32_t inode;
    const char *target = argc == 2 ? argv[1] : "/";

    if (argc > 2) {
        shell_out_str(io, "usage: cd [DIR]\n");
        return 1;
    }
    if (shell_resolve_dir(target, &inode, path) != 0) {
        not_a_folder(io, "cd", target);
        return 1;
    }
    shell_cwd_set(inode, path);
    return 0;
}

/* Lists one entry per line in folder order; folders end with '/'. A file path prints itself. */
int cmd_ls(int argc, char **argv, struct ShellIO *io) {
    char path[SHELL_PATH_MAX];
    const char *target = argc == 2 ? argv[1] : ".";
    const uint8_t *table;
    struct FsuTarget file;
    struct FsuEntry entry;
    uint32_t inode;
    uint32_t offset = 0;

    if (argc > 2) {
        shell_out_str(io, "usage: ls [DIR]\n");
        return 1;
    }
    if (shell_resolve_dir(target, &inode, path) != 0) {
        if (fsu_stat(target, &file) == 0) {
            shell_out_str(io, target);
            shell_out_str(io, "\n");
            return 0;
        }
        fsu_error(io, "ls", target, "not found");
        return 1;
    }
    table = fsu_list(inode);
    if (table == NULL) {
        fsu_error(io, "ls", target, "cannot read directory");
        return 1;
    }
    while (fsu_next_entry(table, &offset, &entry)) {
        shell_out(io, entry.name, entry.name_len);
        shell_out_str(io, entry.type == EXT2_FT_DIR ? "/\n" : "\n");
    }
    return 0;
}
