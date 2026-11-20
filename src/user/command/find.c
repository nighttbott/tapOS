#include <stddef.h>
#include "header/stdlib/string.h"
#include "user/commands.h"
#include "user/syscall.h"

/*
 * find NAME: depth first search of the whole filesystem from the root, printing the absolute path
 * of every file and folder called NAME. "." and ".." are skipped, so the walk is a tree walk and
 * cannot loop (Book 3.4, "EXT2, Graph, Tree").
 */

struct FindState {
    struct ShellIO *io;
    const char     *name;
    uint32_t        name_len;
    char            path[SHELL_PATH_MAX];  // path of the folder being visited, grows per level
    bool            skipped;               // a folder could not be read or its path was too long
};

static void visit(struct FindState *state, uint32_t dir, uint32_t len, uint32_t depth) {
    const uint8_t *table = NULL;
    uint32_t offset = 0;

    for (;;) {
        struct FsuEntry entry;
        uint32_t child_len;

        // The listing buffer is shared, so read it again only after visiting a child folder.
        if (table == NULL && (table = fsu_list(dir)) == NULL) {
            state->skipped = true;
            return;
        }
        if (!fsu_next_entry(table, &offset, &entry)) {
            return;
        }

        child_len = len + (len > 1 ? 1 : 0) + entry.name_len;
        if (child_len >= SHELL_PATH_MAX) {
            state->skipped = true;
            continue;
        }
        if (len > 1) {
            state->path[len] = '/';
        }
        memcpy(state->path + child_len - entry.name_len, entry.name, entry.name_len);
        state->path[child_len] = '\0';

        if (entry.name_len == state->name_len &&
            memcmp(entry.name, state->name, entry.name_len) == 0) {
            shell_out(state->io, state->path, child_len);
            shell_out_str(state->io, entry.type == EXT2_FT_DIR ? "/\n" : "\n");
        }
        if (entry.type == EXT2_FT_DIR) {
            if (depth >= FSU_DEPTH_MAX * 2) {
                state->skipped = true;
            } else {
                visit(state, entry.inode, child_len, depth + 1);
            }
            table = NULL;
        }
    }
}

int cmd_find(int argc, char **argv, struct ShellIO *io) {
    static struct FindState state;

    if (argc != 2) {
        shell_out_str(io, "usage: find NAME\n");
        return 1;
    }
    state.io = io;
    state.name = argv[1];
    state.name_len = shell_strlen(argv[1]);
    state.skipped = false;
    state.path[0] = '/';
    state.path[1] = '\0';

    visit(&state, SHELL_ROOT_INODE, 1, 0);
    if (state.skipped) {
        shell_out_str(io, "find: some folders could not be searched\n");
        return 1;
    }
    return 0;
}
