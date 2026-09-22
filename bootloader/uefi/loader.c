#include <stdint.h>
#include <quanta/boot_info.h>

#define EFI_SUCCESS 0
#define EFI_ALLOCATE_ADDRESS 2
#define EFI_ALLOCATE_ANY 0
#define EFI_LOADER_DATA 2
#define EFI_CONVENTIONAL_MEMORY 7
#define PAGE_SIZE 4096ULL
#define KERNEL_PHYSICAL 0x200000ULL
#define STORAGE_PHYSICAL 0x300000ULL
#define PML4_PHYSICAL 0x100000ULL
#define PAGE_PRESENT_WRITE 3ULL
#define PAGE_LARGE 0x83ULL
#define KERNEL_VMA 0xffffffff80100000ULL
#define PHDR_LOAD 1U

typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef struct EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;
typedef EFI_STATUS (*allocate_pages_fn)(uint32_t, uint32_t, uint64_t, uint64_t *);
typedef EFI_STATUS (*allocate_pool_fn)(uint32_t, uint64_t, void **);
typedef EFI_STATUS (*get_memory_map_fn)(uint64_t *, void *, uint64_t *, uint64_t *, uint32_t *);
typedef EFI_STATUS (*exit_boot_services_fn)(EFI_HANDLE, uint64_t);
typedef EFI_STATUS (*output_string_fn)(void *, uint16_t *);

struct EFI_BOOT_SERVICES {
    uint8_t header[24];
    void *functions[40];
};
struct EFI_SYSTEM_TABLE {
    uint8_t header[24];
    uint16_t *firmware_vendor;
    uint32_t firmware_revision;
    EFI_HANDLE console_in_handle;
    void *con_in;
    EFI_HANDLE console_out_handle;
    void *con_out;
    EFI_HANDLE console_error_handle;
    void *std_err;
    void *runtime_services;
    struct EFI_BOOT_SERVICES *boot_services;
};
struct EFI_MEMORY_DESCRIPTOR {
    uint32_t type;
    uint32_t pad;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t pages;
    uint64_t attributes;
};
struct elf64_header {
    uint8_t ident[16]; uint16_t type, machine; uint32_t version;
    uint64_t entry, phoff, shoff; uint32_t flags; uint16_t ehsize, phentsize,
        phnum, shentsize, shnum, shstrndx;
};
struct elf64_phdr {
    uint32_t type, flags; uint64_t offset, virtual_address, physical_address;
    uint64_t file_size, memory_size, alignment;
};

extern const uint8_t _binary_build_kernel_quanta_elf_start[];
extern const uint8_t _binary_build_kernel_quanta_elf_end[];
extern const uint8_t _binary_build_live_root_img_start[];
extern const uint8_t _binary_build_live_root_img_end[];

static void *bs_function(EFI_SYSTEM_TABLE *table, uint32_t index) {
    return table->boot_services->functions[index];
}
static void copy_bytes(void *destination, const void *source, uint64_t length) {
    uint8_t *out = destination; const uint8_t *in = source;
    while (length--) *out++ = *in++;
}
static void serial_out(uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"((uint16_t)0x3f8));
}
static void serial_init(void) {
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0), "Nd"((uint16_t)0x3f9));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x80), "Nd"((uint16_t)0x3fb));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)3), "Nd"((uint16_t)0x3f8));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0), "Nd"((uint16_t)0x3f9));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)3), "Nd"((uint16_t)0x3fb));
}
static void serial_text(const char *text) {
    while (*text) serial_out((uint8_t)*text++);
}
static void clear_bytes(void *destination, uint64_t length) {
    uint8_t *out = destination;
    while (length--) *out++ = 0;
}
static EFI_STATUS allocate_pages(EFI_SYSTEM_TABLE *table, uint32_t policy,
    uint64_t *address, uint64_t pages) {
    allocate_pages_fn allocate = (allocate_pages_fn)bs_function(table, 2);
    return allocate(policy, EFI_LOADER_DATA, pages, address);
}
static int add_overflow(uint64_t left, uint64_t right, uint64_t *result) {
    if (left > ~0ULL - right) return 1;
    *result = left + right;
    return 0;
}
static int load_kernel(uint64_t destination, uint64_t allocation_size,
    uint64_t *entry_point) {
    const uint8_t *image = _binary_build_kernel_quanta_elf_start;
    uint64_t image_size = (uint64_t)(_binary_build_kernel_quanta_elf_end - image);
    const struct elf64_header *header;
    uint64_t destination_offset, virtual_end;
    int entry_loadable = 0;
    uint16_t index;
    if (image_size < sizeof(struct elf64_header)) { serial_text("QUANTA_UEFI_ELF_HEADER\n"); return -1; }
    header = (const struct elf64_header *)image;
    if (header->ident[0] != 0x7f || header->ident[1] != 'E' || header->ident[2] != 'L' ||
        header->ident[3] != 'F' || header->ident[4] != 2 || header->ident[5] != 1 ||
        header->ident[6] != 1 || header->type != 2 || header->machine != 0x3e ||
        header->version != 1 || header->ehsize != sizeof(struct elf64_header) ||
        header->phentsize != sizeof(struct elf64_phdr) || header->phnum == 0U ||
        header->phoff > image_size ||
        (uint64_t)header->phentsize * header->phnum > image_size - header->phoff) { serial_text("QUANTA_UEFI_ELF_TABLE\n"); return -1; }
    for (index = 0; index < header->phnum; ++index) {
        const struct elf64_phdr *program = (const struct elf64_phdr *)(image +
            header->phoff + (uint64_t)index * header->phentsize);
        uint64_t physical_end, file_end;
        if (program->type != PHDR_LOAD) continue;
        if (program->file_size > program->memory_size || program->offset > image_size ||
            program->file_size > image_size - program->offset ||
            program->physical_address < KERNEL_PHYSICAL ||
            add_overflow(program->physical_address, program->memory_size, &physical_end) ||
            physical_end < program->physical_address || physical_end - KERNEL_PHYSICAL > allocation_size ||
            add_overflow(program->virtual_address, program->memory_size, &virtual_end) ||
            (program->alignment > 1U &&
             (program->alignment & (program->alignment - 1U)) != 0U) ||
            (program->alignment > 1U &&
             (program->physical_address % program->alignment) != (program->offset % program->alignment))) { serial_text("QUANTA_UEFI_ELF_SEGMENT\n"); return -1; }
        file_end = program->offset + program->file_size;
        if (file_end > image_size) { serial_text("QUANTA_UEFI_ELF_FILE\n"); return -1; }
        if (header->entry >= program->virtual_address && header->entry < virtual_end) {
            entry_loadable = 1;
        }
        destination_offset = program->physical_address - KERNEL_PHYSICAL;
        copy_bytes((void *)(destination + destination_offset), image + program->offset, program->file_size);
        clear_bytes((void *)(destination + destination_offset + program->file_size),
            program->memory_size - program->file_size);
    }
    if (!entry_loadable) { serial_text("QUANTA_UEFI_ELF_ENTRY\n"); return -1; }
    *entry_point = header->entry;
    return 0;
}
static void build_tables(uint64_t pml4_physical, uint64_t kernel_physical) {
    uint64_t *pml4 = (uint64_t *)(uintptr_t)pml4_physical;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4_physical + 0x1000);
    uint64_t *pdlow = (uint64_t *)(uintptr_t)(pml4_physical + 0x2000);
    uint64_t *pdhigh = (uint64_t *)(uintptr_t)(pml4_physical + 0x3000);
    uint64_t *pt1 = (uint64_t *)(uintptr_t)(pml4_physical + 0x4000);
    uint64_t *pt2 = (uint64_t *)(uintptr_t)(pml4_physical + 0x5000);
    uint64_t index, address;
    clear_bytes(pml4, 6 * PAGE_SIZE);
    pml4[0] = (uint64_t)pdpt | PAGE_PRESENT_WRITE;
    pml4[511] = (uint64_t)pdpt | PAGE_PRESENT_WRITE;
    pdpt[0] = (uint64_t)pdlow | PAGE_PRESENT_WRITE;
    pdpt[510] = (uint64_t)pdhigh | PAGE_PRESENT_WRITE;
    for (index = 0, address = 0; index < 512; ++index, address += 0x200000) pdlow[index] = address | PAGE_LARGE;
    pdhigh[0] = (uint64_t)pt1 | PAGE_PRESENT_WRITE;
    pdhigh[1] = (uint64_t)pt2 | PAGE_PRESENT_WRITE;
    for (index = 0, address = kernel_physical; index < 256; ++index, address += PAGE_SIZE) pt1[index] = address | PAGE_PRESENT_WRITE;
    for (index = 0, address = kernel_physical + 256 * PAGE_SIZE; index < 256; ++index, address += PAGE_SIZE) pt2[index] = address | PAGE_PRESENT_WRITE;
}
static void jump_kernel(uint64_t root, uint64_t entry, struct quanta_boot_info *info) {
    __asm__ volatile("cli; mov %0, %%cr3; mov %1, %%rdi; jmp *%2" : :
        "r"(root), "r"(info), "r"(entry) : "memory");
    for (;;) { }
}
EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *table) {
    const uint8_t *storage = _binary_build_live_root_img_start;
    uint64_t storage_size = (uint64_t)(_binary_build_live_root_img_end - storage);
    uint64_t storage_pages = (storage_size + PAGE_SIZE - 1U) / PAGE_SIZE;
     uint64_t kernel_pages = 512;
    uint64_t kernel_address = 0;
    uint64_t pml4_address = 0;
    uint64_t storage_address = 0;
    uint64_t info_address = 0x9000;
    uint64_t map_address = 0x5000;
    void **console;
    allocate_pool_fn allocate_pool = (allocate_pool_fn)bs_function(table, 5);
    get_memory_map_fn get_map = (get_memory_map_fn)bs_function(table, 4);
    exit_boot_services_fn exit_boot = (exit_boot_services_fn)bs_function(table, 26);
    uint8_t *map_buffer = 0; uint64_t map_size = 65536, map_key, descriptor_size; uint32_t descriptor_version;
    uint64_t *info_words = (uint64_t *)info_address;
    struct quanta_boot_memory_region *regions = (struct quanta_boot_memory_region *)map_address;
    uint64_t region_count = 0, offset, entry;
    EFI_STATUS status;
    (void)image_handle; (void)console; (void)allocate_pool;
    serial_init();
    serial_text("QUANTA_UEFI_ENTRY\n");
    if (allocate_pages(table, EFI_ALLOCATE_ANY, &kernel_address, kernel_pages) != EFI_SUCCESS) {
        serial_text("QUANTA_UEFI_ALLOC_KERNEL\n"); return 1;
    }
    if (allocate_pages(table, EFI_ALLOCATE_ANY, &pml4_address, 6) != EFI_SUCCESS) {
        serial_text("QUANTA_UEFI_ALLOC_PAGING\n"); return 1;
    }
    if (allocate_pages(table, EFI_ALLOCATE_ANY, &storage_address, storage_pages) != EFI_SUCCESS) {
        serial_text("QUANTA_UEFI_ALLOC_STORAGE\n"); return 1;
    }
    if (load_kernel(kernel_address, kernel_pages * PAGE_SIZE, &entry) != 0) { serial_text("QUANTA_UEFI_LOAD_ERROR\n"); return 1; }
    serial_text("QUANTA_UEFI_KERNEL_READY\n");
    copy_bytes((void *)(uintptr_t)storage_address, storage, storage_size);
    clear_bytes((void *)info_address, PAGE_SIZE);
    clear_bytes((void *)map_address, PAGE_SIZE);
    status = allocate_pool(EFI_LOADER_DATA, map_size, (void **)&map_buffer);
    if (status != EFI_SUCCESS) { serial_text("QUANTA_UEFI_MAP_ERROR\n"); return 1; }
    serial_text("QUANTA_UEFI_EXIT_READY\n");
    status = get_map(&map_size, map_buffer, &map_key, &descriptor_size, &descriptor_version);
    if (status != EFI_SUCCESS) return 1;
    for (offset = 0; offset + descriptor_size <= map_size && region_count < QUANTA_BOOT_MAX_MEMORY_REGIONS; offset += descriptor_size) {
        struct EFI_MEMORY_DESCRIPTOR *descriptor = (struct EFI_MEMORY_DESCRIPTOR *)(map_buffer + offset);
        if (descriptor->type == EFI_CONVENTIONAL_MEMORY) {
            regions[region_count].physical_start = descriptor->physical_start;
            regions[region_count].length = descriptor->pages * PAGE_SIZE;
            regions[region_count].type = QUANTA_BOOT_MEMORY_USABLE;
            regions[region_count].attributes = 0; ++region_count;
        }
    }
    info_words[0] = QUANTA_BOOT_INFO_MAGIC; ((uint32_t *)info_words)[2] = QUANTA_BOOT_INFO_VERSION;
    ((uint32_t *)info_words)[3] = sizeof(struct quanta_boot_info);
    info_words[2] = (uint64_t)regions; info_words[3] = region_count;
    info_words[8] = storage_address; info_words[9] = (storage_size + 511U) / 512U;
    status = exit_boot(image_handle, map_key);
    if (status != EFI_SUCCESS) {
        serial_text("QUANTA_UEFI_EXIT_RETRY\n");
        map_size = 65536;
        status = get_map(&map_size, map_buffer, &map_key, &descriptor_size,
            &descriptor_version);
        if (status != EFI_SUCCESS) {
            serial_text("QUANTA_UEFI_MAP_RETRY_ERROR\n");
            return 1;
        }
        status = exit_boot(image_handle, map_key);
        if (status != EFI_SUCCESS) {
            serial_text("QUANTA_UEFI_EXIT_ERROR\n");
            return 1;
        }
    }
    serial_text("QUANTA_UEFI_EXIT_DONE\n");
    build_tables(pml4_address, kernel_address);
    serial_text("QUANTA_UEFI_TABLES_READY\n");
    jump_kernel(pml4_address, entry, (struct quanta_boot_info *)info_address);
    return 0;
}
