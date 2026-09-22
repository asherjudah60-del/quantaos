#include <quanta/memory.h>

#define PAGE_SIZE 0x1000ULL
#define KERNEL_HIGH_PML4_INDEX 511ULL
#define IDENTITY_LIMIT 0x40000000ULL

static uint64_t next_frame;

static void clear_page(uint64_t physical) {
    volatile uint64_t *page = (volatile uint64_t *)(uintptr_t)physical;
    for (uint64_t index = 0; index < 512; ++index) {
        page[index] = 0;
    }
}

void quanta_memory_initialize(const struct quanta_boot_info *boot_info) {
    const struct quanta_boot_memory_region *regions =
        (const struct quanta_boot_memory_region *)(uintptr_t)boot_info->memory_map_physical;
    next_frame = 0;
    for (uint64_t index = 0; index < boot_info->memory_map_entries; ++index) {
        uint64_t start = regions[index].physical_start;
        uint64_t end = start + regions[index].length;
        if (regions[index].type != QUANTA_BOOT_MEMORY_USABLE || end <= 0x00400000ULL) {
            continue;
        }
        if (start < 0x00400000ULL) {
            start = 0x00400000ULL;
        }
        start = (start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (start < end && end <= IDENTITY_LIMIT) {
            next_frame = start;
            return;
        }
    }
    for (;;) { __asm__ volatile("hlt"); }
}

uint64_t quanta_frame_allocate_or_panic(void) {
    if (next_frame == 0 || next_frame >= IDENTITY_LIMIT) {
        for (;;) { __asm__ volatile("hlt"); }
    }
    uint64_t frame = next_frame;
    next_frame += PAGE_SIZE;
    clear_page(frame);
    return frame;
}

static uint64_t table_for(uint64_t *entry) {
    if ((*entry & QUANTA_PAGE_PRESENT) == 0) {
        *entry = quanta_frame_allocate_or_panic() | QUANTA_PAGE_PRESENT | QUANTA_PAGE_WRITABLE | QUANTA_PAGE_USER;
    }
    return *entry & ~0xfffULL;
}

uint64_t quanta_address_space_create(void) {
    uint64_t root = quanta_frame_allocate_or_panic();
    uint64_t active = *(volatile uint64_t *)(uintptr_t)(0x100000ULL + KERNEL_HIGH_PML4_INDEX * 8);
    ((volatile uint64_t *)(uintptr_t)root)[KERNEL_HIGH_PML4_INDEX] = active;
    return root;
}

void quanta_map_page(uint64_t root, uint64_t virtual_address, uint64_t physical_address,
    uint64_t flags) {
    volatile uint64_t *pml4 = (volatile uint64_t *)(uintptr_t)root;
    uint64_t pml4_index = (virtual_address >> 39) & 0x1ff;
    uint64_t pdpt_phys = table_for((uint64_t *)&pml4[pml4_index]);
    volatile uint64_t *pdpt = (volatile uint64_t *)(uintptr_t)pdpt_phys;
    uint64_t pd_phys = table_for((uint64_t *)&pdpt[(virtual_address >> 30) & 0x1ff]);
    volatile uint64_t *pd = (volatile uint64_t *)(uintptr_t)pd_phys;
    uint64_t pt_phys = table_for((uint64_t *)&pd[(virtual_address >> 21) & 0x1ff]);
    volatile uint64_t *pt = (volatile uint64_t *)(uintptr_t)pt_phys;
    pt[(virtual_address >> 12) & 0x1ff] = (physical_address & ~0xfffULL) | flags;
}
