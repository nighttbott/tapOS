#include "fs_test.h"

static char line[200];
static uint32_t line_len;

static void line_reset(void) {
    line_len = 0;
    line[0] = '\0';
}

static void line_add(const char *text) {
    while (*text != '\0' && line_len < sizeof(line) - 2) {
        line[line_len++] = *text++;
    line[line_len] = '\0';
    }
}

static void line_add_int(int32_t value) {
    char digit[12];
    uint32_t n = 0;
    uint32_t magnitude = value < 0 ? (uint32_t) (-(value + 1)) + 1u : (uint32_t) value;

    if (value < 0)
        line_add("-");
    do {
        digit[n++] = (char) ('0' + magnitude % 10u);
        magnitude /= 10u;
    } while (magnitude > 0);
    while (n > 0) {
        char one[2] = {digit[--n], '\0'};
        line_add(one);
    }
}

static void line_print(struct FsTestContext *ctx) {
    line_add("\n");
    ctx->print(line);
}

void fs_test_init(struct FsTestContext *ctx, fs_test_printfn print) {
    ctx->print = print;
    ctx->passed = 0;
    ctx->failed = 0;
}

void fs_test_check(struct FsTestContext *ctx, const char *id, const char *what, bool ok, int32_t expected, int32_t actual) {
    line_reset();
    line_add(ok ? "PASS " : "FAIL ");
    line_add(id);
    line_add(" ");
    line_add(what);
    if (!ok) {
        line_add(" | expected = ");
        line_add_int(expected);
        line_add(" actual = ");
        line_add_int(actual);
    }
    line_print(ctx);

    if (ok)
        ctx->passed++;
    else
        ctx->failed++;
}

void fs_test_info(struct FsTestContext *ctx, const char *text) {
    line_reset();
    line_add("---- ");
    line_add(text);
    line_print(ctx);
}

void fs_test_sum(struct FsTestContext *ctx, const char *suite) {
    uint32_t total = ctx->passed + ctx->failed;

    line_reset();
    line_add("RESULT ");
    line_add(suite);
    line_add(": ");
    line_add_int((int32_t) ctx->passed);
    line_add("/");
    line_add_int((int32_t) total);
    line_add(" PASS, ");
    line_add_int((int32_t) ctx->failed);
    line_add(" FAIL");
    line_print(ctx);
}

uint8_t fs_test_pattern(uint32_t i, uint32_t size) {
    return (uint32_t) ((i * 13u + size) & 0xFFu);
}

int32_t fs_test_first_mismatch(const uint8_t *buf, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        if (buf[i] != fs_test_pattern(i, size))
            return (int32_t) i;
    }
    return -1;
}