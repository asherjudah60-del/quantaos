#ifndef QUANTA_IPC_H
#define QUANTA_IPC_H

#include <stdint.h>
#include <quanta/capability.h>
#include <quanta/syscall.h>

#define QUANTA_IPC_VERSION 1U
#define QUANTA_IPC_MAX_CAPABILITIES 4U

struct quanta_ipc_message {
    uint16_t version;
    uint16_t service;
    uint32_t operation;
    uint64_t correlation_id;
    uint32_t payload_length;
    uint16_t capability_count;
    uint16_t flags;
    quanta_capability_handle capabilities[QUANTA_IPC_MAX_CAPABILITIES];
} __attribute__((packed));

#endif
