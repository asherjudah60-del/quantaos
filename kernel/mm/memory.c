#include <quanta/memory.h>

#define PAGE_SIZE 0x1000ULL
#define KERNEL_HIGH_PML4_INDEX 511ULL
#define IDENTITY_LIMIT 0x40000000ULL
#define FRAME_COUNT (IDENTITY_LIMIT / PAGE_SIZE)
#define FRAME_BITMAP_WORDS (FRAME_COUNT / 64ULL)

static uint64_t pool_start;
static uint64_t pool_end;
static uint64_t next_frame;
static uint64_t free_frame_head;
static uint64_t allocated_frames[FRAME_BITMAP_WORDS];

static void clear_page(uint64_t physical) {
    volatile uint64_t *page = (volatile uint64_t *)(uintptr_t)physical;
    for (uint64_t index = 0; index < 512; ++index) {
        page[index] = 0;
    }
}

void quanta_memory_initialize(const struct quanta_boot_info *boot_info) {
    const struct quanta_boot_memory_region *regions =
        (const struct quanta_boot_memory_region *)(uintptr_t)boot_info->memory_map_physical;
    uint64_t best_start = 0U;
    uint64_t best_end = 0U;
    for (uint64_t index = 0; index < FRAME_BITMAP_WORDS; ++index) {
        allocated_frames[index] = 0;
    }
    pool_start = 0;
    pool_end = 0;
    next_frame = 0;
    free_frame_head = 0;
    for (uint64_t index = 0; index < boot_info->memory_map_entries; ++index) {
        uint64_t start = regions[index].physical_start;
        uint64_t end = start + regions[index].length;
        if (regions[index].type != QUANTA_BOOT_MEMORY_USABLE || end < start ||
            end <= 0x00400000ULL) {
            continue;
        }
        if (start < 0x00400000ULL) {
            start = 0x00400000ULL;
        }
        start = (start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (end > IDENTITY_LIMIT) {
            end = IDENTITY_LIMIT;
        }
        end &= ~(PAGE_SIZE - 1);
        if (start < end && end - start > best_end - best_start) {
            best_start = start;
            best_end = end;
        }
    }
    if (best_start != 0U) {
        pool_start = best_start;
        pool_end = best_end;
        next_frame = best_start;
        return;
    }
    for (;;) { __asm__ volatile("hlt"); }
}

int quanta_frame_allocate(quanta_physical_address *frame) {
    uint64_t allocated;
    if (frame == 0 || pool_start == 0) {
        return -1;
    }
    if (free_frame_head != 0) {
        allocated = free_frame_head;
        free_frame_head = *(volatile uint64_t *)(uintptr_t)allocated;
    } else {
        if (next_frame >= pool_end) {
            return -1;
        }
        allocated = next_frame;
        next_frame += PAGE_SIZE;
    }
    uint64_t bit = (allocated - pool_start) / PAGE_SIZE;
    allocated_frames[bit / 64U] |= 1ULL << (bit % 64U);
    clear_page(allocated);
    *frame = allocated;
    return 0;
}

static int frame_is_allocated(uint64_t frame) {
    uint64_t bit;
    if (frame < pool_start || frame >= next_frame ||
        (frame & (PAGE_SIZE - 1)) != 0) {
        return 0;
    }
    bit = (frame - pool_start) / PAGE_SIZE;
    return (allocated_frames[bit / 64U] & (1ULL << (bit % 64U))) != 0;
}

void quanta_frame_release(quanta_physical_address frame) {
    uint64_t bit = (frame - pool_start) / PAGE_SIZE;
    if (!frame_is_allocated(frame)) return;
    allocated_frames[bit / 64U] &= ~(1ULL << (bit % 64U));
    *(volatile uint64_t *)(uintptr_t)frame = free_frame_head;
    free_frame_head = frame;
}

uint64_t quanta_frame_allocate_or_panic(void) {
    quanta_physical_address frame;
    if (quanta_frame_allocate(&frame) != 0) {
        for (;;) { __asm__ volatile("hlt"); }
    }
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

static void destroy_table(uint64_t table_frame, uint8_t level) {
    volatile uint64_t *table = (volatile uint64_t *)(uintptr_t)table_frame;
    if (level > 1U) {
        for (uint64_t index = 0; index < 512U; ++index) {
            uint64_t entry = table[index];
            if ((entry & QUANTA_PAGE_PRESENT) == 0U) continue;
            if (level < 4U && (entry & (1ULL << 7)) != 0U) continue;
            destroy_table(entry & ~0xfffULL, level - 1U);
        }
    }
    quanta_frame_release(table_frame);
}

void quanta_address_space_destroy(uint64_t page_root) {
    volatile uint64_t *pml4;
    uint64_t active_root;
    if (!frame_is_allocated(page_root)) return;
    __asm__ volatile("mov %%cr3, %0" : "=r"(active_root));
    if ((active_root & ~0xfffULL) == page_root) return;
    pml4 = (volatile uint64_t *)(uintptr_t)page_root;
    for (uint64_t index = 0; index < KERNEL_HIGH_PML4_INDEX; ++index) {
        uint64_t entry = pml4[index];
        if ((entry & QUANTA_PAGE_PRESENT) != 0U) {
            destroy_table(entry & ~0xfffULL, 3U);
        }
    }
    quanta_frame_release(page_root);
}
