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
