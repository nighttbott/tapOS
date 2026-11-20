#ifndef _USER_COMMANDS_H
#define _USER_COMMANDS_H

#include <stdbool.h>
#include <stdint.h>
#include "user/shell.h"

#define FSU_DIR_BUFFER 16384u
#define FSU_FILE_MAX   65536u
#define FSU_DEPTH_MAX  64u

struct FsuEntry {
    uint32_t    inode;
    uint8_t     type;
    const char *name;
    uint32_t    name_len;
};

struct FsuDest {
    uint32_t parent;
    char     name[SHELL_NAME_MAX + 1];
    char     shown[SHELL_PATH_MAX + SHELL_NAME_MAX + 2];
};

struct FsuTarget {
    uint32_t parent;
    char     name[SHELL_NAME_MAX + 1];
    uint32_t inode;
    uint8_t  type;
};

const uint8_t *fsu_list(uint32_t dir);
bool fsu_next_entry(const uint8_t *listing, uint32_t *offset, struct FsuEntry *entry);
int fsu_read_dir_entry(uint32_t dir, uint32_t *cursor, struct FsuEntry *entry,
                       char name[SHELL_NAME_MAX + 1]);
int fsu_lookup(uint32_t dir, const char *name, uint32_t *inode, uint8_t *type);
int fsu_stat(const char *path, struct FsuTarget *target);
uint32_t fsu_parent(uint32_t dir);
bool fsu_is_inside(uint32_t dir, uint32_t ancestor);

int32_t fsu_read_file(uint32_t parent, const char *name, const char **data, uint32_t *size);
const char *fsu_read_path(const char *path, const char **data, uint32_t *size);

int32_t fsu_remove(uint32_t parent, const char *name, bool is_dir);
int32_t fsu_remove_tree(uint32_t parent, const char *name);

int32_t fsu_write_file(uint32_t parent, const char *name, const void *data, uint32_t size,
                       bool is_dir);
int32_t fsu_copy_tree(uint32_t src, uint32_t dst_parent, const char *name);
const char *fsu_destination(const char *dest, const char *src_name, bool many,
                            struct FsuDest *out);

void fsu_error(struct ShellIO *io, const char *cmd, const char *path, const char *message);
bool fsu_is_dot(const char *name, uint32_t len);

int cmd_cd(int argc, char **argv, struct ShellIO *io);
int cmd_ls(int argc, char **argv, struct ShellIO *io);
int cmd_mkdir(int argc, char **argv, struct ShellIO *io);
int cmd_cat(int argc, char **argv, struct ShellIO *io);
int cmd_rm(int argc, char **argv, struct ShellIO *io);
int cmd_cp(int argc, char **argv, struct ShellIO *io);
int cmd_mv(int argc, char **argv, struct ShellIO *io);
int cmd_find(int argc, char **argv, struct ShellIO *io);

#endif
