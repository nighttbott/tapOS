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
#define FRAMEBUFFER_MEMORY_OFFSET ((volatile uint8_t*) 0xB8000)
#endif
#define CURSOR_PORT_CMD    0x03D4
#define CURSOR_PORT_DATA   0x03D5

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg);
void framebuffer_set_cursor(uint8_t row, uint8_t col);
void framebuffer_clear(void);

#endif
