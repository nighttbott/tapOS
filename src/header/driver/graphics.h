#ifndef _GRAPHICS_H
#define _GRAPHICS_H

#include <stdbool.h>
#include <stdint.h>

/* 640x400, 32 bits per pixel: an 8x16 font gives the same 80x25 cells as text mode 3h. */
#define GRAPHICS_WIDTH       640u
#define GRAPHICS_HEIGHT      400u
#define GRAPHICS_GLYPH_WIDTH  8u
#define GRAPHICS_GLYPH_HEIGHT 16u

/* Virtual address where the linear framebuffer is mapped (page directory entry 0x3C0). */
#define GRAPHICS_VIRTUAL_BASE 0xF0000000u

/**
 * Switches the display to a colour graphics mode through the Bochs/QEMU VBE registers,
 * finds the linear framebuffer on the PCI bus and maps it into kernel space.
 * Call after paging_initialize(), before any process is created.
 *
 * @return true when the mode is active; false leaves the BIOS text mode untouched.
 */
bool graphics_initialize(void);

/**
 * Draws one 8x16 character cell. Colours are 4-bit BIOS attribute values (0..15).
 */
void graphics_draw_cell(uint8_t row, uint8_t col, uint8_t character, uint8_t fg, uint8_t bg);

#endif
