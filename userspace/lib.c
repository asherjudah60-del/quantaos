#include <stdint.h>
#include <quanta/libc.h>

long quanta_syscall(uint64_t number, uint64_t argument, const void *input,
    void *output, uint64_t length) {
    extern long quanta_user_syscall(uint64_t, uint64_t, const void *, void *, uint64_t);
    return quanta_user_syscall(number, argument, input, output, length);
}

void quanta_write(const char *text) {
    uint64_t length = 0;
    while (text[length] && length < QUANTA_SYSCALL_MAX_BUFFER) ++length;
    quanta_syscall(QUANTA_SYSCALL_CONSOLE_WRITE, 1, text, 0, length);
}

void quanta_exit(void) {
    quanta_syscall(QUANTA_SYSCALL_TASK_EXIT, 1, 0, 0, 0);
    for (;;) { }
}

int quanta_open(const char *path, uint64_t flags, uint64_t *handle) {
    return (int)quanta_syscall(QUANTA_SYSCALL_FS_OPEN, 1, path, handle, flags);
}

int quanta_close(uint64_t handle) {
    return (int)quanta_syscall(QUANTA_SYSCALL_FS_CLOSE, handle, 0, 0, 0);
}

long quanta_read(uint64_t handle, void *buffer, uint64_t length) {
    return quanta_syscall(QUANTA_SYSCALL_FS_READ_HANDLE, handle, 0, buffer, length);
}

int quanta_stat(const char *path, struct quanta_file_stat *stat) {
    return (int)quanta_syscall(QUANTA_SYSCALL_FS_STAT, 1, path, stat, sizeof(*stat));
}

int quanta_list(const char *path, char *output, uint64_t capacity) {
    return (int)quanta_syscall(QUANTA_SYSCALL_FS_LIST, 1, path, output, capacity);
}

int quanta_mkdir(const char *path) {
    return (int)quanta_syscall(QUANTA_SYSCALL_FS_MKDIR, 1, path, 0, 0);
}

int quanta_rtc_read(uint8_t clock[6]) {
    return (int)quanta_syscall(QUANTA_SYSCALL_RTC_READ, 1, 0, clock, 6U);
}
