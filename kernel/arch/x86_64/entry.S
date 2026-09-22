.section .text
.global quanta_kernel_entry
.extern quanta_kernel_main
quanta_kernel_entry:
    lea quanta_boot_stack_end(%rip), %rsp
    xor %rbp, %rbp
    call quanta_kernel_main
1:  hlt
    jmp 1b

.section .bss
.align 16
quanta_boot_stack:
    .skip 16384
quanta_boot_stack_end:
