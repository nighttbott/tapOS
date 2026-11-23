#include <stdint.h>
#include "header/cpu/portio.h"
#include "serial.h"

#define COM1 0x3F8

void serial_init(void) {
    out(COM1 + 1, 0x00);
    out(COM1 + 3, 0x80);
    out(COM1 + 0, 0x03);
    out(COM1 + 1, 0x00);
    out(COM1 + 3, 0x03);
    out(COM1 + 2, 0xC7);
    out(COM1 + 4, 0x0B);
}

void serial_write(const char *text) {
    while (*text != '\0') {
        while ((in(COM1 + 5) & 0x20) == 0)
            ;
        out(COM1, (uint8_t) *text++);
    }
}
