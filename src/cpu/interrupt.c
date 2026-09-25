#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/interrupt.h"
#include "header/driver/keyboard.h"
// #include "header/text/framebuffer.h" must be changed

// Exception report, written on the last row
#define EXCEPTION_REPORT_ROW  (FRAMEBUFFER_HEIGHT - 1)
#define EXCEPTION_FG          0xF
#define EXCEPTION_BG          0x4

static volatile uint32_t interrupt_count[INTERRUPT_VECTOR_COUNT];

static const char *const exception_name[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint",
    "Overflow", "Bound Range", "Invalid Opcode", "Device N/A",
    "Double Fault", "Coproc Overrun", "Invalid TSS", "Segment Not Present",
    "Stack Fault", "General Protection", "Page Fault", "Reserved",
    "x87 FP Error", "Alignment Check", "Machine Check", "SIMD FP",
    "Virtualization", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection", "VMM Communication", "Security", "Reserved",
};

uint32_t interrupt_get_count(uint8_t int_vector) {
    if (int_vector >= INTERRUPT_VECTOR_COUNT)
        return 0;
    return interrupt_count[int_vector];
}


static uint8_t report_puts(uint8_t col, const char *str) {
    while (*str && col < FRAMEBUFFER_WIDTH)
        framebuffer_write(EXCEPTION_REPORT_ROW, col++, *str++, EXCEPTION_FG, EXCEPTION_BG);
    return col;
}

static uint8_t report_hex(uint8_t col, uint32_t value, uint8_t digits) {
    static const char hex[] = "0123456789ABCDEF";
    col = report_puts(col, "0x");
    for (int shift = (digits - 1) * 4; shift >= 0 && col < FRAMEBUFFER_WIDTH; shift -= 4)
        framebuffer_write(EXCEPTION_REPORT_ROW, col++, hex[(value >> shift) & 0xF], EXCEPTION_FG, EXCEPTION_BG);
    return col;
}

static void report_exception_and_halt(const struct InterruptFrame *frame) {
    uint8_t col = 0;
    for (uint8_t i = 0; i < FRAMEBUFFER_WIDTH; i++)
        framebuffer_write(EXCEPTION_REPORT_ROW, i, ' ', EXCEPTION_FG, EXCEPTION_BG);

    col = report_puts(col, "EXCEPTION ");
    col = report_hex(col, frame->int_number, 2);
    col = report_puts(col, " ");
    col = report_puts(col, exception_name[frame->int_number & 0x1F]);
    col = report_puts(col, " err=");
    col = report_hex(col, frame->int_stack.error_code, 4);
    col = report_puts(col, " eip=");
    col = report_hex(col, frame->int_stack.eip, 8);
    col = report_puts(col, " cs=");
    report_hex(col, frame->int_stack.cs, 2);

    __asm__ volatile("cli");
    while (true)
        __asm__ volatile("hlt");
}

static void handle_irq(uint8_t irq) {
    switch (irq) {
        case IRQ_KEYBOARD:
            keyboard_isr();
            pic_ack(IRQ_KEYBOARD);
            break;

        case IRQ_LPT1_SPUR:
        case IRQ_SECOND_ATA:
            break;

        default:
            pic_ack(irq);
            break;
    }
}

void main_interrupt_handler(struct InterruptFrame frame) {
    uint32_t vector = frame.int_number;

    if (vector < INTERRUPT_VECTOR_COUNT)
        interrupt_count[vector]++;

    // Software interrupt test, just return
    if (vector == INT_VECTOR_BREAKPOINT || vector == INT_VECTOR_TEST)
        return;

    // CPU exception
    if (vector < PIC1_OFFSET) {
        report_exception_and_halt(&frame);
        return;
    }

    // Hardware IRQ from PIC
    if (vector < PIC2_OFFSET + 8) {
        handle_irq((uint8_t) (vector - PIC1_OFFSET));
        return;
    }
}