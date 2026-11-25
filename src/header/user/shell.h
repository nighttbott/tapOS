#ifndef _USER_SHELL_H
#define _USER_SHELL_H

#include <stdbool.h>
#include <stdint.h>

#define SHELL_LINE_MAX      128
#define SHELL_MAX_ARGS      16
#define SHELL_MAX_STAGES    4
#define SHELL_IO_BUFFER     16384
#define SHELL_PATH_MAX      256
#define SHELL_NAME_MAX      255
#define SHELL_ROOT_INODE    1u

struct ShellIO {
    const char *in;
    uint32_t    in_len;
    char       *out;
    uint32_t    out_len;
    bool        overflow;
};

typedef int (*shell_command_fn)(int argc, char **argv, struct ShellIO *io);

struct ShellCommand {
    const char      *name;
    shell_command_fn run;
};

void shell_out(struct ShellIO *io, const char *text, uint32_t len);
void shell_out_str(struct ShellIO *io, const char *text);

uint32_t    shell_cwd_inode(void);
const char *shell_cwd_path(void);
void        shell_cwd_set(uint32_t inode, const char *absolute_path);

int shell_resolve_parent(const char *path, uint32_t *parent_inode, char *last_name);
int shell_resolve_dir(const char *path, uint32_t *dir_inode, char *absolute_path);

uint32_t shell_strlen(const char *text);
int      shell_strcmp(const char *a, const char *b);

int cmd_grep(int argc, char **argv, struct ShellIO *io);
int cmd_exec(int argc, char **argv, struct ShellIO *io);
int cmd_run(int argc, char **argv, struct ShellIO *io);
int cmd_ps(int argc, char **argv, struct ShellIO *io);
int cmd_kill(int argc, char **argv, struct ShellIO *io);

struct ShellStage {
    int   argc;
    char *argv[SHELL_MAX_ARGS];
};

struct ShellPipeline {
    uint32_t          count;
    struct ShellStage stages[SHELL_MAX_STAGES];
};

const char *shell_parse(char *line, struct ShellPipeline *pipeline);
bool shell_read_line(char *line);
void shell_print_prompt(void);
int  shell_execute_line(char *line);
int  shell_execute_commands(char *line, const struct ShellCommand *table, uint32_t count);

#endif
