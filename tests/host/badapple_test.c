#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "header/badapple/codec.h"
#include "header/badapple/player.h"

/* usage: test_badapple FILE.bin FILE.gray  (the .gray file has the frames the .bin was made from) */

static uint8_t *bin, *gray;
static uint32_t bin_size, gray_size, W, H;
static uint32_t clock_ms, show_cost, stop_after, shown_calls, mismatches, cut_at;

static uint8_t *slurp(const char *path, uint32_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    *size = (uint32_t)ftell(f);
    rewind(f);
    uint8_t *data = malloc(*size);
    if (fread(data, 1, *size, f) != *size) exit(2);
    fclose(f);
    return data;
}

static uint32_t now_ms(void) { return clock_ms; }
static void wait_tick(void) { clock_ms += 10; }
static int stop(void) { return stop_after && shown_calls >= stop_after; }
static int32_t rd(uint32_t off, void *buf, uint32_t n) {
    uint32_t size = cut_at ? cut_at : bin_size;
    if (off >= size) return 0;
    if (n > size - off) n = size - off;
    memcpy(buf, bin + off, n);
    return (int32_t)n;
}
static int32_t show(const uint8_t *bits, uint32_t w, uint32_t h) {
    /* a drawn frame equals the source frame of its time slot (50 ms), or of the slot before when drawn late */
    uint32_t slot = (clock_ms - 1000) / 50, frames = gray_size / (w * h), ok = 0;
    for (uint32_t s = slot ? slot - 1 : 0; s <= slot && s < frames && !ok; s++) {
        ok = 1;
        for (uint32_t i = 0; i < w * h; i++) {
            if (((bits[i / 8] >> (i % 8)) & 1u) != (gray[(size_t)s * w * h + i] >= 128)) { ok = 0; break; }
        }
    }
    if (!ok) mismatches++;
    shown_calls++;
    clock_ms += show_cost;
    return 0;
}

static int failures;
#define CHECK(cond, name) do { if (cond) printf("PASS %s\n", name); else { printf("FAIL %s\n", name); failures++; } } while (0)

static int32_t play(struct BaStats *st, uint32_t cost, uint32_t stop_at, uint32_t cut) {
    static uint8_t px[128 * 96 / 8];
    static uint8_t buf[4096];
    struct BaPlatform p = {now_ms, rd, show, stop, wait_tick};
    clock_ms = 1000; show_cost = cost; stop_after = stop_at; shown_calls = 0; mismatches = 0; cut_at = cut;
    return ba_play(&p, px, sizeof(px), buf, sizeof(buf), st);
}

int main(int argc, char **argv) {
    struct BaHeader h;
    struct BaStats st;
    if (argc != 3) return 2;
    bin = slurp(argv[1], &bin_size);
    gray = slurp(argv[2], &gray_size);
    CHECK(ba_parse_header(bin, bin_size, &h) == 0, "H1 header parses");
    W = h.width; H = h.height;
    CHECK(gray_size == h.frames * W * H, "H2 frame count matches the source");
    CHECK(ba_parse_header((const uint8_t *)"XXXX000000000000", 16, &h) == -1, "H3 bad magic rejected");

    CHECK(play(&st, 5, 0, 0) == 0 && st.frames == st.shown && st.dropped == 0, "P1 plays every frame when the machine keeps up");
    CHECK(mismatches == 0, "P2 every drawn frame equals its source frame");
    CHECK(st.elapsed_ms >= st.frames * 50 - 60 && st.elapsed_ms <= st.frames * 50 + 60, "P3 runs at 20 fps");

    CHECK(play(&st, 80, 0, 0) == 0 && st.dropped > 0 && st.shown + st.dropped == st.frames, "P4 drops frames when drawing is slow");
    CHECK(mismatches == 0 && st.elapsed_ms <= st.frames * 50 + 200, "P5 slow drawing stays in sync with the clock");

    CHECK(play(&st, 5, 7, 0) == 0 && st.stopped && st.shown == 7, "P6 stop key ends playback at once");
    CHECK(play(&st, 5, 0, bin_size / 2) == BA_ERR_DATA, "P7 a cut off file is reported");
    CHECK(play(&st, 5, 0, 8) == BA_ERR_FORMAT, "P8 a file without a header is reported");

    uint8_t px2[1] = {0};
    uint8_t one[] = {2, 3};   /* 2 black then 3 white into 4 pixels: overflow */
    CHECK(ba_decode_frame(one, 2, px2, 4) == -1, "D1 runs longer than the frame are rejected");
    uint8_t exact[] = {0, 4};   /* empty black run, then 4 white */
    CHECK(ba_decode_frame(exact, 2, px2, 4) == 0 && px2[0] == 0x0F, "D2 a frame may start with white");
    uint8_t open[] = {0x80};
    CHECK(ba_decode_frame(open, 1, px2, 4) == -1, "D3 a cut off run length is rejected");

    printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures != 0;
}
