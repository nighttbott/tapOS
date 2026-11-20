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

#define USER_IMAGE_SIZE  0x40000u
#define USER_STACK_PAGES 4u

__attribute__((noreturn))
static void halt_with_message(const char *text) {
    for (uint8_t col = 0; text[col] != '\0'; col++)
        framebuffer_write(0, col, text[col], 0x0F, 0x00);
    while (true)
        __asm__ volatile("hlt");
}

static void load_shell(void) {
    struct PageDirectory *page_dir = paging_get_current_page_directory_addr();
    uint32_t stack_bottom = KERNEL_VIRTUAL_ADDRESS_BASE - USER_STACK_PAGES * PAGE_FRAME_SIZE;

    for (uint32_t addr = 0; addr < USER_IMAGE_SIZE; addr += PAGE_FRAME_SIZE)
        if (!paging_allocate_user_page_frame(page_dir, (void *) addr))
            halt_with_message("tapOS: out of memory for the shell");
    for (uint32_t addr = stack_bottom; addr < KERNEL_VIRTUAL_ADDRESS_BASE; addr += PAGE_FRAME_SIZE)
        if (!paging_allocate_user_page_frame(page_dir, (void *) addr))
            halt_with_message("tapOS: out of memory for the shell stack");

    struct EXT2DriverRequest request = {
        .buf          = (void *) 0,
        .name         = "shell",
        .name_len     = 5,
        .parent_inode = 1,
        .buffer_size  = 0,
        .is_directory = false,
    };
    int8_t status = read(request);
    if (status == 0)
        halt_with_message("tapOS: /shell is empty, run: make insert-shell");
    if (status != 2)
        halt_with_message("tapOS: cannot open /shell, run: make insert-shell");

    request.buffer_size = USER_IMAGE_SIZE;
    status = read(request);
    if (status == 2)
        halt_with_message("tapOS: /shell is larger than the mapped user image");
    if (status != 0)
        halt_with_message("tapOS: cannot load /shell");
}

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    paging_initialize();
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    initialize_filesystem_ext2();
    initialize_tss();
    keyboard_state_activate();

    load_shell();
    enter_user_mode((void *) 0, (void *) KERNEL_VIRTUAL_ADDRESS_BASE);
}
