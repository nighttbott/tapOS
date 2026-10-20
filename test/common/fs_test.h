#ifndef _FS_TEST_H
#define _FS_TEST_H

#include <stdint.h>
#include <stdbool.h>

// prints one finished line (laptop: stdout, QEMU: serial port + screen)
typedef void (*fs_test_printfn)(const char *text);

struct FsTestContext {
    fs_test_printfn print;
    uint32_t passed;
    uint32_t failed;
};

void fs_test_init(struct FsTestContext *ctx, fs_test_printfn print);
void fs_test_check(struct FsTestContext *ctx, const char *id, const char *what, bool ok, int32_t expected, int32_t actual);
void fs_test_info(struct FsTestContext *ctx, const char *text);
void fs_test_sum(struct FsTestContext *ctx, const char *suite);

// expected byte i of a test file of the given size
uint8_t fs_test_pattern(uint32_t i, uint32_t size);
// first offset where buf differs from the pattern, -1 when all bytes match
int32_t fs_test_first_mismatch(const uint8_t *buf, uint32_t size);

#endif
