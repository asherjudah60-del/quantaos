#ifndef QUANTA_USER_SERVICE_H
#define QUANTA_USER_SERVICE_H

#include <quanta/ipc.h>
int quanta_service_dispatch(const struct quanta_ipc_message *message,
    const void *payload);
#endif
