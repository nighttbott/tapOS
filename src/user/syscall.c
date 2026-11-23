#include "header/cpu/syscall.h"
#include "header/user/syscall.h"

static inline int32_t sys_call(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    int32_t ret;
    __asm__ volatile("int $0x30" : "=a"(ret) : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
    return ret;
}

int32_t sys_write_text(const char *buf, uint32_t len) {
    return sys_call(SYSCALL_WRITE_TEXT, (uint32_t)buf, len, 0);
}

int32_t sys_read_char(char *out) {
    return sys_call(SYSCALL_READ_CHAR, (uint32_t)out, 1, 0);
}

int32_t sys_read_file(struct EXT2DriverRequest *req) {
    return sys_call(SYSCALL_READ_FILE, (uint32_t)req, 0, 0);
}

int32_t sys_read_dir(struct EXT2DriverRequest *req) {
    return sys_call(SYSCALL_READ_DIRECTORY, (uint32_t)req, 0, 0);
}

int32_t sys_write_file(struct EXT2DriverRequest *req) {
    return sys_call(SYSCALL_WRITE_FILE, (uint32_t)req, 0, 0);
}

int32_t sys_delete(struct EXT2DriverRequest *req) {
    return sys_call(SYSCALL_DELETE_FILE, (uint32_t)req, 0, 0);
}

void sys_exit(int32_t code) {
    sys_call(SYSCALL_EXIT, (uint32_t)code, 0, 0);
}

int32_t sys_exec(struct EXT2DriverRequest *req, uint32_t *pid_out) {
    return sys_call(SYSCALL_EXEC, (uint32_t)req, (uint32_t)pid_out, 0);
}

int32_t sys_kill(uint32_t pid) {
    return sys_call(SYSCALL_KILL, pid, 0, 0);
}

int32_t sys_process_list(struct SyscallProcessInfo *buf, uint32_t capacity) {
    return sys_call(SYSCALL_PROCESS_LIST, (uint32_t)buf, capacity, 0);
}

int32_t sys_get_time(struct CMOSTime *out) {
    return sys_call(SYSCALL_GET_TIME, (uint32_t)out, 0, 0);
}

int32_t sys_write_status(const char *buf, uint32_t len, uint32_t col) {
    return sys_call(SYSCALL_WRITE_STATUS, (uint32_t)buf, len, col);
}

int32_t sys_read_dir_block(uint32_t inode, uint32_t index, void *buffer) {
    return sys_call(SYSCALL_READ_DIRECTORY_BLOCK, inode, index, (uint32_t)buffer);
}

int32_t sys_move(const struct EXT2MoveRequest *request) {
    return sys_call(SYSCALL_MOVE, (uint32_t)request, 0, 0);
}

int32_t sys_find(struct SyscallFindRequest *request) {
    return sys_call(SYSCALL_FIND, (uint32_t)request, 0, 0);
}
