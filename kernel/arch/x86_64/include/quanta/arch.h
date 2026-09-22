#ifndef QUANTA_ARCH_X86_64_H
#define QUANTA_ARCH_X86_64_H

#include <stdint.h>

struct quanta_boot_info;

void quanta_arch_initialize(uint64_t kernel_stack_top);
void quanta_arch_storage_initialize(const struct quanta_boot_info *boot_info);
uint64_t quanta_arch_storage_physical(void);
uint64_t quanta_arch_storage_sectors(void);
void quanta_arch_write_marker(const char *marker);
void quanta_arch_console_write(const char *text, uint64_t length);
int quanta_arch_serial_read_char(void);
void quanta_arch_console_initialize(void);
int quanta_arch_console_read_char(void);
void quanta_arch_console_clear(void);
void quanta_arch_read_rtc(uint8_t *year, uint8_t *month, uint8_t *day,
    uint8_t *hour, uint8_t *minute, uint8_t *second);
int quanta_arch_disk_read(uint32_t lba, void *buffer);
int quanta_arch_disk_write(uint32_t lba, const void *buffer);
void quanta_arch_shutdown(void) __attribute__((noreturn));
uint64_t quanta_arch_read_cr3(void);
void quanta_arch_enter_user(uint64_t page_root, uint64_t instruction_pointer,
    uint64_t stack_pointer, uint64_t argc, uint64_t argv) __attribute__((noreturn));
void quanta_arch_configure_syscalls(void);
void quanta_arch_write_marker(const char *marker);

#endif
