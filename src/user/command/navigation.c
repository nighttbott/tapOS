#include <stddef.h>
#include "header/stdlib/string.h"
#include "user/commands.h"
#include "user/syscall.h"

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

int cmd_ls(int argc, char **argv, struct ShellIO *io) {
    char path[SHELL_PATH_MAX];
    const char *target = argc == 2 ? argv[1] : ".";
    char name[SHELL_NAME_MAX + 1];
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
    int result;
    while ((result = fsu_read_dir_entry(inode, &offset, &entry, name)) == 1) {
        shell_out(io, entry.name, entry.name_len);
        shell_out_str(io, entry.type == EXT2_FT_DIR ? "/\n" : "\n");
    }
    if (result < 0) {
        fsu_error(io, "ls", target, "cannot read directory");
        return 1;
    }
    return 0;
}

int cmd_mkdir(int argc, char **argv, struct ShellIO *io) {
    int status = 0;

    if (argc < 2) {
        shell_out_str(io, "usage: mkdir DIR...\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        char name[SHELL_NAME_MAX + 1];
        char path[SHELL_PATH_MAX];
        uint32_t parent;
        struct EXT2DriverRequest request = { .buf = NULL, .name = name, .is_directory = true };

        if (shell_resolve_dir(argv[i], &parent, path) == 0) {
            fsu_error(io, "mkdir", argv[i], "already exists");
            status = 1;
            continue;
        }
        if (shell_resolve_parent(argv[i], &parent, name) != 0) {
            fsu_error(io, "mkdir", argv[i], "no such directory");
            status = 1;
            continue;
        }
        request.name_len = (uint8_t)shell_strlen(name);
        request.parent_inode = parent;
        switch (sys_write_file(&request)) {
            case 0:
                break;
            case 1:
                fsu_error(io, "mkdir", argv[i], "already exists");
                status = 1;
                break;
            case 2:
                fsu_error(io, "mkdir", argv[i], "no such directory");
                status = 1;
                break;
            default:
                fsu_error(io, "mkdir", argv[i], "cannot create (disk full?)");
                status = 1;
                break;
        }
    }
    return status;
}
