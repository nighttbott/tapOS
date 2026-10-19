#include <stdio.h>
#include <string.h>
#include "header/filesystem/ext2.h"
#include "fs_test.h"
#include "fs_cases.h"
#include "ram_disk.h"

/*
 * laptop test runner.
 *   test_fs <suite> <image | --blank> [--save out.img]
 * duites: team, boundary, blank, existing
 * exit code 0 only when every check passed (TODO counts as not passed).
 */

static void print_line(const char *text) {
    fputs(text, stdout);
}

struct Suite {
    const char *name;
    void (*run)(struct FsTestContext *ctx);
};

static const struct Suite suites[] = {
    { "team", fs_suite_team_fixture },
    { "boundary", fs_suite_boundary },
    { "blank", fs_suite_blank },
    { "existing", fs_suite_existing },
};

int main(int argc, char **argv) {
    const char *save_path = NULL;
    struct FsTestContext ctx;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <team|boundary|blank|existing> <image|--blank> [--save out.img]\n", argv[0]);
        return 2;
    }
    if (argc == 5 && strcmp(argv[3], "--save") == 0)
        save_path = argv[4];

    int loaded = strcmp(argv[2], "--blank") == 0 ? ram_disk_blank(DISK_SPACE) : ram_disk_load(argv[2]);
    if (loaded != 0) {
        fprintf(stderr, "cannot load disk image %s\n", argv[2]);
        return 2;
    }

    for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        if (strcmp(argv[1], suites[i].name) != 0)
            continue;
        fs_test_init(&ctx, print_line);
        suites[i].run(&ctx);
        fs_test_sum(&ctx, suites[i].name);
        if (save_path != NULL && ram_disk_save(save_path) != 0)
            fprintf(stderr, "cannot save disk image to %s\n", save_path);
        return (ctx.failed == 0 && ctx.not_implemented == 0) ? 0 : 1;
    }
    fprintf(stderr, "unknown suite %s\n", argv[1]);
    return 2;
}