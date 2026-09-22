#include <stdint.h>
#include <quanta/boot_info.h>
#include <quanta/arch.h>

struct descriptor_pointer { uint16_t limit; uint64_t base; } __attribute__((packed));
struct task_state_segment {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} __attribute__((packed));
struct idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

extern void quanta_arch_reload_gdt(const struct descriptor_pointer *pointer);
extern void quanta_arch_int80_stub(void);
extern void quanta_arch_fault_stub(void);
extern void quanta_arch_enter_user(uint64_t root, uint64_t ip, uint64_t stack,
    uint64_t argc, uint64_t argv) __attribute__((noreturn));

static uint64_t gdt[7] __attribute__((aligned(16)));
static struct task_state_segment tss;
static struct idt_gate idt[256] __attribute__((aligned(16)));
static uint16_t *const vga = (uint16_t *)(uintptr_t)0xb8000;
static uint8_t vga_x, vga_y;
static uint8_t shift_down;
static const uint8_t *memory_storage;
static uint64_t memory_storage_sectors;

void quanta_arch_storage_initialize(const struct quanta_boot_info *boot_info) {
    if (boot_info != 0 && boot_info->storage_physical != 0 && boot_info->storage_sectors != 0) {
        memory_storage = (const uint8_t *)(uintptr_t)boot_info->storage_physical;
        memory_storage_sectors = boot_info->storage_sectors;
    }
}

uint64_t quanta_arch_storage_physical(void) { return (uint64_t)(uintptr_t)memory_storage; }
uint64_t quanta_arch_storage_sectors(void) { return memory_storage_sectors; }

static void set_gate(uint8_t vector, void (*handler)(void), uint8_t attributes) {
    uint64_t address = (uint64_t)(uintptr_t)handler;
    idt[vector].offset_low = address;
    idt[vector].selector = 0x08;
    idt[vector].ist = 0;
    idt[vector].attributes = attributes;
    idt[vector].offset_middle = address >> 16;
    idt[vector].offset_high = address >> 32;
    idt[vector].reserved = 0;
}

static void outb(uint16_t port, uint8_t value) { __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port)); }
static void outw(uint16_t port, uint16_t value) { __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port)); }
static uint8_t inb(uint16_t port) { uint8_t value; __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }
static uint16_t inw(uint16_t port) { uint16_t value; __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port)); return value; }
static int ata_wait(uint8_t mask, uint8_t value) { uint32_t timeout = 1000000U; while (timeout--) { uint8_t status = inb(0x1f7); if (status & 1U) return -1; if ((status & mask) == value) return 0; } return -1; }
static int ata_select(uint32_t lba) { outb(0x1f6, (uint8_t)(0xe0U | ((lba >> 24) & 0x0fU))); outb(0x1f2, 1); outb(0x1f3, (uint8_t)lba); outb(0x1f4, (uint8_t)(lba >> 8)); outb(0x1f5, (uint8_t)(lba >> 16)); outb(0x1f7, 0x20); return ata_wait(0x88, 0x08); }
static void vga_cursor(void) { uint16_t pos = (uint16_t)(vga_y * 80 + vga_x); outb(0x3d4, 14); outb(0x3d5, pos >> 8); outb(0x3d4, 15); outb(0x3d5, pos); }
static void vga_scroll(void) { uint16_t i; for (i = 0; i < 24 * 80; ++i) vga[i] = vga[i + 80]; for (; i < 25 * 80; ++i) vga[i] = 0x0720; vga_y = 24; }
static void vga_put(char ch) { if (ch == '\n') { vga_x = 0; ++vga_y; } else if (ch == '\r') vga_x = 0; else if (ch == '\b') { if (vga_x) --vga_x; vga[vga_y * 80 + vga_x] = 0x0720; } else { vga[vga_y * 80 + vga_x] = 0x0700 | (uint8_t)ch; if (++vga_x == 80) { vga_x = 0; ++vga_y; } } if (vga_y >= 25) vga_scroll(); vga_cursor(); }

void quanta_arch_write_marker(const char *marker) {
    while (*marker) {
        while ((inb(0x3fd) & 0x20) == 0) { }
        outb(0x3f8, (uint8_t)*marker); vga_put(*marker++);
    }
}

void quanta_arch_console_write(const char *text, uint64_t length) {
    while (length--) {
        while ((inb(0x3fd) & 0x20) == 0) { }
        outb(0x3f8, (uint8_t)*text); vga_put(*text++);
    }
}

int quanta_arch_disk_read(uint32_t lba, void *buffer) {
    uint16_t *output = (uint16_t *)buffer;
    uint16_t index;
    if (memory_storage != 0 && (uint64_t)lba < memory_storage_sectors) {
        const uint8_t *input = memory_storage + (uint64_t)lba * 512U;
        uint8_t *bytes = buffer;
        for (index = 0; index < 512U; ++index) bytes[index] = input[index];
        return 0;
    }
    if (buffer == 0 || ata_select(lba) != 0) return -1;
    for (index = 0; index < 256; ++index) output[index] = inw(0x1f0);
    return 0;
}

int quanta_arch_disk_write(uint32_t lba, const void *buffer) {
    const uint16_t *input = (const uint16_t *)buffer;
    uint16_t index;
    if (buffer == 0 || memory_storage != 0) return -1;
    outb(0x1f6, (uint8_t)(0xe0U | ((lba >> 24) & 0x0fU))); outb(0x1f2, 1);
    outb(0x1f3, (uint8_t)lba); outb(0x1f4, (uint8_t)(lba >> 8));
    outb(0x1f5, (uint8_t)(lba >> 16)); outb(0x1f7, 0x30);
    if (ata_wait(0x88, 0x08) != 0) return -1;
    for (index = 0; index < 256; ++index) outw(0x1f0, input[index]);
    outb(0x1f7, 0xe7);
    return ata_wait(0x80, 0);
}

void quanta_arch_console_clear(void) { uint16_t i; for (i = 0; i < 80 * 25; ++i) vga[i] = 0x0720; vga_x = vga_y = 0; vga_cursor(); }
void quanta_arch_console_initialize(void) { quanta_arch_console_clear(); quanta_arch_write_marker("QUANTA_VGA_READY\nQUANTA_PS2_READY\n"); }

static int keycode(uint8_t code) {
    static const char normal[59] = "\000\0331234567890-=\b\tqwertyuiop[]\n\000asdfghjkl;'`\000\\zxcvbnm,./\000*\000 ";
    static const char shifted[59] = "\000\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\000ASDFGHJKL:\"~\000|ZXCVBNM<>?\000*\000 ";
    if (code >= 58) return -1; return (shift_down ? shifted : normal)[code];
}
int quanta_arch_console_read_char(void) {
    if (inb(0x64) & 1) { uint8_t code = inb(0x60); if (code == 0x2a || code == 0x36) { shift_down = 1; return -1; } if (code == 0xaa || code == 0xb6) { shift_down = 0; return -1; } if ((code & 0x80) == 0) return keycode(code); }
    return quanta_arch_serial_read_char();
}

static uint8_t bcd(uint8_t value) { return (uint8_t)((value & 15) + (value >> 4) * 10); }
void quanta_arch_read_rtc(uint8_t *year, uint8_t *month, uint8_t *day, uint8_t *hour, uint8_t *minute, uint8_t *second) {
    uint8_t status, h; outb(0x70, 0x0b); status = inb(0x71); outb(0x70, 0); *second = inb(0x71); outb(0x70, 2); *minute = inb(0x71); outb(0x70, 4); h = inb(0x71); outb(0x70, 7); *day = inb(0x71); outb(0x70, 8); *month = inb(0x71); outb(0x70, 9); *year = inb(0x71); if (!(status & 4)) { *second=bcd(*second); *minute=bcd(*minute); h=bcd(h & 0x7f) | (h & 0x80); *day=bcd(*day); *month=bcd(*month); *year=bcd(*year); } if (!(status & 2) && (h & 0x80)) h = (uint8_t)(((h & 0x7f) + 12) % 24); *hour = h & 0x7f;
}

int quanta_arch_serial_read_char(void) {
    if ((inb(0x3fd) & 1) == 0) return -1;
    return inb(0x3f8);
}

void quanta_arch_shutdown(void) {
    outw(0x604, 0x2000); outw(0xb004, 0x2000);
    for (;;) __asm__ volatile("hlt");
}

static void wrmsr(uint32_t index, uint64_t value) { __asm__ volatile("wrmsr" : : "c"(index), "a"((uint32_t)value), "d"((uint32_t)(value >> 32))); }

void quanta_arch_configure_syscalls(void) {
    uint32_t low, high; uint64_t efer;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0xc0000080));
    efer = ((uint64_t)high << 32) | low;
    wrmsr(0xc0000080, efer | 1); /* EFER.SCE */
    wrmsr(0xc0000081, ((uint64_t)0x08 << 32) | ((uint64_t)0x13 << 48));
    extern void quanta_arch_syscall_entry(void);
    wrmsr(0xc0000082, (uint64_t)(uintptr_t)quanta_arch_syscall_entry);
    wrmsr(0xc0000084, (1ULL << 9));
}

void quanta_arch_initialize(uint64_t kernel_stack_top) {
    tss.rsp0 = kernel_stack_top;
    tss.io_map_base = sizeof(tss);
    gdt[1] = 0x00af9a000000ffffULL;
    gdt[2] = 0x00af92000000ffffULL;
    gdt[3] = 0x00aff2000000ffffULL;
    gdt[4] = 0x00affa000000ffffULL;
        uint64_t cr4;
        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= 1ULL << 9;
        __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
        uint64_t tss_address = (uint64_t)(uintptr_t)&tss;
    gdt[5] = 0x0000890000000000ULL | ((tss_address & 0xffffffULL) << 16) |
        ((tss_address & 0xff000000ULL) << 32) | ((uint64_t)(sizeof(tss) - 1));
    gdt[6] = tss_address >> 32;
    struct descriptor_pointer gdt_pointer = { sizeof(gdt) - 1, (uint64_t)(uintptr_t)gdt };
    quanta_arch_reload_gdt(&gdt_pointer);
    set_gate(0x80, quanta_arch_int80_stub, 0xee);
    set_gate(6, quanta_arch_fault_stub, 0x8e);
    set_gate(13, quanta_arch_fault_stub, 0x8e);
    set_gate(14, quanta_arch_fault_stub, 0x8e);
    struct descriptor_pointer idt_pointer = { sizeof(idt) - 1, (uint64_t)(uintptr_t)idt };
    __asm__ volatile("lidt %0" : : "m"(idt_pointer));
}

uint64_t quanta_arch_read_cr3(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}
