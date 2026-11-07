#include <stddef.h>
#include "header/stdlib/string.h"
#include "user/shell.h"
#include "user/syscall.h"

#define GREP_FILE_MAX 16384u

/* Returns NULL on success, otherwise the error message. */
static const char *read_file(const char *path, char *buffer, uint32_t *len) {
    char name[SHELL_NAME_MAX + 1];
    uint32_t parent;
    struct EXT2DriverRequest request = {
        .buf = buffer,
        .name = name,
        .buffer_size = GREP_FILE_MAX,
    };

    if (shell_resolve_parent(path, &parent, name) != 0) {
        return "not found";
    }
    request.parent_inode = parent;
    request.name_len = (uint8_t)shell_strlen(name);

    memset(buffer, 0, GREP_FILE_MAX);
    switch (sys_read_file(&request)) {
        case 0:
            break;
        case 1:
            return "not a file";
        case 2:
            return "file too large";
        case 3:
            return "not found";
        default:
            return "read failed";
    }

    // shortcut: read() does not return the size, so trailing NUL bytes of a file are dropped.
    *len = GREP_FILE_MAX;
    while (*len > 0 && buffer[*len - 1] == '\0') {
        (*len)--;
    }
    return NULL;
}

static bool contains(const char *line, uint32_t len, const char *pattern, uint32_t pattern_len) {
    for (uint32_t i = 0; i + pattern_len <= len; i++) {
        if (memcmp(line + i, pattern, pattern_len) == 0) {
            return true;
        }
    }
    return false;
}

int cmd_grep(int argc, char **argv, struct ShellIO *io) {
    static char file[GREP_FILE_MAX];
    const char *text = io->in;
    uint32_t len = io->in_len;
    uint32_t pattern_len;
    uint32_t start = 0;

    if (argc != 2 && argc != 3) {
        shell_out_str(io, "usage: grep PATTERN [FILE]\n");
        return 1;
    }
    if (argc == 3) {
        const char *error = read_file(argv[2], file, &len);
        if (error != NULL) {
            shell_out_str(io, "grep: ");
            shell_out_str(io, argv[2]);
            shell_out_str(io, ": ");
            shell_out_str(io, error);
            shell_out_str(io, "\n");
            return 1;
        }
        text = file;
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
