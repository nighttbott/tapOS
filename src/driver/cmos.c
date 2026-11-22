#include <stdbool.h>
#include "header/cpu/portio.h"
#include "header/driver/cmos.h"

#define CMOS_ADDRESS_PORT 0x70
#define CMOS_DATA_PORT    0x71
#define CMOS_SECOND       0x00
#define CMOS_MINUTE       0x02
#define CMOS_HOUR         0x04
#define CMOS_DAY          0x07
#define CMOS_MONTH        0x08
#define CMOS_YEAR         0x09
#define CMOS_STATUS_A     0x0A
#define CMOS_STATUS_B     0x0B
#define STATUS_A_UPDATING 0x80
#define STATUS_B_24_HOUR  0x02
#define STATUS_B_BINARY   0x04
#define HOUR_PM_BIT       0x80

struct CMOSRaw {
    uint8_t second, minute, hour, day, month, year;
};

static uint8_t cmos_read(uint8_t reg) {
    out(CMOS_ADDRESS_PORT, reg);
    return in(CMOS_DATA_PORT);
}

static void cmos_read_raw(struct CMOSRaw *raw) {
    while (cmos_read(CMOS_STATUS_A) & STATUS_A_UPDATING) {
    }
    raw->second = cmos_read(CMOS_SECOND);
    raw->minute = cmos_read(CMOS_MINUTE);
    raw->hour = cmos_read(CMOS_HOUR);
    raw->day = cmos_read(CMOS_DAY);
    raw->month = cmos_read(CMOS_MONTH);
    raw->year = cmos_read(CMOS_YEAR);
}

static uint8_t from_bcd(uint8_t value) {
    return (uint8_t)((value & 0x0F) + (value >> 4) * 10);
}

void cmos_read_time(struct CMOSTime *time) {
    struct CMOSRaw raw, again;

    /* A read that straddles an RTC update gives a torn time, so read until two reads agree. */
    cmos_read_raw(&raw);
    for (;;) {
        cmos_read_raw(&again);
        if (again.second == raw.second && again.minute == raw.minute && again.hour == raw.hour &&
            again.day == raw.day && again.month == raw.month && again.year == raw.year)
            break;
        raw = again;
    }

    uint8_t status = cmos_read(CMOS_STATUS_B);
    bool pm = !(status & STATUS_B_24_HOUR) && (raw.hour & HOUR_PM_BIT);
    raw.hour &= (uint8_t)~HOUR_PM_BIT;
    if (!(status & STATUS_B_BINARY)) {
        raw.second = from_bcd(raw.second);
        raw.minute = from_bcd(raw.minute);
        raw.hour = from_bcd(raw.hour);
        raw.day = from_bcd(raw.day);
        raw.month = from_bcd(raw.month);
        raw.year = from_bcd(raw.year);
    }
    if (!(status & STATUS_B_24_HOUR)) {
        raw.hour %= 12;  /* 12 AM is hour 0 and 12 PM is hour 12 */
        if (pm)
            raw.hour += 12;
    }

    time->second = raw.second;
    time->minute = raw.minute;
    time->hour = raw.hour;
    time->day = raw.day;
    time->month = raw.month;
    time->year = (uint16_t)(2000u + raw.year);
}
