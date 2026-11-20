#include <stddef.h>
#include "header/stdlib/string.h"
#include "user/commands.h"

static bool contains(const char *line, uint32_t len, const char *pattern, uint32_t pattern_len) {
    for (uint32_t i = 0; i + pattern_len <= len; i++) {
        if (memcmp(line + i, pattern, pattern_len) == 0) {
            return true;
        }
    }
    return false;
}

int cmd_grep(int argc, char **argv, struct ShellIO *io) {
    const char *text = io->in;
    uint32_t len = io->in_len;
    uint32_t pattern_len;
    uint32_t start = 0;

    if (argc != 2 && argc != 3) {
        shell_out_str(io, "usage: grep PATTERN [FILE]\n");
        return 1;
    }
    if (argc == 3) {
        const char *error = fsu_read_path(argv[2], &text, &len);
        if (error != NULL) {
            shell_out_str(io, "grep: ");
            shell_out_str(io, argv[2]);
            shell_out_str(io, ": ");
            shell_out_str(io, error);
            shell_out_str(io, "\n");
            return 1;
        }
    } else if (text == NULL) {
        shell_out_str(io, "grep: no input\n");
        return 1;
    }

    pattern_len = shell_strlen(argv[1]);
    while (start < len) {
        uint32_t end = start;

        while (end < len && text[end] != '\n') {
            end++;
        }
        if (contains(text + start, end - start, argv[1], pattern_len)) {
            shell_out(io, text + start, end - start);
            shell_out(io, "\n", 1);
        }
        start = end + 1;
    }
    return 0;
}
