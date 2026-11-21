#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/cpu/idt.h"
#include "header/cpu/portio.h"
#include "header/driver/keyboard.h"
#include "header/text/framebuffer.h"
#include "header/kernel-entrypoint.h"
#include "header/process/process.h"
#include "header/cpu/scheduler.h"

__attribute__((noreturn))
static void halt_with_message(const char *text) {
    for (uint8_t col = 0; text[col] != '\0'; col++)
        framebuffer_write(0, col, text[col], 0x0F, 0x00);
    while (true)
        __asm__ volatile("hlt");
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

    __asm__ volatile("cli" : : : "memory");
    process_initialize();
    int32_t status = process_create_init();
    if (status == PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY)
        halt_with_message("tapOS: out of memory for /shell");
    if (status == PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT)
        halt_with_message("tapOS: /shell is empty or exceeds the user image limit");
    if (status != PROCESS_CREATE_SUCCESS)
        halt_with_message("tapOS: cannot load /shell, run: make insert-shell");

    struct ProcessControlBlock *init = process_lookup(PROCESS_INIT_PID);
    process_set_running(init);
    paging_use_page_directory(init->context.page_directory_virtual_addr);
    scheduler_init();
    enter_user_mode((void *)init->context.eip, (void *)init->context.cpu.stack.esp);
}
