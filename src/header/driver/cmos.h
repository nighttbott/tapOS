#ifndef _CMOS_H
#define _CMOS_H

#include <stdint.h>

/* Shared with user programs: the layout SYSCALL_GET_TIME writes. */
struct CMOSTime {
    uint8_t second, minute, hour, day, month;
    uint16_t year;
} __attribute__((packed));

/* Reads the real-time clock as 24-hour binary values. The year is 2000 + the RTC's two digits. */
void cmos_read_time(struct CMOSTime *out);

#endif
