#ifndef QUANTA_USER_LIBC_H
#define QUANTA_USER_LIBC_H

#include <stdint.h>
#include <quanta/syscall.h>

long quanta_syscall(uint64_t number, uint64_t argument, const void *input,
    void *output, uint64_t length);
void quanta_write(const char *text);
void quanta_exit(void);
int quanta_open(const char *path, uint64_t flags, uint64_t *handle);
int quanta_close(uint64_t handle);
long quanta_read(uint64_t handle, void *buffer, uint64_t length);
int quanta_stat(const char *path, struct quanta_file_stat *stat);
int quanta_list(const char *path, char *output, uint64_t capacity);
int quanta_mkdir(const char *path);
int quanta_rtc_read(uint8_t clock[6]);

#endif
