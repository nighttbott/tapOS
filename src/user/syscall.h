#ifndef _USER_SYSCALL_H
#define _USER_SYSCALL_H

#include <stdint.h>
#include "header/cpu/syscall.h"
#include "header/filesystem/ext2.h"

struct CMOSTime;  // defined in header/driver/cmos.h

/* Ring 3 side of the team syscall ABI. Results are described in docs/shell.md. */
int32_t sys_write_text(const char *buf, uint32_t len);
/* Does not wait: returns 1 with a key, 0 without one. */
int32_t sys_read_char(char *out);
int32_t sys_read_file(struct EXT2DriverRequest *req);
int32_t sys_read_dir(struct EXT2DriverRequest *req);
int32_t sys_write_file(struct EXT2DriverRequest *req);
int32_t sys_delete(struct EXT2DriverRequest *req);

/* Does not return once the kernel implements it. */
void    sys_exit(int32_t code);
/* Only name, name_len and parent_inode are used; buffer_size must be 0. pid_out may be NULL. */
int32_t sys_exec(struct EXT2DriverRequest *req, uint32_t *pid_out);
int32_t sys_kill(uint32_t pid);
/* Returns the number of rows written, -1 on a bad buffer. */
int32_t sys_process_list(struct SyscallProcessInfo *buf, uint32_t capacity);
int32_t sys_get_time(struct CMOSTime *out);
/* Writes on the status row (row 24) from column col; does not move the console cursor. */
int32_t sys_write_status(const char *buf, uint32_t len, uint32_t col);

#endif
