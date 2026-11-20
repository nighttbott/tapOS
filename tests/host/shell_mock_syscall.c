#include <string.h>
#include "user/syscall.h"

char mock_output[65536];
uint32_t mock_output_len;
const char *mock_input = "";

int32_t sys_write_text(const char *buf, uint32_t len) {
    if (len > sizeof(mock_output) - 1 - mock_output_len) {
        return -1;
    }
    memcpy(mock_output + mock_output_len, buf, len);
    mock_output_len += len;
    mock_output[mock_output_len] = '\0';
    return (int32_t)len;
}

int32_t sys_read_char(char *out) {
    static int ready;

    ready = !ready;
    if (!ready) {
        return 0;
    }
    *out = *mock_input != '\0' ? *mock_input++ : '\n';
    return 1;
}

int32_t sys_read_file(struct EXT2DriverRequest *req) {
    return fs_read(*req);
}

int32_t sys_read_dir(struct EXT2DriverRequest *req) {
    return read_directory(req);
}

int32_t sys_write_file(struct EXT2DriverRequest *req) {
    return fs_write(req);
}

int32_t sys_delete(struct EXT2DriverRequest *req) {
    return fs_delete(*req);
}

struct MockExec {
    int32_t  result;
    uint32_t pid;
    int      calls;
    char     name[256];
    uint32_t parent_inode;
    uint32_t buffer_size;
} mock_exec;

int32_t sys_exec(struct EXT2DriverRequest *req, uint32_t *pid_out) {
    mock_exec.calls++;
    memcpy(mock_exec.name, req->name, req->name_len);
    mock_exec.name[req->name_len] = '\0';
    mock_exec.parent_inode = req->parent_inode;
    mock_exec.buffer_size = req->buffer_size;
    if (mock_exec.result == 0 && pid_out != NULL) {
        *pid_out = mock_exec.pid;
    }
    return mock_exec.result;
}

int32_t mock_kill_result;
uint32_t mock_kill_pid;
int mock_kill_calls;

int32_t sys_kill(uint32_t pid) {
    mock_kill_calls++;
    mock_kill_pid = pid;
    return mock_kill_result;
}

struct SyscallProcessInfo mock_ps[16];
int32_t mock_ps_count;

int32_t sys_process_list(struct SyscallProcessInfo *buf, uint32_t capacity) {
    int32_t count = mock_ps_count < (int32_t)capacity ? mock_ps_count : (int32_t)capacity;

    if (count > 0) {
        memcpy(buf, mock_ps, (uint32_t)count * sizeof(*buf));
    }
    return count;
}

int32_t sys_read_dir_block(uint32_t inode, uint32_t index, void *buffer) {
    return ext2_read_directory_block(inode, index, buffer);
}

int32_t sys_move(const struct EXT2MoveRequest *request) {
    return ext2_move(request);
}
