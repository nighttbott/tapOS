#include <stdint.h>
#include "header/cpu/portio.h"
#include "serial.h"

#define COM1 0x3F8

void serial_init(void) {
    out(COM1 + 1, 0x00);    // no serial interrupts, we only poll
    out(COM1 + 3, 0x80);    // enable divisor latch to set the speed
    out(COM1 + 0, 0x03);    // divisor 3 = 38400 baud
    out(COM1 + 1, 0x00);
    out(COM1 + 3, 0x03);    // 8 bits, no parity, 1 stop bit
    out(COM1 + 2, 0xC7);    // enable and clear the FIFO
    out(COM1 + 4, 0x0B);    // ready to send
}

void serial_write(const char *text) {
    while (*text != '\0') {
        while ((in(COM1 + 5) & 0x20) == 0)
            ;               // wait until the port can take another byte
        out(COM1, (uint8_t) *text++);
    }
}
