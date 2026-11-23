#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/user/shell.h"
#include "header/user/syscall.h"

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
