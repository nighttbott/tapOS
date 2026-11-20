#include <stddef.h>
#include "header/stdlib/string.h"
#include "user/commands.h"
#include "user/syscall.h"

/* File commands: cat, cp, rm. */

/* Writes each file in order. With no file it passes the pipeline input through, like UNIX cat. */
int cmd_cat(int argc, char **argv, struct ShellIO *io) {
    int status = 0;

    if (argc == 1) {
        if (io->in == NULL) {
            shell_out_str(io, "usage: cat FILE...\n");
            return 1;
        }
        shell_out(io, io->in, io->in_len);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        const char *data;
        uint32_t size;
        const char *error = fsu_read_path(argv[i], &data, &size);

        if (error != NULL) {
            fsu_error(io, "cat", argv[i], error);
            status = 1;
            continue;
        }
        shell_out(io, data, size);
    }
    return status;
}
