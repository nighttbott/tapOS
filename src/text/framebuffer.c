#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
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
    uint8_t row;
    uint8_t col;

    for (row = 0; row < FRAMEBUFFER_HEIGHT; row++) {
        for (col = 0; col < FRAMEBUFFER_WIDTH; col++) {
            framebuffer_write(row, col, ' ', 0x07, 0x00);
        }
    }
    framebuffer_set_cursor(0, 0);
}
