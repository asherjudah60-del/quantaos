#include <stdint.h>
#include <quanta/libc.h>

void _start(void) {
    if (quanta_mkdir("/tmp") != QUANTA_STATUS_OK) {
        quanta_write("mkdir: failed\n");
        quanta_exit();
    }
    quanta_exit();
}
