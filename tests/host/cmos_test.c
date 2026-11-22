/* Runs the real src/driver/cmos.c against a fake RTC register file. */
#include <stdio.h>
#include <string.h>
#include "header/driver/cmos.h"

static uint8_t regs[0x10];
static uint8_t selected;
static unsigned status_a_reads;
static unsigned tear_after_reads;  /* when non-zero, the seconds register changes once after this many reads */

void out(uint16_t port, uint8_t data) { if (port == 0x70) selected = data; }

uint8_t in(uint16_t port) {
    (void)port;
    if (selected == 0x0A) /* update in progress for the first two reads */
        return status_a_reads++ < 2 ? 0x80 : 0x00;
    if (selected == 0x00 && tear_after_reads && --tear_after_reads == 0)
        regs[0x00]++;
    return regs[selected];
}

static int failed;

static void expect(const char *name, uint8_t status_b, const uint8_t *time, uint8_t h, uint8_t m, uint8_t s,
                   uint16_t year) {
    struct CMOSTime now;
    memset(regs, 0, sizeof(regs));
    status_a_reads = 0;
    tear_after_reads = 0;
    regs[0x00] = time[0]; regs[0x02] = time[1]; regs[0x04] = time[2];
    regs[0x07] = time[3]; regs[0x08] = time[4]; regs[0x09] = time[5];
    regs[0x0B] = status_b;
    cmos_read_time(&now);
    int ok = now.hour == h && now.minute == m && now.second == s && now.year == year;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    failed += !ok;
}

int main(void) {
    /* status B: bit1 = 24 hour, bit2 = binary */
    expect("BCD 24 hour", 0x02, (uint8_t[]){0x56, 0x34, 0x23, 0x21, 0x11, 0x26}, 23, 34, 56, 2026);
    expect("binary 24 hour", 0x06, (uint8_t[]){56, 34, 23, 21, 11, 26}, 23, 34, 56, 2026);
    expect("BCD 12 hour, 3 PM", 0x00, (uint8_t[]){0x00, 0x05, 0x83, 0x21, 0x11, 0x26}, 15, 5, 0, 2026);
    expect("BCD 12 hour, 12 AM is hour 0", 0x00, (uint8_t[]){0x09, 0x10, 0x12, 0x21, 0x11, 0x26}, 0, 10, 9, 2026);
    expect("BCD 12 hour, 12 PM is hour 12", 0x00, (uint8_t[]){0x09, 0x10, 0x92, 0x21, 0x11, 0x26}, 12, 10, 9, 2026);

    struct CMOSTime now;
    memset(regs, 0, sizeof(regs));
    regs[0x0B] = 0x06; regs[0x00] = 10;
    status_a_reads = 0;
    tear_after_reads = 2;  /* the second read of the first pass sees a changed second */
    cmos_read_time(&now);
    int stable = now.second == 11;
    printf("%s a time that changes mid-read is read again\n", stable ? "PASS" : "FAIL");
    failed += !stable;

    printf("RESULT cmos: %s\n", failed ? "FAILED" : "0 FAIL");
    return failed != 0;
}
