#include <stddef.h>
#include "user/shell.h"

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
