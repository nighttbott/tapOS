/* The file system name index (src/filesystem/index.c) on a blank RAM disk, built with a tiny
 * name pool so that "the index cannot hold this disk" can be reached with a few files. */
#include <stdio.h>
#include <string.h>
#include "fs_test.h"
#include "ram_disk.h"
#include "header/filesystem/ext2.h"
#include "header/filesystem/index.h"

static struct FsTestContext ctx;

static void print_line(const char *text) {
    puts(text);
}

static void check(const char *id, const char *what, bool ok) {
    fs_test_check(&ctx, id, what, ok, 1, ok);
}

static int make(uint32_t parent, const char *name, bool folder) {
    struct EXT2DriverRequest request = {
        .buf = NULL, .name = (char *)name, .name_len = (uint8_t)strlen(name),
        .parent_inode = parent, .buffer_size = 0, .is_directory = folder,
    };
    return fs_write(&request);
}

static int remove_entry(uint32_t parent, const char *name) {
    struct EXT2DriverRequest request = {
        .name = (char *)name, .name_len = (uint8_t)strlen(name), .parent_inode = parent,
    };
    return fs_delete(request);
}

static int32_t find(const char *name, char *out, uint32_t size, uint32_t *written) {
    return fs_index_find(name, (uint8_t)strlen(name), out, size, written);
}

int main(void) {
    char out[256];
    uint32_t written;

    if (ram_disk_blank(DISK_SPACE) != 0)
        return 2;
    initialize_filesystem_ext2();
    fs_test_init(&ctx, print_line);

    check("X01", "an empty disk indexes nothing", fs_index_count() == 0);
    check("X01", "nothing found on an empty disk", find("a", out, sizeof(out), &written) == 0 &&
          written == 0);

    check("X02", "make folder", make(1, "docs", true) == 0);
    check("X02", "make file", make(1, "notes", false) == 0);
    check("X02", "found in the root", find("notes", out, sizeof(out), &written) == 1 &&
          written == 7 && memcmp(out, "/notes\n", 7) == 0);
    check("X02", "folders end with a slash", find("docs", out, sizeof(out), &written) == 1 &&
          memcmp(out, "/docs/\n", 7) == 0);

    check("X03", "delete the file", remove_entry(1, "notes") == 0);
    check("X03", "it is gone from the index", find("notes", out, sizeof(out), &written) == 0);
    check("X03", "the count follows", fs_index_count() == 1);

    /* The pool holds 64 name bytes. */
    check("X04", "fill the pool", make(1, "0123456789012345678901234567890123456789", false) == 0 &&
          make(1, "abcdefghijabcdefghij", false) == 0);
    check("X04", "still indexed",
          find("abcdefghijabcdefghij", out, sizeof(out), &written) == 1);
    check("X04", "a name that no longer fits makes the index rebuild and fail",
          make(1, "zzzzzzzzzzzzzzzzzzzz", false) == 0 &&
          find("docs", out, sizeof(out), &written) == FS_INDEX_NO_MATCH_ERROR);
    check("X04", "deleting makes room again",
          remove_entry(1, "0123456789012345678901234567890123456789") == 0 &&
          find("zzzzzzzzzzzzzzzzzzzz", out, sizeof(out), &written) == 1);

    check("X05", "a folder is searched below the root",
          make(2, "leaf", false) == 0 && find("leaf", out, sizeof(out), &written) == 1 &&
          memcmp(out, "/docs/leaf\n", 11) == 0);
    check("X05", "reformatting forgets the index", (create_ext2(), fs_index_count() == 0));

    fs_test_sum(&ctx, "index");
    return ctx.failed == 0 ? 0 : 1;
}
