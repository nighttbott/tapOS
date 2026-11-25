#include "header/badapple/player.h"
#include "header/user/syscall.h"

/* Plays /apple.dat (made by tools/badapple_encode.py) with SYSCALL_BLIT_BITS. Run it with `run badapple`. */

#define ROOT_INODE 1u
#define MAX_BYTES  (128u * 96u / 8u)

static uint8_t  frame_bits[MAX_BYTES];
static uint8_t  buffer[2048];

static uint32_t now(void) { return sys_get_ticks() * 10u; }

static int32_t read_at(uint32_t offset, void *out, uint32_t size) {
    static char name[] = "apple.dat";
    struct EXT2DriverRequest request = {
        .buf = out,
        .name = name,
        .name_len = sizeof(name) - 1u,
        .parent_inode = ROOT_INODE,
        .buffer_size = size,
    };
    return sys_read_range(&request, offset);
}

static int32_t show(const uint8_t *bits, uint32_t w, uint32_t h) { return sys_blit_bits(bits, w, h); }

/* The keyboard driver ignores Ctrl, so any key stops the video. */
static int stop(void) {
    char c;
    return sys_read_char(&c) > 0;
}

static void idle(void) {}

static void print_number(uint32_t n) {
    char digits[11];
    uint32_t len = 0;
    do {
        digits[len++] = (char)('0' + n % 10u);
        n /= 10u;
    } while (n);
    while (len) {
        sys_write_text(&digits[--len], 1);
    }
}

static void print(const char *s) {
    uint32_t len = 0;
    while (s[len]) len++;
    sys_write_text(s, len);
}

int main(void) {
    static const struct BaPlatform platform = {now, read_at, show, stop, idle};
    struct BaStats stats;
    int32_t code = ba_play(&platform, frame_bits, MAX_BYTES, buffer, sizeof(buffer), &stats);

    sys_redraw();
    if (code != 0) {
        print("badapple: cannot play /apple.dat\n");
        return 1;
    }
    print("badapple: ");
    print_number(stats.shown);
    print(" of ");
    print_number(stats.frames);
    print(" frames in ");
    print_number(stats.elapsed_ms);
    print(" ms\n");
    return 0;
}
