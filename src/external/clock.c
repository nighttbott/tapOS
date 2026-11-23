#include "header/driver/cmos.h"
#include "header/user/syscall.h"

#define CLOCK_COLUMN 72u
#define CLOCK_TEXT_LENGTH 8u

static void two_digits(char *out, uint8_t value) {
    out[0] = (char)('0' + value / 10u);
    out[1] = (char)('0' + value % 10u);
}

int main(void) {
    struct CMOSTime now;
    char text[CLOCK_TEXT_LENGTH] = {0, 0, ':', 0, 0, ':', 0, 0};
    uint8_t shown_second = 0xFF;

    /* There is no sleep syscall, so this polls; the scheduler gives the shell its share. */
    for (;;) {
        if (sys_get_time(&now) != 0 || now.second == shown_second)
            continue;
        two_digits(text, now.hour);
        two_digits(text + 3, now.minute);
        two_digits(text + 6, now.second);
        sys_write_status(text, CLOCK_TEXT_LENGTH, CLOCK_COLUMN);
        shown_second = now.second;
    }
}
