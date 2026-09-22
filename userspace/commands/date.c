#include <stdint.h>
#include <quanta/libc.h>

static void two_digits(char *output, uint8_t value) {
    output[0] = (char)('0' + value / 10U);
    output[1] = (char)('0' + value % 10U);
}

void _start(void) {
    uint8_t clock[6];
    char stamp[] = "20xx-xx-xx xx:xx:xx UTC\n";
    if (quanta_rtc_read(clock) != QUANTA_STATUS_OK) {
        quanta_write("date: unavailable\n");
        quanta_exit();
    }
    two_digits(stamp + 2, clock[0]);
    two_digits(stamp + 5, clock[1]);
    two_digits(stamp + 8, clock[2]);
    two_digits(stamp + 11, clock[3]);
    two_digits(stamp + 14, clock[4]);
    two_digits(stamp + 17, clock[5]);
    quanta_write(stamp);
    quanta_exit();
}