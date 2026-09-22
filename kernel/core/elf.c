#include <stdint.h>
#include <quanta/elf.h>

struct elf_header {
    uint8_t magic[4];
    uint8_t class;
    uint8_t data;
    uint8_t version;
    uint8_t padding[9];
    uint16_t type;
    uint16_t machine;
    uint32_t version2;
    uint64_t entry;
    uint64_t program_offset;
    uint64_t section_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_size;
    uint16_t program_count;
} __attribute__((packed));

struct elf_program {
    uint32_t type;
    uint32_t flags;
    uint64_t file_offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} __attribute__((packed));

int quanta_elf_validate(const uint8_t *image, uint64_t size,
    struct quanta_elf_image *result) {
    const struct elf_header *header;
    uint64_t index, start = ~0ULL, end = 0;
    if (image == 0 || result == 0 || size < sizeof(struct elf_header)) return -1;
    header = (const struct elf_header *)image;
    if (header->magic[0] != 0x7f || header->magic[1] != 'E' || header->magic[2] != 'L' || header->magic[3] != 'F' ||
        header->class != 2 || header->data != 1 || header->machine != 0x3e || header->program_size != 56) return -1;
    if (header->program_offset > size || (uint64_t)header->program_count * header->program_size > size - header->program_offset) return -1;
    for (index = 0; index < header->program_count; ++index) {
        const struct elf_program *program = (const struct elf_program *)(image + header->program_offset + index * header->program_size);
        if (program->type != 1) continue;
        if (program->file_size > program->memory_size || program->file_offset > size || program->file_size > size - program->file_offset || program->virtual_address < 0x400000) return -1;
        if (program->virtual_address < start) start = program->virtual_address;
        if (program->virtual_address + program->memory_size < program->virtual_address) return -1;
        if (program->virtual_address + program->memory_size > end) end = program->virtual_address + program->memory_size;
    }
    if (start == ~0ULL || header->entry < start || header->entry >= end) return -1;
    result->entry = header->entry;
    result->image_start = start;
    result->image_end = end;
    return 0;
}
