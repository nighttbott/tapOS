#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/portio.h"
#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"
#include "header/driver/keyboard.h"
#include "header/text/framebuffer.h"
#include "header/memory/swap.h"
#include "header/cpu/syscall.h"
#include "header/cpu/scheduler.h"

struct TaskStateSegment _tss;
static volatile uint32_t pit_ticks;

void tss_set_kernel_stack(void *stack_top) {
    _tss.esp0 = (uint32_t)stack_top;
    _tss.ss0 = GDT_KERNEL_DATA_SEGMENT_SELECTOR;
}

void activate_keyboard_interrupt(void) {
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}

void pit_initialize(uint16_t divisor) {
    if (divisor == 0)
        return;
    pit_ticks = 0;
    out(0x43, 0x36); 
    out(0x40, (uint8_t)(divisor & 0xFFu));
    out(0x40, (uint8_t)(divisor >> 8));
    out(PIC1_DATA, in(PIC1_DATA) & ~(1u << IRQ_TIMER));
}

uint32_t pit_get_ticks(void) {
    return pit_ticks;
}

void io_wait(void) {
    out(0x80, 0);
}

void pic_ack(uint8_t irq) {
    if (irq >= 8) out(PIC2_COMMAND, PIC_ACK);
    out(PIC1_COMMAND, PIC_ACK);
}

void pic_remap(void) {
    out(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4); 
    io_wait();
    out(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out(PIC1_DATA, PIC1_OFFSET);
    io_wait();
    out(PIC2_DATA, PIC2_OFFSET);
    io_wait();
    out(PIC1_DATA, 0b0100);
    io_wait();
    out(PIC2_DATA, 0b0010);
    io_wait();

    out(PIC1_DATA, ICW4_8086);
    io_wait();
    out(PIC2_DATA, ICW4_8086);
    io_wait();

    out(PIC1_DATA, PIC_DISABLE_ALL_MASK);
    out(PIC2_DATA, PIC_DISABLE_ALL_MASK);
}

static void exception_write_text(uint8_t row, uint8_t col, const char *text) {
    while (*text != '\0' && col < FRAMEBUFFER_WIDTH) {
        framebuffer_write(row, col, *text, 0x0F, 0x00);
        ++text;
        ++col;
    }
}

static void exception_write_hex(uint8_t row, uint8_t col, uint32_t value) {
    const char digits[] = "0123456789ABCDEF";

    for (uint32_t i = 0; i < 8; ++i){
        uint32_t shift = (7U - i) * 4U;
        uint8_t digit = (value >> shift) & 0x0F;
        framebuffer_write(row, col + i, digits[digit], 0x0F, 0x00);
    }
}

__attribute__((noreturn))
static void exception_panic(const struct InterruptFrame *frame) {
    __asm__ volatile("cli");

    framebuffer_clear();

    exception_write_text(0, 0, "tapOS : CPU EXCEPTION");

    exception_write_text(2, 0, "Vector     : 0x");
    exception_write_hex(2, 15, frame->int_number);

    exception_write_text(3, 0, "Error code : 0x");
    exception_write_hex(3, 15, frame->int_stack.error_code);
    
    exception_write_text(4, 0, "EIP        : 0x");
    exception_write_hex(4, 15, frame->int_stack.eip);

    exception_write_text(5, 0, "CR2        : 0x");
    exception_write_hex(5, 15, read_cr2());

    exception_write_text(6, 0, "Kernel dihentikan");

    while (true) {
        __asm__ volatile ("hlt");
    }
}

void main_interrupt_handler(struct InterruptFrame *frame) {
    if (frame->int_number == PAGE_FAULT_VECTOR && handle_page_fault(frame->int_stack.error_code))
        return;
    if (frame->int_number < 32U)
        exception_panic(frame);

    switch (frame->int_number) {
        case PIC1_OFFSET + IRQ_TIMER:
            ++pit_ticks;
            pic_ack(IRQ_TIMER);
            scheduler_handle_timer(frame);
            break;
        case PIC1_OFFSET + IRQ_KEYBOARD:
            keyboard_isr();
            pic_ack(IRQ_KEYBOARD);
            break;
        case SYSCALL_VECTOR:
            syscall_dispatch(frame);
            break;
    }
}
