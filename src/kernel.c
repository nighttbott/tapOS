#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/cpu/idt.h"
#include "header/cpu/portio.h"
#include "header/driver/keyboard.h"
#include "header/text/framebuffer.h"
#include "header/kernel-entrypoint.h"
#include "header/driver/disk.h"
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"


void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    initialize_tss();
    paging_initialize();
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    initialize_filesystem_ext2();
   
    uint32_t row = 0;
    uint32_t col = 0;
    keyboard_state_activate();
    while (true) {
        char c;
        get_keyboard_buffer(&c);
        if (c == '\0')
            continue;

        if (c == KEY_ARROW_UP) {
            if(row == 0) {
                continue;
            }
            --row;
        } else if (c == KEY_ARROW_DOWN) {
            if(row >= FRAMEBUFFER_HEIGHT - 1U) {
                continue;
            }
            ++row;
        } else if (c == KEY_ARROW_LEFT) {
            if(col == 0) {
                continue;
            }
            --col;
        } else if (c == KEY_ARROW_RIGHT) {
            if(col >= FRAMEBUFFER_WIDTH - 1U) {
                continue;
            }
            ++col;
        } else if (c == '\n') {
            col = 0;
            ++row;
        } else if (c == '\b') {
            if(row == 0 && col == 0)
                continue;
            else if (col > 0) {
                --col;
            }
            else if (col == 0) {
                --row;
                col = FRAMEBUFFER_WIDTH - 1U;
            }
            memsetw((void *) (FRAMEBUFFER_MEMORY_OFFSET + (row * FRAMEBUFFER_WIDTH + col) * 2U), ((0x07 << 8) | 0x00), 1);
        } else if (c >= 32 && c <= 126) {
            framebuffer_write(row, col, c, 0x0F, 0x00);
            ++col;
        }

        if(col >= FRAMEBUFFER_WIDTH){
            col = 0;
            ++row;
        }
        if(row >= FRAMEBUFFER_HEIGHT){
            framebuffer_scroll_up();
            row = FRAMEBUFFER_HEIGHT - 1U;
        }

        framebuffer_set_cursor(row, col);
    }
}
