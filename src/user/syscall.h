#ifndef _USER_SYSCALL_H
#define _USER_SYSCALL_H

#include <stdint.h>
#include "header/filesystem/ext2.h"

/* Ring 3 side of the team syscall ABI. Results are described in docs/shell.md. */
int32_t sys_write_text(const char *buf, uint32_t len);
/* Does not wait: returns 1 with a key, 0 without one. */
int32_t sys_read_char(char *out);
int32_t sys_read_file(struct EXT2DriverRequest *req);
int32_t sys_read_dir(struct EXT2DriverRequest *req);
int32_t sys_write_file(struct EXT2DriverRequest *req);
int32_t sys_delete(struct EXT2DriverRequest *req);

#endif
