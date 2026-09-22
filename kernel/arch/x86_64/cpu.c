void quanta_arch_halt(void) {
    __asm__ volatile("hlt");
}
