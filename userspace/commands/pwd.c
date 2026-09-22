#include <quanta/libc.h>

void _start(void) {
    quanta_write("/home/quanta\n");
    quanta_exit();
}
