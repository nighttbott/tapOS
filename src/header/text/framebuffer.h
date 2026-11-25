#ifndef _FRAMEBUFFER_H
#define _FRAMEBUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define FRAMEBUFFER_WIDTH  80U
#define FRAMEBUFFER_HEIGHT 25U

#ifdef FRAMEBUFFER_TEST
extern volatile uint8_t framebuffer_test_memory[FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT * 2U];
#define FRAMEBUFFER_MEMORY_OFFSET framebuffer_test_memory
#else
/* Character cells in text-mode layout. They are the BIOS text memory until the graphics mode is on,
 * then a copy in kernel RAM that framebuffer_write() paints onto the pixel framebuffer. */
extern volatile uint8_t *framebuffer_cells;
#define FRAMEBUFFER_MEMORY_OFFSET framebuffer_cells
#endif
#define CURSOR_PORT_CMD    0x03D4
#define CURSOR_PORT_DATA   0x03D5

/**
 * Terminal framebuffer
 * Resolution: 80x25
 * Starting at FRAMEBUFFER_MEMORY_OFFSET,
 * - Even number memory: Character, 8-bit
 * - Odd number memory:  Character color lower 4-bit, Background color upper 4-bit
*/

/**
 * Set framebuffer character and color with corresponding parameter values.
 * More details: https://en.wikipedia.org/wiki/BIOS_color_attributes
 *
 * @param row Vertical location (index start 0)
 * @param col Horizontal location (index start 0)
 * @param c   Character
 * @param fg  Foreground / Character color
 * @param bg  Background color
 */
void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg);

/**
 * Set cursor to specified location. Row and column starts from 0
 * 
 * @param r row
 * @param c column
*/
void framebuffer_set_cursor(uint8_t r, uint8_t c);

/**
 * Set all cell in framebuffer character to 0x00 (empty character)
 * and color to 0x07 (gray character & black background)
 * Extra note: It's allowed to use different color palette for this
 *
 */
void framebuffer_clear(void);

void framebuffer_scroll_up(void);

/**
 * Hides the cursor marker. It comes back the next time framebuffer_set_cursor() is called.
 */
void framebuffer_hide_cursor(void);

/** Paints every text cell again (after a program drew pixels over the console). */
void framebuffer_redraw(void);

#ifndef FRAMEBUFFER_TEST
/**
 * Tries to switch the display from text mode 3h to the 640x400 colour graphics mode.
 * The 80x25 cell grid, colours and cursor keep working; without a supported display
 * adapter the text mode stays as it was. Call before the first framebuffer_clear().
 *
 * @return true when graphics mode is active
 */
bool framebuffer_enable_graphics(void);
#endif

#endif