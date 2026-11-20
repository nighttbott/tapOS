#include <stddef.h>
#include "header/stdlib/string.h"
#include "user/commands.h"
#include "user/syscall.h"

/* File commands: cat, cp, rm, mv. */

/* Writes each file in order. With no file it passes the pipeline input through, like UNIX cat. */
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

/* True when the last component of path is "." or "..". */
static bool ends_in_dot(const char *path) {
    char name[SHELL_NAME_MAX + 1];
    uint32_t parent;

    return shell_resolve_parent(path, &parent, name) == 0 && fsu_is_dot(name, shell_strlen(name));
}

/* "-r" or "-R" as the first argument. Returns the index of the first operand. */
static int recursive_flag(int argc, char **argv, bool *recursive) {
    *recursive = argc > 1 &&
                 (shell_strcmp(argv[1], "-r") == 0 || shell_strcmp(argv[1], "-R") == 0);
    return *recursive ? 2 : 1;
}

/* Returns NULL on success, otherwise the error message. */
static const char *remove_one(const char *path, bool recursive) {
    struct FsuTarget target;
    int32_t code;

    if (ends_in_dot(path)) {
        return "cannot remove '.' or '..'";
    }
    if (fsu_stat(path, &target) != 0) {
        uint32_t dir;
        char absolute[SHELL_PATH_MAX];

        // Only the root resolves as a folder without being an entry of some parent.
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

/* Removes files; folders need -r. The current folder and the folders above it are kept. */
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

/*
 * Finds the source of cp or mv. Returns true, or writes the error and returns false.
 * verb is "copy" or "move", used in messages.
 */
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

/*
 * Writes the file src to dest, replacing a file already there. A folder in the way is an error.
 * Returns true, or writes the error and returns false.
 */
static bool put_file(struct ShellIO *io, const char *cmd, const char *src_path,
                     const struct FsuTarget *src, const struct FsuDest *dest) {
    const char *data;
    uint32_t size;
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
    if (fsu_read_file(src->parent, src->name, &data, &size) != 0) {
        fsu_error(io, cmd, src_path, "cannot read (larger than 64 KiB?)");
        return false;
    }
    if (exists && fsu_remove(dest->parent, dest->name, false) != 0) {
        fsu_error(io, cmd, dest->shown, "cannot replace");
        return false;
    }
    if (fsu_write_file(dest->parent, dest->name, data, size, false) != 0) {
        fsu_error(io, cmd, dest->shown, "cannot write (disk full?)");
        return false;
    }
    return true;
}

/* Copies folder src to dest. Returns true, or writes the error and returns false. */
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

/* cp [-r] SOURCE DEST, or cp [-r] SOURCE... FOLDER. An existing file at DEST is replaced. */
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

/*
 * mv SOURCE DEST, or mv SOURCE... FOLDER; renames as well as moves. There is no rename syscall,
 * so the source is copied with cp's code and removed only after the copy fully succeeded.
 */
int cmd_mv(int argc, char **argv, struct ShellIO *io) {
    const char *dest_path = argv[argc - 1];
    bool many = argc > 3;
    int status = 0;

    if (argc < 3) {
        shell_out_str(io, "usage: mv SOURCE... DEST\n");
        return 1;
    }
    for (int i = 1; i < argc - 1; i++) {
        struct FsuTarget src;
        static struct FsuDest dest;
        const char *error;
        uint32_t existing;
        bool ok;
        int32_t code;

        if (!find_source(io, "mv", "move", argv[i], &src)) {
            status = 1;
            continue;
        }
        error = fsu_destination(dest_path, src.name, many, &dest);
        if (error != NULL) {
            fsu_error(io, "mv", dest_path, error);
            return 1;
        }
        if (fsu_lookup(dest.parent, dest.name, &existing, NULL) == 0 && existing == src.inode) {
            fsu_error(io, "mv", argv[i], "source and destination are the same");
            status = 1;
            continue;
        }
        if (src.type == EXT2_FT_DIR) {
            // The copy gets new inodes, so the shell would be left inside a deleted folder.
            if (fsu_is_inside(shell_cwd_inode(), src.inode)) {
                fsu_error(io, "mv", argv[i], "is the current directory or above it");
                status = 1;
                continue;
            }
            ok = put_tree(io, "mv", argv[i], &src, &dest);
            code = ok ? fsu_remove_tree(src.parent, src.name) : 0;
        } else {
            ok = put_file(io, "mv", argv[i], &src, &dest);
            code = ok ? fsu_remove(src.parent, src.name, false) : 0;
        }
        if (!ok) {
            status = 1;
        } else if (code != 0) {
            fsu_error(io, "mv", argv[i], "copied, but the original could not be removed");
            status = 1;
        }
    }
    return status;
}
