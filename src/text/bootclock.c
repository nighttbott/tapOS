#include <stdint.h>
#include "header/cpu/portio.h"
#include "header/text/bootclock.h"

#define PIT_HZ       1193182u
#define PIT_COMMAND  0x43
#define PIT_CHANNEL0 0x40

static uint16_t previous;
static uint32_t ticks;        /* PIT ticks not yet counted as milliseconds */
static uint32_t whole_ms;     /* milliseconds since boot_clock_start() */

static uint16_t pit_read(void) {
    out(PIT_COMMAND, 0x00);          /* latch the counter */
    uint16_t low = in(PIT_CHANNEL0);
    return (uint16_t) ((in(PIT_CHANNEL0) << 8) | low);
}

void boot_clock_start(void) {
    out(PIT_COMMAND, 0x34);          /* channel 0, low/high byte, mode 2 (counts down by one) */
    out(PIT_CHANNEL0, 0);
    out(PIT_CHANNEL0, 0);            /* reload 65536 */
    previous = pit_read();
    ticks = 0;
    whole_ms = 0;
}

uint32_t boot_clock_ms(void) {
    uint16_t now = pit_read();

    ticks += (uint16_t) (previous - now);   /* the counter runs down and wraps */
    previous = now;
    /* 1193 ticks per millisecond (really 1193.18): a drift of 0.015%, under a millisecond per boot screen */
    while (ticks >= PIT_HZ / 1000u) {
        ticks -= PIT_HZ / 1000u;
        whole_ms++;
    }
    return whole_ms;
}

void boot_clock_wait(uint32_t ms) {
    uint32_t start = boot_clock_ms();

    while (boot_clock_ms() - start < ms) {
    }
}
