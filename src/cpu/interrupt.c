#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/portio.h"
#include "header/cpu/interrupt.h"
#include "header/driver/keyboard.h"
#include "header/text/framebuffer.h"
#include "header/memory/swap.h"
#include "header/cpu/syscall.h"

static volatile uint32_t pit_ticks;
static void (*timer_handler)(struct InterruptFrame *frame);

void activate_keyboard_interrupt(void) {
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}

void io_wait(void) {
    out(0x80, 0);
}

void pic_ack(uint8_t irq) {
    if (irq >= 8) out(PIC2_COMMAND, PIC_ACK);
    out(PIC1_COMMAND, PIC_ACK);
}

void pic_remap(void) {
    // Starts the initialization sequence in cascade mode
    out(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4); 
    io_wait();
    out(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out(PIC1_DATA, PIC1_OFFSET); // ICW2: Master PIC vector offset
    io_wait();
    out(PIC2_DATA, PIC2_OFFSET); // ICW2: Slave PIC vector offset
    io_wait();
    out(PIC1_DATA, 0b0100); // ICW3: tell Master PIC, slave PIC at IRQ2 (0000 0100)
    io_wait();
    out(PIC2_DATA, 0b0010); // ICW3: tell Slave PIC its cascade identity (0000 0010)
    io_wait();

    out(PIC1_DATA, ICW4_8086);
    io_wait();
    out(PIC2_DATA, ICW4_8086);
    io_wait();

    // Disable all interrupts
    out(PIC1_DATA, PIC_DISABLE_ALL_MASK);
    out(PIC2_DATA, PIC_DISABLE_ALL_MASK);
}

void pit_initialize(uint32_t frequency_hz) {
    uint32_t divisor;

    if (frequency_hz == 0) {
        frequency_hz = 1;
    }
    if (frequency_hz > PIT_INPUT_HZ) {
        frequency_hz = PIT_INPUT_HZ;
    }

    divisor = PIT_INPUT_HZ / frequency_hz;
    if (divisor == 0 || divisor > 0xFFFFu) {
        divisor = 0xFFFFu;
    }

    pit_ticks = 0;
    out(PIT_COMMAND, PIT_COMMAND_CHANNEL0);
    out(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFFu));
    out(PIT_CHANNEL0_DATA, (uint8_t)(divisor >> 8));
    out(PIC1_DATA, in(PIC1_DATA) & ~(1u << IRQ_TIMER));
}

uint32_t pit_get_ticks(void) {
    return pit_ticks;
}

void interrupt_set_timer_handler(void (*handler)(struct InterruptFrame *frame)) {
    timer_handler = handler;
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
    exception_write_hex(2, 14, frame->int_number);

    exception_write_text(3, 0, "Error code : 0x");
    exception_write_hex(3, 14, frame->int_stack.error_code);
    
    exception_write_text(4, 0, "EIP        : 0x");
    exception_write_hex(4, 14, frame->int_stack.eip);

    exception_write_text(5, 0, "CR2        : 0x");
    exception_write_hex(5, 14, read_cr2());

    exception_write_text(6, 0, "Kernel dihentikan");

    while (true) {
        __asm__ volatile ("hlt");
    }
}

void main_interrupt_handler(struct InterruptFrame *frame) {
    if (frame == NULL) {
        return;
    }
    if (frame->int_number == PAGE_FAULT_VECTOR && handle_page_fault(frame->int_stack.error_code))
        return;
    if (frame->int_number < 32U){
        exception_panic(frame);
    }
    switch (frame->int_number) {
        case PIC1_OFFSET + IRQ_TIMER:
            /* ACK before scheduler hook: a non-returning context switch is safe. */
            pic_ack(IRQ_TIMER);
            pit_ticks++;
            if (timer_handler != NULL) {
                timer_handler(frame);
            }
            break;
        case PIC1_OFFSET + IRQ_KEYBOARD :
            keyboard_isr();
            break;
        case SYSCALL_VECTOR:
            syscall_dispatch(frame);
            break;
    }
}
