#include "header/driver/keyboard.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/portio.h"
#include "header/stdlib/string.h"

const char keyboard_scancode_1_to_ascii_map[256] = {
      0, 0x1B, '1', '2', '3', '4', '5', '6',  '7', '8', '9',  '0',  '-', '=', '\b', '\t',
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',  'o', 'p', '[',  ']', '\n',   0,  'a',  's',
    'd',  'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0, '\\',  'z', 'x',  'c',  'v',
    'b',  'n', 'm', ',', '.', '/',   0, '*',    0, ' ',   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0, '-',    0,    0,   0,  '+',    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,

      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
};

const char keyboard_scancode_1_to_ascii_uppercase_map[256] = {
      0, 0x1B, '1', '2', '3', '4', '5', '6',  '7', '8', '9',  '0',  '-', '=', '\b', '\t',
    'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',  'O', 'P', '[',  ']', '\n',   0,  'A',  'S',
    'D',  'F', 'G', 'H', 'J', 'K', 'L', ';', '\'', '`',   0, '\\',  'Z', 'X',  'C',  'V',
    'B',  'N', 'M', ',', '.', '/',   0, '*',    0, ' ',   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0, '-',    0,    0,   0,  '+',    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,

      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
};

const char keyboard_scancode_1_to_ascii_shift_map[256] = {
      0, 0x1B, '!', '@', '#', '$', '%', '^',  '&', '*', '(',  ')',  '_', '+', '\b', '\t',
    'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',  'O', 'P', '{',  '}', '\n',   0,  'A',  'S',
    'D',  'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',   0, '|',  'Z', 'X',  'C',  'V',
    'B',  'N', 'M', '<', '>', '?',   0, '*',    0, ' ',   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0, '-',    0,    0,   0,  '+',    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,

      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
};

static struct KeyboardDriverState keyboard_state = {
    .read_extended_mode = false,
    .keyboard_input_on = false,
    .keyboard_buffer = 0,
    .left_shift_pressed = false,
    .right_shift_pressed = false,
    .caps_lock_on = false,
    .caps_lock_pressed = false,
};

void keyboard_state_activate(void) {
    keyboard_state.read_extended_mode = false;
    keyboard_state.keyboard_buffer = 0;
    keyboard_state.keyboard_input_on = true;
}

void keyboard_state_deactivate(void) {
    keyboard_state.read_extended_mode = false;
    keyboard_state.keyboard_buffer = 0;
    keyboard_state.keyboard_input_on = false;
}

void get_keyboard_buffer(char *buf) {
    if (buf == 0) {
        return;
    }
    uint32_t interrupt_flags;

    __asm__ volatile(
        "pushfl\n\t"
        "popl %0\n\t"
        "cli"
        : "=r"(interrupt_flags)
        :
        : "memory"
    );

    *buf = keyboard_state.keyboard_buffer;
    keyboard_state.keyboard_buffer = 0;

    __asm__ volatile(
        "pushl %0\n\t"
        "popfl"
        :
        : "r"(interrupt_flags)
        : "memory", "cc"
    );
}

void keyboard_isr(void) {
    uint8_t scancode = in(KEYBOARD_DATA_PORT);
    pic_ack(IRQ_KEYBOARD);

    if (scancode == EXTENDED_SCANCODE_BYTE) {
        keyboard_state.read_extended_mode = true;
        return;
    }
    if (keyboard_state.read_extended_mode) {
        keyboard_state.read_extended_mode = false;

        if ((scancode & 0x80U) != 0)
            return;

        if (!keyboard_state.keyboard_input_on)
            return;

        switch (scancode) {
            case EXT_SCANCODE_UP:
                keyboard_state.keyboard_buffer = KEY_ARROW_UP;
                break;

            case EXT_SCANCODE_DOWN:
                keyboard_state.keyboard_buffer = KEY_ARROW_DOWN;
                break;

            case EXT_SCANCODE_LEFT:
               keyboard_state.keyboard_buffer = KEY_ARROW_LEFT;
                break;

            case EXT_SCANCODE_RIGHT:
                keyboard_state.keyboard_buffer = KEY_ARROW_RIGHT;
                break;

            default:
                break;
        }

        return;
    }
    if (scancode == SCANCODE_LEFT_SHIFT_PRESSED) {
        keyboard_state.left_shift_pressed = true;
        return;        
    }
    if (scancode == SCANCODE_LEFT_SHIFT_RELEASED) {
        keyboard_state.left_shift_pressed = false;
        return;        
    }
    if (scancode == SCANCODE_RIGHT_SHIFT_PRESSED) {
        keyboard_state.right_shift_pressed = true;
        return;        
    }
    if (scancode == SCANCODE_RIGHT_SHIFT_RELEASED) {
        keyboard_state.right_shift_pressed = false;
        return;        
    }
    if (scancode == SCANCODE_CAPSLOCK_PRESSED) {
        if(!keyboard_state.caps_lock_pressed){
            keyboard_state.caps_lock_on = !keyboard_state.caps_lock_on;
            keyboard_state.caps_lock_pressed = true;
        }
        return;
    }
    if (scancode == SCANCODE_CAPSLOCK_RELEASED) {
        keyboard_state.caps_lock_pressed = false;
        return;
    }
    if (!keyboard_state.keyboard_input_on) {
        return;
    }

    if ((scancode & 0x80U) != 0) {
        return;
    }
    bool shift_pressed = keyboard_state.left_shift_pressed || keyboard_state.right_shift_pressed;
    bool uppercase = shift_pressed ^ keyboard_state.caps_lock_on;

    char character = keyboard_scancode_1_to_ascii_map[scancode];
    if (character >= 'a' && character <= 'z'){
        if (uppercase) {
            character = keyboard_scancode_1_to_ascii_uppercase_map[scancode];
        }
    }
    else if(shift_pressed) {
        character = keyboard_scancode_1_to_ascii_shift_map[scancode];
    }
    if (character != '\0') {
        keyboard_state.keyboard_buffer = character;
    }
}
