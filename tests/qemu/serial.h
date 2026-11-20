#ifndef _SERIAL_H
#define _SERIAL_H

// COM1 serial port. QEMU saves everything printed here into a text file (-serial file:...)
void serial_init(void);
void serial_write(const char *text);

#endif
