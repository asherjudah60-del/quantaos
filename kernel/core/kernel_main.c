#include <stdint.h>
#include <quanta/boot_info.h>
#include <quanta/arch.h>
#include <quanta/memory.h>
#include <quanta/task.h>
extern void quanta_task_bootstrap(void) __attribute__((noreturn));
static uint8_t kernel_stack[16384] __attribute__((aligned(16)));

void quanta_kernel_main(const struct quanta_boot_info *boot_info) {
    quanta_arch_initialize((uint64_t)(uintptr_t)(kernel_stack + sizeof(kernel_stack)));
    quanta_arch_storage_initialize(boot_info);
    quanta_memory_initialize(boot_info);
    quanta_arch_write_marker("QUANTA_KERNEL_READY\n");
    quanta_arch_console_initialize();

    quanta_task_bootstrap();
}
