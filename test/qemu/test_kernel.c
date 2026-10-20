#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/kernel-entrypoint.h"
#include "header/text/framebuffer.h"
#include "fs_test.h"
#include "fs_cases.h"
#include "serial.h"

/*
 * Test kernel. It replaces src/kernel.c only in test builds made by
 * test/qemu/run_qemu_tests.sh, so the normal OS is not changed.
 * TAPOS_TEST_SUITE picks the suite, for example -DTAPOS_TEST_SUITE=fs_suite_boundary
 */

#ifndef TAPOS_TEST_SUITE
#error "build with -DTAPOS_TEST_SUITE=<suite function> -DTAPOS_TEST_NAME=\"name\""
#endif

static uint8_t screen_row;

// Each result line goes to the serial port (for the script) and to the screen (for people)
static void kernel_print(const char *text) {
    serial_write(text);
    if (screen_row >= FRAMEBUFFER_HEIGHT) {
        framebuffer_scroll_up();
        screen_row = FRAMEBUFFER_HEIGHT - 1;
    }
    for (uint8_t col = 0; text[col] != '\0' && text[col] != '\n' && col < FRAMEBUFFER_WIDTH; col++)
        framebuffer_write(screen_row, col, text[col], 0x0F, 0x00);
    screen_row++;
}

void kernel_setup(void) {
    struct FsTestContext ctx;

    load_gdt(&_gdt_gdtr);
    framebuffer_clear();
    serial_init();

    fs_test_init(&ctx, kernel_print);
    TAPOS_TEST_SUITE(&ctx);
    fs_test_sum(&ctx, TAPOS_TEST_NAME);
    serial_write("TAPOS_TEST_DONE\n");

    __asm__ volatile("cli");
    while (true)
        __asm__ volatile("hlt");
}
