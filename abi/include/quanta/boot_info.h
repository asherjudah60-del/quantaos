#ifndef QUANTA_BOOT_INFO_H
#define QUANTA_BOOT_INFO_H

#include <stdint.h>

#define QUANTA_BOOT_INFO_VERSION 2U
#define QUANTA_BOOT_INFO_MAGIC 0x51424f4f54494e46ULL /* "QBOOTINF" */
#define QUANTA_BOOT_MEMORY_USABLE 1U
#define QUANTA_BOOT_MAX_MEMORY_REGIONS 64U

struct quanta_boot_memory_region {
    uint64_t physical_start;
    uint64_t length;
    uint32_t type;
    uint32_t attributes;
} __attribute__((packed));

/* Produced only by bootloader; passed to the kernel in RDI. */
struct quanta_boot_info {
    uint64_t magic;
    uint32_t version;
    uint32_t size;
    uint64_t memory_map_physical;
    uint64_t memory_map_entries;
    uint64_t framebuffer_physical;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint8_t framebuffer_bpp;
    uint8_t reserved[3];
    uint64_t rsdp_physical;
    uint64_t storage_physical;
    uint64_t storage_sectors;
} __attribute__((packed));

_Static_assert(sizeof(struct quanta_boot_memory_region) == 24,
    "boot memory-region ABI must remain packed");
_Static_assert(sizeof(struct quanta_boot_info) == 80,
    "boot-info ABI must remain 64 bytes");

#endif
