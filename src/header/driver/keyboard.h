#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

#define KEYBOARD_DATA_PORT     0x60
#define EXTENDED_SCANCODE_BYTE 0xE0

extern const char keyboard_scancode_1_to_ascii_map[256];

struct KeyboardDriverState {
    bool read_extended_mode;
    bool keyboard_input_on;
    char keyboard_buffer;
} __attribute__((packed));

void keyboard_state_activate(void);
void keyboard_state_deactivate(void);
void get_keyboard_buffer(char *buf);
void keyboard_handle_scancode(uint8_t scancode);
void keyboard_isr(void);

#endif
