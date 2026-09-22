ENTRY(quanta_kernel_entry)

SECTIONS {
    PROVIDE_HIDDEN(KERNEL_VMA = 0xffffffff80100000);
    PROVIDE_HIDDEN(KERNEL_PHYSICAL = 0x00200000);
    . = KERNEL_VMA;
    PROVIDE_HIDDEN(_kernel_start = .);
    .text : AT(ADDR(.text) - KERNEL_VMA + KERNEL_PHYSICAL) { *(.text .text.*) }
    .rodata : AT(ADDR(.rodata) - KERNEL_VMA + KERNEL_PHYSICAL) { *(.rodata .rodata.*) }
    .data : AT(ADDR(.data) - KERNEL_VMA + KERNEL_PHYSICAL) { *(.data .data.*) }
    .bss : AT(ADDR(.bss) - KERNEL_VMA + KERNEL_PHYSICAL) { *(COMMON) *(.bss .bss.*) }
    PROVIDE_HIDDEN(_kernel_end = .);
}
