#ifndef _USER_SHELL_H
#define _USER_SHELL_H

#include <stdbool.h>
#include <stdint.h>

#define SHELL_LINE_MAX      128
#define SHELL_MAX_ARGS      16
#define SHELL_MAX_STAGES    4      // most commands in one pipeline
#define SHELL_IO_BUFFER     16384  // a 16 KiB file fits through a pipeline
#define SHELL_PATH_MAX      256
#define SHELL_NAME_MAX      255    // name_len of a request is 8 bits
#define SHELL_ROOT_INODE    1u

struct ShellIO {
    const char *in;       // output of the previous stage; NULL for the first stage
    uint32_t    in_len;
    char       *out;      // output buffer of this stage, SHELL_IO_BUFFER bytes
    uint32_t    out_len;
    bool        overflow; // true when out is full
};

/*
 * Every command has this shape. Return 0 on success. On non-zero the shell
 * prints what the command wrote to io and stops the pipeline, so error
 * messages go through shell_out like normal output.
 */
typedef int (*shell_command_fn)(int argc, char **argv, struct ShellIO *io);

struct ShellCommand {
    const char      *name;
    shell_command_fn run;
};

/* Command output. Always use these, not sys_write_text, so pipelines work. */
void shell_out(struct ShellIO *io, const char *text, uint32_t len);
void shell_out_str(struct ShellIO *io, const char *text);

/* CWD state. shell_cwd_set ignores a path that does not fit SHELL_PATH_MAX. */
uint32_t    shell_cwd_inode(void);
const char *shell_cwd_path(void);
void        shell_cwd_set(uint32_t inode, const char *absolute_path);

/*
 * Path resolution: absolute ("/a/b"), relative ("a/b"), ".", "..".
 * Both return 0 on success and -1 on failure.
 *
 * Fills the parent folder inode and the last component. The last component
 * itself may not exist yet. last_name holds SHELL_NAME_MAX + 1 bytes.
 */
int shell_resolve_parent(const char *path, uint32_t *parent_inode, char *last_name);
/* Fills the target folder inode and its normalized path (SHELL_PATH_MAX bytes), for cd. */
int shell_resolve_dir(const char *path, uint32_t *dir_inode, char *absolute_path);

/* libc is not available in user programs. */
uint32_t shell_strlen(const char *text);
int      shell_strcmp(const char *a, const char *b);

/* Commands owned by the shell. Commands from E are declared in user/commands.h. */
int cmd_grep(int argc, char **argv, struct ShellIO *io);
int cmd_exec(int argc, char **argv, struct ShellIO *io);
int cmd_ps(int argc, char **argv, struct ShellIO *io);
int cmd_kill(int argc, char **argv, struct ShellIO *io);

/* Shell internals, public so the host tests can reach them. Commands do not need these. */
struct ShellStage {
    int   argc;
    char *argv[SHELL_MAX_ARGS];
};

struct ShellPipeline {
    uint32_t          count;  // 0 for a blank line
    struct ShellStage stages[SHELL_MAX_STAGES];
};

/* Splits line in place. Returns NULL on success, otherwise the error message. */
const char *shell_parse(char *line, struct ShellPipeline *pipeline);
/* line must hold SHELL_LINE_MAX bytes. */
void shell_read_line(char *line);
void shell_print_prompt(void);
int  shell_execute_line(char *line);
int  shell_execute_commands(char *line, const struct ShellCommand *table, uint32_t count);

#endif
