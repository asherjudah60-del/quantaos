.section .text
.global quanta_arch_reload_gdt
.global quanta_arch_int80_stub
.global quanta_arch_enter_user
.global quanta_arch_syscall_entry
.global quanta_arch_fault_stub
.extern quanta_arch_syscall_dispatch
.extern quanta_task_user_fault

quanta_arch_reload_gdt:
    lgdt (%rdi)
    pushq $0x08
    lea .gdt_loaded(%rip), %rax
    pushq %rax
    lretq
.gdt_loaded:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $0x28, %ax
    ltr %ax
    ret

quanta_arch_int80_stub:
    pushq %rax
    call quanta_arch_syscall_dispatch
    popq %rax
    iretq

quanta_arch_syscall_entry:
    /* Preserve all register arguments and the SYSRET return pair. */
    pushq %r11
    pushq %rcx
    pushq %r10
    pushq %rdx
    pushq %rsi
    pushq %rdi
    mov %rax, %rdi
    mov 0(%rsp), %rsi
    mov 8(%rsp), %rdx
    mov 16(%rsp), %rcx
    mov 24(%rsp), %r8
        mov 32(%rsp), %r9
        pushq 40(%rsp)
    call quanta_arch_syscall_dispatch
        addq $8, %rsp
        addq $32, %rsp
    popq %rcx
    popq %r11
    sysretq

quanta_arch_fault_stub:
    mov $6, %rdi
    call quanta_task_user_fault
1:  hlt
    jmp 1b

quanta_arch_enter_user:
    mov %rdi, %cr3
    pushq $0x1b
    pushq %rdx
    pushfq
    pushq $0x23
    pushq %rsi
    iretq
