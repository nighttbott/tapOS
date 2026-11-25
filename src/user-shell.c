#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/user/commands.h"
#include "header/user/shell.h"
#include "header/user/syscall.h"

static const struct ShellCommand commands[] = {
    { "cd", cmd_cd },
    { "ls", cmd_ls },
    { "mkdir", cmd_mkdir },
    { "cat", cmd_cat },
    { "cp", cmd_cp },
    { "rm", cmd_rm },
    { "mv", cmd_mv },
    { "find", cmd_find },
    { "grep", cmd_grep },
    { "exec", cmd_exec },
    { "run", cmd_run },
    { "ps", cmd_ps },
    { "kill", cmd_kill },
};

static uint32_t cwd_inode = SHELL_ROOT_INODE;
static char cwd_path[SHELL_PATH_MAX] = "/";

uint32_t shell_strlen(const char *text) {
    uint32_t len = 0;
    while (text[len] != '\0') {
        len++;
    }
    return len;
}

int shell_strcmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static void print(const char *text) {
    sys_write_text(text, shell_strlen(text));
}

uint32_t shell_cwd_inode(void) {
    return cwd_inode;
}

const char *shell_cwd_path(void) {
    return cwd_path;
}

void shell_cwd_set(uint32_t inode, const char *absolute_path) {
    uint32_t len = shell_strlen(absolute_path);

    if (len >= SHELL_PATH_MAX) {
        return;
    }
    memcpy(cwd_path, absolute_path, len + 1);
    cwd_inode = inode;
}

static int dir_child(uint32_t dir, const char *name, uint32_t len, uint32_t *child) {
    if (len == 2 && name[0] == '.' && name[1] == '.') {
        *child = fsu_parent(dir);
        return *child == 0 ? -1 : 0;
    }

    char entry_name[SHELL_NAME_MAX + 1];
    uint8_t type;
    memcpy(entry_name, name, len);
    entry_name[len] = '\0';
    return fsu_lookup(dir, entry_name, child, &type) == 0 && type == EXT2_FT_DIR ? 0 : -1;
}

static int walk(const char *path, uint32_t len, uint32_t *inode, char *absolute) {
    uint32_t dir = path[0] == '/' ? SHELL_ROOT_INODE : cwd_inode;
    uint32_t absolute_len = 0;
    uint32_t i = 0;

    if (absolute != NULL) {
        const char *base = path[0] == '/' ? "/" : cwd_path;
        absolute_len = shell_strlen(base);
        memcpy(absolute, base, absolute_len + 1);
    }

    while (i < len) {
        uint32_t start;
        uint32_t name_len;

        while (i < len && path[i] == '/') {
            i++;
        }
        start = i;
        while (i < len && path[i] != '/') {
            i++;
        }
        name_len = i - start;

        if (name_len == 0 || (name_len == 1 && path[start] == '.')) {
            continue;
        }
        if (name_len > SHELL_NAME_MAX ||
            dir_child(dir, path + start, name_len, &dir) != 0) {
            return -1;
        }
        if (absolute == NULL) {
            continue;
        }

        if (name_len == 2 && path[start] == '.' && path[start + 1] == '.') {
            while (absolute_len > 1 && absolute[--absolute_len] != '/') {
            }
        } else {
            if (absolute_len + 1 + name_len >= SHELL_PATH_MAX) {
                return -1;
            }
            if (absolute_len > 1) {
                absolute[absolute_len++] = '/';
            }
            memcpy(absolute + absolute_len, path + start, name_len);
            absolute_len += name_len;
        }
        absolute[absolute_len] = '\0';
    }

    *inode = dir;
    return 0;
}

int shell_resolve_parent(const char *path, uint32_t *parent_inode, char *last_name) {
    uint32_t end = shell_strlen(path);
    uint32_t start;

    while (end > 0 && path[end - 1] == '/') {
        end--;
    }
    start = end;
    while (start > 0 && path[start - 1] != '/') {
        start--;
    }
    if (end == 0 || end - start > SHELL_NAME_MAX ||
        walk(path, start, parent_inode, NULL) != 0) {
        return -1;
    }
    memcpy(last_name, path + start, end - start);
    last_name[end - start] = '\0';
    return 0;
}

int shell_resolve_dir(const char *path, uint32_t *dir_inode, char *absolute_path) {
    return walk(path, shell_strlen(path), dir_inode, absolute_path);
}

int shell_execute_line(char *line) {
    return shell_execute_commands(line, commands, sizeof(commands) / sizeof(commands[0]));
}

bool shell_read_line(char *line) {
    uint32_t len = 0;
    bool too_long = false;
    char c;

    for (;;) {
        while (sys_read_char(&c) != 1) {
        }

        if (c == '\n') {
            sys_write_text(&c, 1);
            line[len] = '\0';
            if (too_long) {
                print("shell: input line too long\n");
            }
            return !too_long;
        }
        if (too_long) {
            continue;
        }
        if (c == '\b') {
            if (len > 0) {
                len--;
                sys_write_text(&c, 1);
            }
        } else if (c >= 0x20 && c <= 0x7E) {
            if (len == SHELL_LINE_MAX - 1) {
                too_long = true;
            } else {
                line[len++] = c;
                sys_write_text(&c, 1);
            }
        }
    }
}

void shell_print_prompt(void) {
    print("\x1F" "tap");
    print("\x1E" "OS");
    print("\x17" ":");
    print("\x1B");
    print(cwd_path);
    print("\x1F" "$ ");
}

int main(void) {
    static char line[SHELL_LINE_MAX];

    for (;;) {
        shell_print_prompt();
        if (shell_read_line(line)) {
            shell_execute_line(line);
        }
    }
}
