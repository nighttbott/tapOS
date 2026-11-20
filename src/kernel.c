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
#include "header/memory/process_memory.h"

// Must match the ASSERT in src/user/user-linker.ld: code, data and .bss of the shell.
#define USER_IMAGE_SIZE  0x40000u

__attribute__((noreturn))
static void halt_with_message(const char *text) {
    for (uint8_t col = 0; text[col] != '\0'; col++)
        framebuffer_write(0, col, text[col], 0x0F, 0x00);
    while (true)
        __asm__ volatile("hlt");
}

/*
 * The shell gets its own address space from the M4 loader, the same path every process takes.
 * The kernel page directory keeps only the kernel half. Process table and scheduler (members A
 * and C) still have to record it as process 0.
 */
static struct ProcessMemory shell_memory;

static void load_shell(void) {
    struct EXT2DriverRequest request = {
        .buf          = (void *) 0,
        .name         = "shell",
        .name_len     = 5,
        .parent_inode = 1,
        .buffer_size  = USER_IMAGE_SIZE,
        .is_directory = false,
    };

    switch (process_memory_load(&shell_memory, request)) {
        case PROCESS_LOAD_SUCCESS:
            break;
        case PROCESS_LOAD_FAIL_FS_READ_FAILURE:
            halt_with_message("tapOS: /shell not found, run: make insert-shell");
        default:
            halt_with_message("tapOS: out of memory for the shell");
    }
    paging_use_page_directory(shell_memory.page_dir);
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
    // No syscall turns the keyboard on, so the kernel does it before Ring 3
    keyboard_state_activate();

    load_shell();
    enter_user_mode((void *) shell_memory.entrypoint, (void *) shell_memory.stack_top);
}
