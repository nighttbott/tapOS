#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/user/commands.h"
#include "header/user/syscall.h"

int cmd_cat(int argc, char **argv, struct ShellIO *io) {
    int status = 0;

    if (argc == 1) {
        if (io->in == NULL) {
            shell_out_str(io, "usage: cat FILE...\n");
            return 1;
        }
        shell_out(io, io->in, io->in_len);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        const char *data;
        uint32_t size;
        const char *error = fsu_read_path(argv[i], &data, &size);

        if (error != NULL) {
            fsu_error(io, "cat", argv[i], error);
            status = 1;
            continue;
        }
        shell_out(io, data, size);
    }
    return status;
}

static bool ends_in_dot(const char *path) {
    char name[SHELL_NAME_MAX + 1];
    uint32_t parent;

    return shell_resolve_parent(path, &parent, name) == 0 && fsu_is_dot(name, shell_strlen(name));
}

static int recursive_flag(int argc, char **argv, bool *recursive) {
    *recursive = argc > 1 &&
                 (shell_strcmp(argv[1], "-r") == 0 || shell_strcmp(argv[1], "-R") == 0);
    return *recursive ? 2 : 1;
}

static const char *remove_one(const char *path, bool recursive) {
    struct FsuTarget target;
    int32_t code;

    if (ends_in_dot(path)) {
        return "cannot remove '.' or '..'";
    }
    if (fsu_stat(path, &target) != 0) {
        uint32_t dir;
        char absolute[SHELL_PATH_MAX];

        return shell_resolve_dir(path, &dir, absolute) == 0 ? "cannot remove the root directory"
                                                            : "not found";
    }
    if (target.type == EXT2_FT_DIR) {
        if (!recursive) {
            return "is a directory (use rm -r)";
        }
        if (fsu_is_inside(shell_cwd_inode(), target.inode)) {
            return "is the current directory or above it";
        }
        code = fsu_remove_tree(target.parent, target.name);
    } else {
        code = fsu_remove(target.parent, target.name, false);
    }
    return code == 0 ? NULL : "cannot remove";
}

int cmd_rm(int argc, char **argv, struct ShellIO *io) {
    bool recursive;
    int first = recursive_flag(argc, argv, &recursive);
    int status = 0;

    if (first >= argc) {
        shell_out_str(io, "usage: rm [-r] PATH...\n");
        return 1;
    }
    for (int i = first; i < argc; i++) {
        const char *error = remove_one(argv[i], recursive);

        if (error != NULL) {
            fsu_error(io, "rm", argv[i], error);
            status = 1;
        }
    }
    return status;
}

static bool find_source(struct ShellIO *io, const char *cmd, const char *verb, const char *path,
                        struct FsuTarget *target) {
    uint32_t dir;
    char absolute[SHELL_PATH_MAX];

    if (ends_in_dot(path)) {
        fsu_error(io, cmd, path, verb[0] == 'c' ? "cannot copy '.' or '..'"
                                                : "cannot move '.' or '..'");
        return false;
    }
    if (fsu_stat(path, target) != 0) {
        bool root = shell_resolve_dir(path, &dir, absolute) == 0;

        fsu_error(io, cmd, path, root ? (verb[0] == 'c' ? "cannot copy the root directory"
                                                       : "cannot move the root directory")
                                      : "not found");
        return false;
    }
    return true;
}

static bool put_file(struct ShellIO *io, const char *cmd, const char *src_path,
                     const struct FsuTarget *src, const struct FsuDest *dest) {
    static char previous_data[FSU_FILE_MAX];
    const char *data;
    uint32_t size;
    uint32_t previous_size = 0;
    uint32_t existing;
    uint8_t type;
    bool exists = fsu_lookup(dest->parent, dest->name, &existing, &type) == 0;

    if (exists && existing == src->inode) {
        fsu_error(io, cmd, src_path, "source and destination are the same file");
        return false;
    }
    if (exists && type == EXT2_FT_DIR) {
        fsu_error(io, cmd, dest->shown, "is a directory");
        return false;
    }
    if (exists) {
        if (fsu_read_file(dest->parent, dest->name, &data, &previous_size) != 0) {
            fsu_error(io, cmd, dest->shown, "cannot back up existing file");
            return false;
        }
        memcpy(previous_data, data, previous_size);
    }
    if (fsu_read_file(src->parent, src->name, &data, &size) != 0) {
        fsu_error(io, cmd, src_path, "cannot read (larger than 64 KiB?)");
        return false;
    }
    if (exists && fsu_remove(dest->parent, dest->name, false) != 0) {
        fsu_error(io, cmd, dest->shown, "cannot replace");
        return false;
    }
    if (fsu_write_file(dest->parent, dest->name, data, size, false) == 0)
        return true;

    if (exists && fsu_write_file(dest->parent, dest->name, previous_data,
                                 previous_size, false) != 0) {
        fsu_error(io, cmd, dest->shown, "cannot write or restore existing file");
        return false;
    }
    fsu_error(io, cmd, dest->shown, "cannot write (disk full?)");
    return false;
}

static bool put_tree(struct ShellIO *io, const char *cmd, const char *src_path,
                     const struct FsuTarget *src, const struct FsuDest *dest) {
    if (fsu_lookup(dest->parent, dest->name, NULL, NULL) == 0) {
        fsu_error(io, cmd, dest->shown, "already exists");
        return false;
    }
    if (fsu_is_inside(dest->parent, src->inode)) {
        fsu_error(io, cmd, src_path, "cannot put a directory inside itself");
        return false;
    }
    if (fsu_copy_tree(src->inode, dest->parent, dest->name) != 0) {
        fsu_error(io, cmd, src_path, "copy failed (disk full?)");
        return false;
    }
    return true;
}

int cmd_cp(int argc, char **argv, struct ShellIO *io) {
    bool recursive;
    int first = recursive_flag(argc, argv, &recursive);
    const char *dest_path = argv[argc - 1];
    bool many = argc - first > 2;
    int status = 0;

    if (argc - first < 2) {
        shell_out_str(io, "usage: cp [-r] SOURCE... DEST\n");
        return 1;
    }
    for (int i = first; i < argc - 1; i++) {
        struct FsuTarget src;
        static struct FsuDest dest;
        const char *error;
        bool ok;

        if (!find_source(io, "cp", "copy", argv[i], &src)) {
            status = 1;
            continue;
        }
        error = fsu_destination(dest_path, src.name, many, &dest);
        if (error != NULL) {
            fsu_error(io, "cp", dest_path, error);
            return 1;
        }
        if (src.type == EXT2_FT_DIR) {
            if (!recursive) {
                fsu_error(io, "cp", argv[i], "is a directory (use cp -r)");
                ok = false;
            } else {
                ok = put_tree(io, "cp", argv[i], &src, &dest);
            }
        } else {
            ok = put_file(io, "cp", argv[i], &src, &dest);
        }
        if (!ok) {
            status = 1;
        }
    }
    return status;
}

int cmd_mv(int argc, char **argv, struct ShellIO *io) {
    bool many = argc > 3;
    int status = 0;

    if (argc < 3) {
        shell_out_str(io, "usage: mv SOURCE... DEST\n");
        return 1;
    }
    const char *dest_path = argv[argc - 1];
    for (int i = 1; i < argc - 1; i++) {
        struct FsuTarget src;
        struct FsuDest dest;
        uint32_t existing;
        const char *error_path = argv[i];

        if (!find_source(io, "mv", "move", argv[i], &src)) {
            status = 1;
            continue;
        }
        const char *error = fsu_destination(dest_path, src.name, many, &dest);
        if (error != NULL) {
            fsu_error(io, "mv", dest_path, error);
            return 1;
        }
        uint8_t existing_type;
        if (fsu_lookup(dest.parent, dest.name, &existing, &existing_type) == 0) {
            if (existing == src.inode)
                error = "source and destination are the same";
            else if (src.type == EXT2_FT_DIR || existing_type == EXT2_FT_DIR) {
                error = "already exists";
                error_path = dest.shown;
            }
        }
        if (error == NULL && src.type == EXT2_FT_DIR) {
            if (fsu_is_inside(shell_cwd_inode(), src.inode))
                error = "is the current directory or above it";
            else if (fsu_is_inside(dest.parent, src.inode))
                error = "cannot put a directory inside itself";
        }
        if (error == NULL) {
            struct EXT2MoveRequest request = {
                .source_parent = src.parent,
                .destination_parent = dest.parent,
                .source_name = src.name,
                .destination_name = dest.name,
                .source_length = (uint8_t)shell_strlen(src.name),
                .destination_length = (uint8_t)shell_strlen(dest.name),
            };
            if (sys_move(&request) != 0)
                error = "cannot move";
        }
        if (error != NULL) {
            fsu_error(io, "mv", error_path, error);
            status = 1;
        }
    }
    return status;
}
