#ifndef QUANTA_KERNEL_IPC_ROUTER_H
#define QUANTA_KERNEL_IPC_ROUTER_H

#include <quanta/ipc.h>
int quanta_ipc_send(const struct quanta_ipc_message *message, const void *payload);
#endif
