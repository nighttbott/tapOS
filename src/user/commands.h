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

/* Writes "cmd: path: message\n". */
void fsu_error(struct ShellIO *io, const char *cmd, const char *path, const char *message);
/* True for "." and "..". */
bool fsu_is_dot(const char *name, uint32_t len);

#endif
