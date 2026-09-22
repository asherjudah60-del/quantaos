#ifndef QUANTA_KERNEL_TASK_H
#define QUANTA_KERNEL_TASK_H

#include <stdint.h>
#include <quanta/capability.h>
#include <quanta/ipc.h>

typedef uint64_t quanta_task_id;
struct quanta_task;
int quanta_task_grant(struct quanta_task *task, enum quanta_capability_kind kind,
    uint64_t resource, quanta_capability_handle *handle);
int quanta_task_revoke(struct quanta_task *task, quanta_capability_handle handle);
void quanta_task_bootstrap(void) __attribute__((noreturn));
long quanta_task_syscall(uint64_t number, uint64_t handle, uint64_t message,
    uint64_t payload, uint64_t length, uint64_t return_ip, uint64_t flags);
void quanta_task_user_fault(uint64_t vector) __attribute__((noreturn));

#endif
