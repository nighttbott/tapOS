#ifndef _USER_SYSCALL_H
#define _USER_SYSCALL_H

#include <stdint.h>
#include "header/cpu/syscall.h"
#include "header/filesystem/ext2.h"

struct CMOSTime;

int32_t sys_write_text(const char *buf, uint32_t len);
int32_t sys_read_char(char *out);
int32_t sys_read_file(struct EXT2DriverRequest *req);
int32_t sys_read_dir(struct EXT2DriverRequest *req);
int32_t sys_write_file(struct EXT2DriverRequest *req);
int32_t sys_delete(struct EXT2DriverRequest *req);

void    sys_exit(int32_t code);
int32_t sys_exec(struct EXT2DriverRequest *req, uint32_t *pid_out);
int32_t sys_kill(uint32_t pid);
int32_t sys_process_list(struct SyscallProcessInfo *buf, uint32_t capacity);
int32_t sys_get_time(struct CMOSTime *out);
int32_t sys_write_status(const char *buf, uint32_t len, uint32_t col);

int32_t sys_read_dir_block(uint32_t inode, uint32_t index, void *buffer);
int32_t sys_move(const struct EXT2MoveRequest *request);

/* Number of matches (see header/filesystem/index.h for the negative codes). */
int32_t sys_find(struct SyscallFindRequest *request);

#endif
