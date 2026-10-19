#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/text/framebuffer.h"
#include "header/stdlib/string.h"
#include "header/cpu/portio.h"

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    uint16_t position;

    if (r >= FRAMEBUFFER_HEIGHT || c >= FRAMEBUFFER_WIDTH) {
        return;
    }

    position = (uint16_t) r * FRAMEBUFFER_WIDTH + c;
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
    FRAMEBUFFER_MEMORY_OFFSET[position * 2U] = (uint8_t) c;
    FRAMEBUFFER_MEMORY_OFFSET[position * 2U + 1U] =
        (uint8_t) ((bg << 4) | (fg & 0x0F));
}

void framebuffer_clear(void) {
    uint16_t blank = (0x07 << 8) | 0x00;
    size_t total_pixel = FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT;

    memsetw((void *) FRAMEBUFFER_MEMORY_OFFSET, blank, total_pixel);
    framebuffer_set_cursor(0, 0);
}

void framebuffer_scroll_up(void) {
    uint16_t blank = (0x07 << 8) | 0x00;
    uint32_t i, j;
    for (i = 1; i < FRAMEBUFFER_HEIGHT; ++i){
        for (j = 0; j < FRAMEBUFFER_WIDTH; ++j){
            uint32_t position = (i * FRAMEBUFFER_WIDTH + j);
            framebuffer_write(i - 1, j, FRAMEBUFFER_MEMORY_OFFSET[position * 2U], FRAMEBUFFER_MEMORY_OFFSET[position * 2U + 1U] & 0x0F, FRAMEBUFFER_MEMORY_OFFSET[position * 2U + 1U] >> 4);
        }
    }
    memsetw((void *) (FRAMEBUFFER_MEMORY_OFFSET + (FRAMEBUFFER_HEIGHT - 1U) * FRAMEBUFFER_WIDTH * 2U), blank, FRAMEBUFFER_WIDTH);
}