#ifndef QUANTA_KERNEL_ELF_H
#define QUANTA_KERNEL_ELF_H

#include <stdint.h>

struct quanta_elf_image {
    uint64_t entry;
    uint64_t image_start;
    uint64_t image_end;
};

int quanta_elf_validate(const uint8_t *image, uint64_t size,
    struct quanta_elf_image *result);

#endif
