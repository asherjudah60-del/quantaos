#include <stdint.h>
#include <quanta/syscall.h>

extern long quanta_user_syscall(uint64_t number, uint64_t argument,
    const void *input, void *output, uint64_t length);

void _start(void) {
    quanta_user_syscall(QUANTA_SYSCALL_TASK_EXIT, 1, 0, 0, 0);
    for (;;) { }
}
