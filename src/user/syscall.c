#include "header/cpu/syscall.h"
#include "user/syscall.h"

static inline int32_t sys_call(uint32_t eax, uint32_t ebx, uint32_t ecx) {
    int32_t ret;
    __asm__ volatile("int $0x30" : "=a"(ret) : "a"(eax), "b"(ebx), "c"(ecx) : "memory");
    return ret;
}

int32_t sys_write_text(const char *buf, uint32_t len) {
    return sys_call(SYSCALL_WRITE_TEXT, (uint32_t)buf, len);
}

int32_t sys_read_char(char *out) {
    return sys_call(SYSCALL_READ_CHAR, (uint32_t)out, 1);
}

int32_t sys_read_file(struct EXT2DriverRequest *req) {
    return sys_call(SYSCALL_READ_FILE, (uint32_t)req, 0);
}

int32_t sys_read_dir(struct EXT2DriverRequest *req) {
    return sys_call(SYSCALL_READ_DIRECTORY, (uint32_t)req, 0);
}

int32_t sys_write_file(struct EXT2DriverRequest *req) {
    return sys_call(SYSCALL_WRITE_FILE, (uint32_t)req, 0);
}

int32_t sys_delete(struct EXT2DriverRequest *req) {
    return sys_call(SYSCALL_DELETE_FILE, (uint32_t)req, 0);
}
