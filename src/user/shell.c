#include <stddef.h>
#include "header/stdlib/string.h"
#include "user/commands.h"
#include "user/shell.h"
#include "user/syscall.h"

// shortcut: a folder bigger than 32 blocks cannot be entered, raise this if a folder ever needs it.
#define SHELL_DIR_BUFFER 16384u

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

void shell_out(struct ShellIO *io, const char *text, uint32_t len) {
    uint32_t room = SHELL_IO_BUFFER - io->out_len;

    if (len > room) {
        len = room;
        io->overflow = true;
    }
    memcpy(io->out + io->out_len, text, len);
    io->out_len += len;
}

void shell_out_str(struct ShellIO *io, const char *text) {
    shell_out(io, text, shell_strlen(text));
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

/* read_directory has no inode result, but the first entry of a folder is "." */
static int dir_child(uint32_t dir, const char *name, uint32_t len, uint32_t *child) {
    static uint8_t table[SHELL_DIR_BUFFER];
    struct EXT2DirectoryEntry dot;
    struct EXT2DriverRequest request = {
        .buf = table,
        .name = (char *)name,
        .name_len = (uint8_t)len,
        .parent_inode = dir,
        .buffer_size = SHELL_DIR_BUFFER,
        .is_directory = true,
    };

    if (sys_read_dir(&request) != 0) {
        return -1;
    }
    memcpy(&dot, table, sizeof(dot));
    *child = dot.inode;
    return 0;
}

/* Follows the first len bytes of path. absolute may be NULL when only the inode is needed. */
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

const char *shell_parse(char *line, struct ShellPipeline *pipeline) {
    struct ShellStage *stage = &pipeline->stages[0];
    bool in_word = false;

    pipeline->count = 1;
    stage->argc = 0;

    for (char *cursor = line;; cursor++) {
        char c = *cursor;

        if (c != ' ' && c != '|' && c != '\0') {
            if (!in_word) {
                if (stage->argc == SHELL_MAX_ARGS) {
                    return "too many arguments";
                }
                stage->argv[stage->argc++] = cursor;
                in_word = true;
            }
            continue;
        }

        *cursor = '\0';
        in_word = false;
        if (c == ' ') {
            continue;
        }
        if (stage->argc == 0 && (c == '|' || pipeline->count > 1)) {
            return "empty command in pipeline";
        }
        if (c == '\0') {
            if (stage->argc == 0) {
                pipeline->count = 0;
            }
            return NULL;
        }
        if (pipeline->count == SHELL_MAX_STAGES) {
            return "too many commands in pipeline";
        }
        stage = &pipeline->stages[pipeline->count++];
        stage->argc = 0;
    }
}

static shell_command_fn find_command(const struct ShellCommand *table, uint32_t count,
                                     const char *name) {
    for (uint32_t i = 0; i < count; i++) {
        if (shell_strcmp(table[i].name, name) == 0) {
            return table[i].run;
        }
    }
    return NULL;
}

int shell_execute_commands(char *line, const struct ShellCommand *table, uint32_t count) {
    static char buffers[2][SHELL_IO_BUFFER];
    struct ShellPipeline pipeline;
    shell_command_fn stages[SHELL_MAX_STAGES];
    const char *error = shell_parse(line, &pipeline);
    const char *in = NULL;
    uint32_t in_len = 0;

    if (error != NULL) {
        print("shell: ");
        print(error);
        print("\n");
        return -1;
    }

    // Look everything up first: "rm a | typo" must not delete a.
    for (uint32_t i = 0; i < pipeline.count; i++) {
        stages[i] = find_command(table, count, pipeline.stages[i].argv[0]);
        if (stages[i] == NULL) {
            print(pipeline.stages[i].argv[0]);
            print(": command not found\n");
            return -1;
        }
    }

    for (uint32_t i = 0; i < pipeline.count; i++) {
        struct ShellIO io = { in, in_len, buffers[i % 2], 0, false };
        int status = stages[i](pipeline.stages[i].argc, pipeline.stages[i].argv, &io);

        if (io.overflow) {
            print("pipeline: output too large\n");
            return -1;
        }
        if (status != 0 || i + 1 == pipeline.count) {
            sys_write_text(io.out, io.out_len);
            return status;
        }
        in = io.out;
        in_len = io.out_len;
    }
    return 0;
}

int shell_execute_line(char *line) {
    return shell_execute_commands(line, commands, sizeof(commands) / sizeof(commands[0]));
}

void shell_read_line(char *line) {
    uint32_t len = 0;
    char c;

    for (;;) {
        while (sys_read_char(&c) != 1) {
        }

        if (c == '\n') {
            sys_write_text(&c, 1);
            line[len] = '\0';
            return;
        }
        if (c == '\b') {
            if (len > 0) {
                len--;
                sys_write_text(&c, 1);
            }
        } else if (c >= 0x20 && c <= 0x7E && len < SHELL_LINE_MAX - 1) {
            line[len++] = c;
            sys_write_text(&c, 1);
        }
    }
}

void shell_print_prompt(void) {
    print("tapOS:");
    print(cwd_path);
    print("$ ");
}

int main(void) {
    static char line[SHELL_LINE_MAX];

    for (;;) {
        shell_print_prompt();
        shell_read_line(line);
        shell_execute_line(line);
    }
}
