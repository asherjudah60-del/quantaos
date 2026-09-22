#include <stdint.h>
#include <quanta/libc.h>

void _start(void) {
    uint64_t handle;
    char buffer[QUANTA_SYSCALL_MAX_BUFFER + 1U];
    if (quanta_open("/etc/motd", QUANTA_OPEN_READ, &handle) != QUANTA_STATUS_OK) {
        quanta_write("cat: cannot open file\n");
        quanta_exit();
    }
    for (;;) {
        long count = quanta_read(handle, buffer, QUANTA_SYSCALL_MAX_BUFFER);
        if (count <= 0) break;
        buffer[count] = 0;
        quanta_write(buffer);
    }
    quanta_close(handle);
    quanta_exit();
}
