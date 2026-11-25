#include "header/badapple/player.h"
#include "header/user/syscall.h"

/*
 * Plays /apple.dat (made by tools/badapple_encode.py) with BLIT.
 * Needs the syscalls from the DOOM branch: sys_get_ms, sys_blit, sys_read_range, sys_yield, sys_redraw,
 * and a program format larger than the 28 KiB ELF limit (the frame buffer alone is 48 KiB).
 * Assumed prototypes, fix here if they differ:
 *   uint32_t sys_get_ms(void);   int32_t sys_blit(const uint32_t *pixels, uint32_t w, uint32_t h);
 *   int32_t sys_read_range(struct EXT2DriverRequest *request, uint32_t offset);
 *   void sys_yield(void);        void sys_redraw(void);
 */

#define ROOT_INODE 1u
#define MAX_PIXELS (128u * 96u)

static uint32_t pixels[MAX_PIXELS];
static uint8_t  buffer[4096];

static uint32_t now(void) { return sys_get_ms(); }

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

static int32_t show(const uint32_t *px, uint32_t w, uint32_t h) { return sys_blit(px, w, h); }

/* The keyboard driver ignores Ctrl, so any key stops the video. */
static int stop(void) {
    char c;
    return sys_read_char(&c) > 0;
}

static void yield(void) { sys_yield(); }

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
    static const struct BaPlatform platform = {now, read_at, show, stop, yield};
    struct BaStats stats;
    int32_t code = ba_play(&platform, pixels, MAX_PIXELS, buffer, sizeof(buffer), &stats);

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
