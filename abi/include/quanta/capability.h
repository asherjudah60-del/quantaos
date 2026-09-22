#ifndef QUANTA_CAPABILITY_H
#define QUANTA_CAPABILITY_H

#include <stdint.h>

typedef uint64_t quanta_capability_handle;
#define QUANTA_CAPABILITY_INVALID ((quanta_capability_handle)0)

enum quanta_capability_kind {
    QUANTA_CAP_MMIO = 1,
    QUANTA_CAP_IRQ,
    QUANTA_CAP_DMA,
    QUANTA_CAP_ENDPOINT,
    QUANTA_CAP_NAMESPACE,
};

enum quanta_capability_right {
    QUANTA_CAP_RIGHT_SEND = 1U << 0,
    QUANTA_CAP_RIGHT_RECEIVE = 1U << 1,
    QUANTA_CAP_RIGHT_GRANT = 1U << 2,
};

#endif
