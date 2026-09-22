#ifndef QUANTA_KERNEL_MEMORY_H
#define QUANTA_KERNEL_MEMORY_H

#include <stdint.h>
#include <quanta/boot_info.h>

typedef uint64_t quanta_physical_address;
typedef uint64_t quanta_virtual_address;
struct quanta_address_space;

#define QUANTA_PAGE_PRESENT  (1ULL << 0)
#define QUANTA_PAGE_WRITABLE (1ULL << 1)
#define QUANTA_PAGE_USER     (1ULL << 2)
#define QUANTA_PAGE_NO_EXECUTE (1ULL << 63)

int quanta_frame_allocate(quanta_physical_address *frame);
void quanta_frame_release(quanta_physical_address frame);
int quanta_address_space_map(struct quanta_address_space *space,
    quanta_virtual_address virtual_address, quanta_physical_address physical_address,
    uint64_t flags);
void quanta_memory_initialize(const struct quanta_boot_info *boot_info);
uint64_t quanta_frame_allocate_or_panic(void);
uint64_t quanta_address_space_create(void);
void quanta_map_page(uint64_t page_root, uint64_t virtual_address,
    uint64_t physical_address, uint64_t flags);
void quanta_address_space_destroy(uint64_t page_root);

#endif
