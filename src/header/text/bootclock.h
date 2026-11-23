#ifndef _BOOTCLOCK_H
#define _BOOTCLOCK_H

#include <stdint.h>

/**
 * A millisecond clock for the boot screen. Interrupts are still off while the kernel boots, so it
 * reads the PIT counter by polling; call boot_clock_ms() at least every 50 ms to see every wrap.
 */
void boot_clock_start(void);

/** Milliseconds since boot_clock_start(). */
uint32_t boot_clock_ms(void);

/** Busy-waits for the given number of milliseconds. */
void boot_clock_wait(uint32_t ms);

#endif
