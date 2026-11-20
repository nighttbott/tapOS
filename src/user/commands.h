#ifndef _USER_COMMANDS_H
#define _USER_COMMANDS_H

#include <stdbool.h>
#include <stdint.h>
#include "user/shell.h"

/*
 * Filesystem commands (cd, ls, mkdir, cat, cp, rm, mv, find) and the helpers they share.
 * Everything goes through the syscall wrappers in user/syscall.h; nothing touches the disk directly.
 */

#define FSU_DIR_BUFFER 16384u  // a folder up to 32 blocks, same limit as shell_resolve_dir
#define FSU_FILE_MAX   65536u  // largest file cat, cp and mv can read
#define FSU_DEPTH_MAX  64u     // deepest folder nesting cp -r, rm -r and mv walk into

/* One child of a folder. name points into the listing and is not '\0' terminated. */
struct FsuEntry {
    uint32_t    inode;
    uint8_t     type;      // EXT2_FT_REG_FILE or EXT2_FT_DIR
    const char *name;
    uint32_t    name_len;
};

/* Where cp or mv puts one source. */
struct FsuDest {
    uint32_t parent;
    char     name[SHELL_NAME_MAX + 1];
    char     shown[SHELL_PATH_MAX + SHELL_NAME_MAX + 2];  // path for messages
};

/* An existing file or folder found from a path typed by the user. */
struct FsuTarget {
    uint32_t parent;                    // folder that holds the entry
    char     name[SHELL_NAME_MAX + 1];  // last path component
    uint32_t inode;
    uint8_t  type;
};

/*
 * Reads a whole folder by inode. Returns the listing, or NULL on failure. The listing lives in
 * one static buffer and stays valid only until the next fsu_list call.
 */
const uint8_t *fsu_list(uint32_t dir);
/* Steps through a listing, skipping unused entries, "." and "..". *offset starts at 0. */
bool fsu_next_entry(const uint8_t *listing, uint32_t *offset, struct FsuEntry *entry);
/* Finds name inside folder dir. Returns 0 and fills inode/type (either may be NULL), or -1. */
int fsu_lookup(uint32_t dir, const char *name, uint32_t *inode, uint8_t *type);
/* Resolves path to an existing entry. Returns 0, or -1 when it does not exist. "." and ".." never match. */
int fsu_stat(const char *path, struct FsuTarget *target);
/* Inode of the folder above dir (root is its own parent), or 0 on failure. */
uint32_t fsu_parent(uint32_t dir);
/* True when dir is ancestor itself or lies somewhere below it. */
bool fsu_is_inside(uint32_t dir, uint32_t ancestor);

/*
 * Reads a whole file. Returns the read() code (0 success, 1 not a file, 2 larger than
 * FSU_FILE_MAX, 3 not found, 4 bad parent, -1 other) and on success points *data at a static
 * buffer holding exactly *size bytes, valid until the next fsu_read_file call.
 */
int32_t fsu_read_file(uint32_t parent, const char *name, const char **data, uint32_t *size);
/* Same as fsu_read_file, from a path. Returns NULL on success, otherwise a short error message. */
const char *fsu_read_path(const char *path, const char **data, uint32_t *size);

/* Deletes a file, or an empty folder when is_dir. Returns the delete() code. */
int32_t fsu_remove(uint32_t parent, const char *name, bool is_dir);
/* Deletes folder name with everything inside it. Returns the delete() code of the folder. */
int32_t fsu_remove_tree(uint32_t parent, const char *name);

/* Creates a file holding data, or an empty folder when is_dir. Returns the write() code. */
int32_t fsu_write_file(uint32_t parent, const char *name, const void *data, uint32_t size,
                       bool is_dir);
/*
 * Creates folder name in dst_parent with a copy of everything inside folder src. Returns the
 * write() code for the new folder, or -1 when copying its content failed; nothing is left behind.
 * The caller must make sure dst_parent does not lie inside src.
 */
int32_t fsu_copy_tree(uint32_t src, uint32_t dst_parent, const char *name);
/*
 * Destination of cp/mv for a source named src_name. An existing folder dest receives the source
 * under its own name; otherwise dest names the new entry, which is only allowed for one source
 * (many == false). Fills out->shown in both cases. Returns NULL, or the error about dest.
 */
const char *fsu_destination(const char *dest, const char *src_name, bool many,
                            struct FsuDest *out);

/* Writes "cmd: path: message\n". */
void fsu_error(struct ShellIO *io, const char *cmd, const char *path, const char *message);
/* True for "." and "..". */
bool fsu_is_dot(const char *name, uint32_t len);

/* Commands, registered in the table in user/shell.c. */
int cmd_cd(int argc, char **argv, struct ShellIO *io);
int cmd_ls(int argc, char **argv, struct ShellIO *io);
int cmd_mkdir(int argc, char **argv, struct ShellIO *io);
int cmd_cat(int argc, char **argv, struct ShellIO *io);
int cmd_rm(int argc, char **argv, struct ShellIO *io);
int cmd_cp(int argc, char **argv, struct ShellIO *io);
int cmd_mv(int argc, char **argv, struct ShellIO *io);
int cmd_find(int argc, char **argv, struct ShellIO *io);

#endif
