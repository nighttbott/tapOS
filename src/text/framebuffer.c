#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/text/framebuffer.h"
#include "header/cpu/portio.h"
#ifndef FRAMEBUFFER_TEST
#include "header/driver/graphics.h"

/* tests/qemu reads the screen from this symbol when the BIOS text memory is not displayed. */
volatile uint8_t framebuffer_text_shadow[FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT * 2U];
volatile uint8_t *framebuffer_cells = (volatile uint8_t *) 0xC00B8000;
static bool graphics_active;
static uint8_t cursor_row;
static uint8_t cursor_col;

static void draw_cell(uint8_t row, uint8_t col) {
    if (row >= FRAMEBUFFER_HEIGHT || col >= FRAMEBUFFER_WIDTH)
        return;   /* the hidden cursor is parked outside the grid */
    uint32_t index = ((uint32_t) row * FRAMEBUFFER_WIDTH + col) * 2U;
    uint8_t character = framebuffer_cells[index];
    if (character == 0)   /* text mode draws NUL as a blank, the font's glyph 0 is a symbol */
        character = ' ';
    uint8_t attribute = framebuffer_cells[index + 1U];
    uint8_t fg = attribute & 0x0F;
    uint8_t bg = attribute >> 4;

    /* The cursor is the cell with foreground and background swapped. */
    if (row == cursor_row && col == cursor_col) {
        uint8_t swap = fg;
        fg = bg;
        bg = swap;
    }
    graphics_draw_cell(row, col, character, fg, bg);
}

bool framebuffer_enable_graphics(void) {
    if (!graphics_initialize())
        return false;
    framebuffer_cells = framebuffer_text_shadow;
    graphics_active = true;
    return true;
}
#endif

void framebuffer_redraw(void) {
#ifndef FRAMEBUFFER_TEST
    if (graphics_active) {
        for (uint8_t row = 0; row < FRAMEBUFFER_HEIGHT; row++)
            for (uint8_t col = 0; col < FRAMEBUFFER_WIDTH; col++)
                draw_cell(row, col);
    }
#endif
}

void framebuffer_hide_cursor(void) {
#ifndef FRAMEBUFFER_TEST
    if (graphics_active) {
        uint8_t old_row = cursor_row;
        uint8_t old_col = cursor_col;
        cursor_row = FRAMEBUFFER_HEIGHT;   /* no cell is the cursor cell */
        cursor_col = FRAMEBUFFER_WIDTH;
        draw_cell(old_row, old_col);
        return;
    }
#endif
    out(CURSOR_PORT_CMD, 0x0A);
    out(CURSOR_PORT_DATA, 0x20);   /* cursor disable bit of the VGA text cursor */
}

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    uint16_t position;

    if (r >= FRAMEBUFFER_HEIGHT || c >= FRAMEBUFFER_WIDTH) {
        return;
    }

#ifndef FRAMEBUFFER_TEST
    if (graphics_active) {
        uint8_t old_row = cursor_row;
        uint8_t old_col = cursor_col;
        cursor_row = r;
        cursor_col = c;
        draw_cell(old_row, old_col);
        draw_cell(r, c);
        return;
    }
#endif

    position = (uint16_t) r * FRAMEBUFFER_WIDTH + c;
    out(CURSOR_PORT_CMD, 0x0A);
    out(CURSOR_PORT_DATA, in(CURSOR_PORT_DATA) & (uint8_t) ~0x20u);   /* show it again if it was hidden */
    out(CURSOR_PORT_CMD, 0x0F);
    out(CURSOR_PORT_DATA, (uint8_t) position);
    out(CURSOR_PORT_CMD, 0x0E);
    out(CURSOR_PORT_DATA, (uint8_t) (position >> 8));
}

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    uint16_t position;

    if (row >= FRAMEBUFFER_HEIGHT || col >= FRAMEBUFFER_WIDTH) {
        return;
    }

    position = (uint16_t) row * FRAMEBUFFER_WIDTH + col;
    uint8_t attribute = (uint8_t) ((bg << 4) | (fg & 0x0F));
#ifndef FRAMEBUFFER_TEST
    /* Painting 128 pixels is the slow part, so unchanged cells (blank rows while scrolling) are skipped. */
    if (graphics_active && FRAMEBUFFER_MEMORY_OFFSET[position * 2U] == (uint8_t) c &&
        FRAMEBUFFER_MEMORY_OFFSET[position * 2U + 1U] == attribute) {
        return;
    }
#endif
    FRAMEBUFFER_MEMORY_OFFSET[position * 2U] = (uint8_t) c;
    FRAMEBUFFER_MEMORY_OFFSET[position * 2U + 1U] = attribute;
#ifndef FRAMEBUFFER_TEST
    if (graphics_active)
        draw_cell(row, col);
#endif
}

void framebuffer_clear(void) {
    uint16_t blank = (0x07 << 8) | 0x00;
    size_t total_pixel = FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT;

    memsetw((void *) FRAMEBUFFER_MEMORY_OFFSET, blank, total_pixel);
#ifndef FRAMEBUFFER_TEST
    if (graphics_active) {
        for (uint8_t row = 0; row < FRAMEBUFFER_HEIGHT; row++)
            for (uint8_t col = 0; col < FRAMEBUFFER_WIDTH; col++)
                draw_cell(row, col);
    }
#endif
    framebuffer_set_cursor(0, 0);
}

void framebuffer_scroll_up(void) {
    uint32_t i, j;
    for (i = 1; i < FRAMEBUFFER_HEIGHT; ++i){
        for (j = 0; j < FRAMEBUFFER_WIDTH; ++j){
            uint32_t position = (i * FRAMEBUFFER_WIDTH + j);
            framebuffer_write(i - 1, j, FRAMEBUFFER_MEMORY_OFFSET[position * 2U], FRAMEBUFFER_MEMORY_OFFSET[position * 2U + 1U] & 0x0F, FRAMEBUFFER_MEMORY_OFFSET[position * 2U + 1U] >> 4);
        }
    }
    for (j = 0; j < FRAMEBUFFER_WIDTH; ++j)
        framebuffer_write(FRAMEBUFFER_HEIGHT - 1U, j, 0, 0x07, 0x00);
}
