#include <stddef.h>
#include "header/user/shell.h"
#include "header/user/syscall.h"

#define PS_MAX 16

static uint32_t format_uint(char *out, uint32_t value) {
    uint32_t len = 0;

    for (uint32_t rest = value; rest != 0 || len == 0; rest /= 10) {
        len++;
    }
    for (uint32_t i = len; i > 0; value /= 10) {
        out[--i] = (char)('0' + value % 10);
    }
    return len;
}

static void out_padded(struct ShellIO *io, const char *text, uint32_t len, uint32_t width) {
    shell_out(io, text, len);
    for (; len < width; len++) {
        shell_out(io, " ", 1);
    }
}

static bool parse_uint(const char *text, uint32_t *value) {
    *value = 0;
    if (*text == '\0') {
        return false;
    }
    for (; *text != '\0'; text++) {
        uint32_t digit = (uint32_t)(*text - '0');
        if (*text < '0' || *text > '9' || *value > (0xFFFFFFFFu - digit) / 10) {
            return false;
        }
        *value = *value * 10 + digit;
    }
    return true;
}

static int fail(struct ShellIO *io, const char *cmd, const char *path, const char *message) {
    shell_out_str(io, cmd);
    shell_out_str(io, ": ");
    if (path != NULL) {
        shell_out_str(io, path);
        shell_out_str(io, ": ");
    }
    shell_out_str(io, message);
    shell_out_str(io, "\n");
    return 1;
}

/* Starts PATH as a new process. `cmd` names the command in messages; say = print the PID line. */
static int start_process(const char *cmd, int argc, char **argv, struct ShellIO *io, uint32_t *pid, bool say) {
    char name[SHELL_NAME_MAX + 1];
    uint32_t parent;
    struct EXT2DriverRequest request = { .name = name, .buffer_size = 0 };

    if (argc != 2) {
        shell_out_str(io, "usage: ");
        shell_out_str(io, cmd);
        shell_out_str(io, " PATH\n");
        return 1;
    }
    if (shell_resolve_parent(argv[1], &parent, name) != 0) {
        return fail(io, cmd, argv[1], "not found");
    }
    request.parent_inode = parent;
    request.name_len = (uint8_t)shell_strlen(name);

    switch (sys_exec(&request, pid)) {
        case 0:
            if (say) {
                char digits[10];
                shell_out_str(io, "exec: started ");
                shell_out_str(io, name);
                shell_out_str(io, " (PID ");
                shell_out(io, digits, format_uint(digits, *pid));
                shell_out_str(io, ")\n");
            }
            return 0;
        case 1:
            return fail(io, cmd, NULL, "too many processes");
        case 2:
            return fail(io, cmd, argv[1], "not an executable");
        case 3:
            return fail(io, cmd, NULL, "not enough memory");
        case 4:
            return fail(io, cmd, argv[1], "not found");
        default:
            return fail(io, cmd, NULL, "invalid request");
    }
}

int cmd_exec(int argc, char **argv, struct ShellIO *io) {
    uint32_t pid = 0;

    return start_process("exec", argc, argv, io, &pid, true);
}

/* Like exec, but the shell waits (and reads no keys) until the program has ended. */
int cmd_run(int argc, char **argv, struct ShellIO *io) {
    static struct SyscallProcessInfo list[PS_MAX];
    uint32_t pid = 0;
    bool alive = true;

    if (start_process("run", argc, argv, io, &pid, false) != 0) {
        return 1;
    }
    while (alive) {
        int32_t count = sys_process_list(list, PS_MAX);

        alive = false;
        for (int32_t i = 0; i < count && i < PS_MAX; i++) {
            alive = alive || list[i].pid == pid;
        }
    }
    sys_redraw();
    return 0;
}

int cmd_ps(int argc, char **argv, struct ShellIO *io) {
    static struct SyscallProcessInfo list[PS_MAX];
    int32_t count = sys_process_list(list, PS_MAX);

    (void)argc;
    (void)argv;
    if (count < 0) {
        shell_out_str(io, "ps: cannot read the process list\n");
        return 1;
    }

    shell_out_str(io, "PID  STATE    NAME\n");
    for (int32_t i = 0; i < count && i < PS_MAX; i++) {
        const char *state = list[i].state == 2 ? "running" : list[i].state == 1 ? "ready" : "unknown";
        char pid[10];
        uint32_t pid_len = format_uint(pid, list[i].pid);
        uint32_t name_len = 0;

        while (name_len < SYSCALL_PROCESS_NAME_MAX && list[i].name[name_len] != '\0') {
            name_len++;
        }

        out_padded(io, pid, pid_len, 5);
        out_padded(io, state, shell_strlen(state), 9);
        shell_out(io, list[i].name, name_len);
        shell_out(io, "\n", 1);
    }
    return 0;
}

int cmd_kill(int argc, char **argv, struct ShellIO *io) {
    uint32_t pid;

    if (argc != 2 || !parse_uint(argv[1], &pid)) {
        shell_out_str(io, "usage: kill PID\n");
        return 1;
    }
    switch (sys_kill(pid)) {
        case 0:
            return 0;
        case 1:
            shell_out_str(io, "kill: ");
            shell_out_str(io, argv[1]);
            shell_out_str(io, ": no such process\n");
            return 1;
        case 2:
            shell_out_str(io, "kill: cannot kill init (PID 1)\n");
            return 1;
        default:
            shell_out_str(io, "kill: ");
            shell_out_str(io, argv[1]);
            shell_out_str(io, ": failed\n");
            return 1;
    }
}
