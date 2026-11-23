#include <stdbool.h>
#include <stdint.h>
#include "header/cpu/portio.h"
#include "header/text/bootanim.h"
#include "header/text/framebuffer.h"

/*
 * Boot animation in the style of the Windows XP start-up screen: the logo is revealed letter by letter,
 * a row of blue blocks slides along a frame while the system starts, a welcome line flashes and
 * the screen opens like a curtain onto the shell. It only uses framebuffer_write(), so it looks the
 * same in text mode and in the pixel mode. Time comes from the PIT counter, read by polling,
 * because interrupts are still off while the kernel boots.
 */

#define SCREEN_BG        0x00
#define COLOR_LOGO       0x0F    /* bright white */
#define COLOR_LOGO_ALT   0x0E    /* yellow, for "OS" (the orange of the XP logo) */
#define COLOR_TEXT       0x07
#define COLOR_DIM        0x08
#define COLOR_FRAME      0x07
#define COLOR_HEAD       0x0B    /* the three blocks: bright cyan head, blue body, dark tail */
#define COLOR_BODY       0x09
#define COLOR_TAIL       0x01

#define BLOCK_CHAR       ((char) 0xDB)
#define LETTERS          5       /* t a p O S */
#define LETTER_ROWS      7
#define LETTER_COLS      5
#define PIXEL_CELLS      2       /* a cell is 8x16 pixels, so two cells side by side make a square */
#define LETTER_GAP       2       /* in cells */
#define LOGO_ROWS        LETTER_ROWS
#define LOGO_WIDTH       (LETTERS * LETTER_COLS * PIXEL_CELLS + (LETTERS - 1) * LETTER_GAP)
#define LETTERS_PLAIN    3       /* "tap" in white, "OS" in the other colour */
#define LOGO_TOP         3
#define BAR_WIDTH        28
#define BAR_TOP          14
#define BLOCK_WIDTH      2
#define BLOCK_GAP        1
#define BLOCKS           3
#define BLOCKS_SPAN      (BLOCKS * BLOCK_WIDTH + (BLOCKS - 1) * BLOCK_GAP)

#define PIT_HZ           1193182u
#define PIT_COMMAND      0x43
#define PIT_CHANNEL0     0x40

/* The logo is drawn as a 5x7 pixel font; every '#' becomes two full-block characters. */
static const char *const letters[LETTERS][LETTER_ROWS] = {
    {"..#..", "..#..", "#####", "..#..", "..#..", "..#..", "...##"},   /* t */
    {".....", ".....", ".###.", "....#", ".####", "#...#", ".####"},   /* a */
    {".....", ".....", "####.", "#...#", "#...#", "####.", "#...."},   /* p */
    {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."},   /* O */
    {".####", "#....", "#....", ".###.", "....#", "....#", "####."},   /* S */
};

static void pit_start(void) {
    out(PIT_COMMAND, 0x34);          /* channel 0, low/high byte, mode 2 (counts down by one) */
    out(PIT_CHANNEL0, 0);
    out(PIT_CHANNEL0, 0);            /* reload 65536 */
}

static uint16_t pit_read(void) {
    out(PIT_COMMAND, 0x00);          /* latch the counter */
    uint16_t low = in(PIT_CHANNEL0);
    return (uint16_t) ((in(PIT_CHANNEL0) << 8) | low);
}

static void wait_ms(uint32_t ms) {
    uint32_t needed = ms * (PIT_HZ / 1000u) + ms * (PIT_HZ % 1000u) / 1000u;   /* 32-bit only: no libgcc */
    uint32_t elapsed = 0;
    uint16_t previous = pit_read();

    while (elapsed < needed) {
        uint16_t now = pit_read();
        elapsed += (uint16_t) (previous - now);   /* the counter runs down and wraps */
        previous = now;
    }
}

static void put_text(uint8_t row, uint8_t col, const char *text, uint8_t color) {
    for (; *text != '\0'; text++, col++)
        framebuffer_write(row, col, *text, color, SCREEN_BG);
}

static uint8_t text_length(const char *text) {
    uint8_t length = 0;

    while (text[length] != '\0')
        length++;
    return length;
}

static void put_centered(uint8_t row, const char *text, uint8_t color) {
    put_text(row, (uint8_t) ((FRAMEBUFFER_WIDTH - text_length(text)) / 2u), text, color);
}

static void clear_row(uint8_t row) {
    for (uint8_t col = 0; col < FRAMEBUFFER_WIDTH; col++)
        framebuffer_write(row, col, ' ', COLOR_TEXT, SCREEN_BG);
}

static void draw_letter(uint8_t letter) {
    uint8_t left = (uint8_t) ((FRAMEBUFFER_WIDTH - LOGO_WIDTH) / 2u +
                              letter * (LETTER_COLS * PIXEL_CELLS + LETTER_GAP));
    uint8_t color = letter < LETTERS_PLAIN ? COLOR_LOGO : COLOR_LOGO_ALT;

    for (uint8_t row = 0; row < LETTER_ROWS; row++) {
        for (uint8_t col = 0; col < LETTER_COLS; col++) {
            if (letters[letter][row][col] != '#')
                continue;
            for (uint8_t cell = 0; cell < PIXEL_CELLS; cell++)
                framebuffer_write((uint8_t) (LOGO_TOP + row), (uint8_t) (left + col * PIXEL_CELLS + cell),
                                  BLOCK_CHAR, color, SCREEN_BG);
        }
    }
}

static void draw_bar_frame(void) {
    uint8_t left = (FRAMEBUFFER_WIDTH - (BAR_WIDTH + 2)) / 2u;

    framebuffer_write(BAR_TOP, left, (char) 0xDA, COLOR_FRAME, SCREEN_BG);
    framebuffer_write(BAR_TOP + 1, left, (char) 0xB3, COLOR_FRAME, SCREEN_BG);
    framebuffer_write(BAR_TOP + 2, left, (char) 0xC0, COLOR_FRAME, SCREEN_BG);
    for (uint8_t i = 1; i <= BAR_WIDTH; i++) {
        framebuffer_write(BAR_TOP, (uint8_t) (left + i), (char) 0xC4, COLOR_FRAME, SCREEN_BG);
        framebuffer_write(BAR_TOP + 2, (uint8_t) (left + i), (char) 0xC4, COLOR_FRAME, SCREEN_BG);
    }
    framebuffer_write(BAR_TOP, (uint8_t) (left + BAR_WIDTH + 1), (char) 0xBF, COLOR_FRAME, SCREEN_BG);
    framebuffer_write(BAR_TOP + 1, (uint8_t) (left + BAR_WIDTH + 1), (char) 0xB3, COLOR_FRAME, SCREEN_BG);
    framebuffer_write(BAR_TOP + 2, (uint8_t) (left + BAR_WIDTH + 1), (char) 0xD9, COLOR_FRAME, SCREEN_BG);
}

/* One frame of the bar: the blocks' head is at `head` and may be partly outside the frame. */
static void draw_bar(int head) {
    uint8_t left = (uint8_t) ((FRAMEBUFFER_WIDTH - (BAR_WIDTH + 2)) / 2u + 1u);

    for (int i = 0; i < BAR_WIDTH; i++)
        framebuffer_write(BAR_TOP + 1, (uint8_t) (left + i), ' ', COLOR_TEXT, SCREEN_BG);
    for (int block = 0; block < BLOCKS; block++) {
        /* block 0 is the head, the others trail behind it */
        int start = head - block * (BLOCK_WIDTH + BLOCK_GAP) - (BLOCK_WIDTH - 1);
        uint8_t color = block == 0 ? COLOR_HEAD : block == 1 ? COLOR_BODY : COLOR_TAIL;

        for (int i = 0; i < BLOCK_WIDTH; i++) {
            int x = start + i;
            if (x >= 0 && x < BAR_WIDTH)
                framebuffer_write(BAR_TOP + 1, (uint8_t) (left + x), BLOCK_CHAR, color, SCREEN_BG);
        }
    }
}

static void reveal_logo(void) {
    for (uint8_t letter = 0; letter < LETTERS; letter++) {
        draw_letter(letter);
        wait_ms(180);
    }
}

static void slide_bar(uint32_t duration_ms) {
    static const char *const dots[] = {"Starting tapOS", "Starting tapOS.", "Starting tapOS..", "Starting tapOS..."};
    uint32_t frames = duration_ms / 55u;

    for (uint32_t frame = 0; frame < frames; frame++) {
        draw_bar((int) (frame % (BAR_WIDTH + BLOCKS_SPAN)));
        if (frame % 5 == 0) {
            clear_row(19);
            put_centered(19, dots[(frame / 5u) % 4u], COLOR_TEXT);
        }
        wait_ms(55);
    }
}

static void welcome(void) {
    for (uint8_t row = BAR_TOP; row <= 19; row++)
        clear_row(row);
    put_centered(BAR_TOP + 1, "Welcome", COLOR_LOGO);
    wait_ms(700);
}

/* The screen opens from the middle towards both sides, like a curtain. */
static void open_curtain(void) {
    for (uint8_t step = 0; step < FRAMEBUFFER_WIDTH / 2u; step++) {
        uint8_t left = (uint8_t) (FRAMEBUFFER_WIDTH / 2u - 1u - step);
        uint8_t right = (uint8_t) (FRAMEBUFFER_WIDTH / 2u + step);

        for (uint8_t row = 0; row < FRAMEBUFFER_HEIGHT; row++) {
            framebuffer_write(row, left, ' ', COLOR_TEXT, SCREEN_BG);
            framebuffer_write(row, right, ' ', COLOR_TEXT, SCREEN_BG);
        }
        wait_ms(12);
    }
}

void boot_animation_play(void) {
    pit_start();
    framebuffer_clear();
    framebuffer_hide_cursor();

    reveal_logo();
    put_centered(LOGO_TOP + LOGO_ROWS + 1, "IF2130 Operating Systems", COLOR_DIM);
    draw_bar_frame();
    slide_bar(2200);
    welcome();
    open_curtain();
    framebuffer_clear();
}
