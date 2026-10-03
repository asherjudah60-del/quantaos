.section .text
.global quanta_arch_reload_gdt
.global quanta_arch_int80_stub
.global quanta_arch_enter_user
.global quanta_arch_syscall_entry
.global quanta_arch_timer_stub
.global quanta_arch_exception_stubs
.extern quanta_arch_syscall_dispatch
.extern quanta_arch_timer_interrupt
.extern quanta_arch_exception_dispatch

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
    swapgs
    mov %rsp, %gs:0
    mov %gs:8, %rsp
    pushq %r11
    pushq %rcx
    pushq %r10
    pushq %rdx
    pushq %rsi
    pushq %rdi
    cld
    mov %rax, %rdi
    mov 0(%rsp), %rsi
    mov 8(%rsp), %rdx
    mov 16(%rsp), %rcx
    mov 24(%rsp), %r8
    mov 32(%rsp), %r9
    subq $16, %rsp
    mov 56(%rsp), %rax
    mov %rax, (%rsp)
    call quanta_arch_syscall_dispatch
    addq $16, %rsp
    addq $32, %rsp
    popq %rcx
    popq %r11
    movabsq $0x0000800000000000, %r10
    cmpq %r10, %rcx
    jae .Lsyscall_bad_return
    andq $0x240fd5, %r11
    orq $0x2, %r11
    mov %gs:0, %rsp
    swapgs
    sysretq
.Lsyscall_bad_return:
    movq $13, %rdi
    xorq %rsi, %rsi
    movq $0x1b, %rdx
    swapgs
    call quanta_arch_exception_dispatch
1:  hlt
    jmp 1b

.macro EXCEPTION_NO_ERROR number
.Lquanta_exception_\number:
    pushq $0
    pushq $\number
    jmp .Lquanta_exception_common
.endm

.macro EXCEPTION_ERROR number
.Lquanta_exception_\number:
    pushq $\number
    jmp .Lquanta_exception_common
.endm

EXCEPTION_NO_ERROR 0
EXCEPTION_NO_ERROR 1
EXCEPTION_NO_ERROR 2
EXCEPTION_NO_ERROR 3
EXCEPTION_NO_ERROR 4
EXCEPTION_NO_ERROR 5
EXCEPTION_NO_ERROR 6
EXCEPTION_NO_ERROR 7
EXCEPTION_ERROR 8
EXCEPTION_NO_ERROR 9
EXCEPTION_ERROR 10
EXCEPTION_ERROR 11
EXCEPTION_ERROR 12
EXCEPTION_ERROR 13
EXCEPTION_ERROR 14
EXCEPTION_NO_ERROR 15
EXCEPTION_NO_ERROR 16
EXCEPTION_ERROR 17
EXCEPTION_NO_ERROR 18
EXCEPTION_NO_ERROR 19
EXCEPTION_NO_ERROR 20
EXCEPTION_ERROR 21
EXCEPTION_NO_ERROR 22
EXCEPTION_NO_ERROR 23
EXCEPTION_NO_ERROR 24
EXCEPTION_NO_ERROR 25
EXCEPTION_NO_ERROR 26
EXCEPTION_NO_ERROR 27
EXCEPTION_NO_ERROR 28
EXCEPTION_ERROR 29
EXCEPTION_ERROR 30
EXCEPTION_NO_ERROR 31

.Lquanta_exception_common:
    movq 0(%rsp), %rdi
    movq 8(%rsp), %rsi
    movq 24(%rsp), %rdx
    cld
    andq $-16, %rsp
    call quanta_arch_exception_dispatch
1:  hlt
    jmp 1b

quanta_arch_timer_stub:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %rbx
    andq $-16, %rsp
    cld
    call quanta_arch_timer_interrupt
    movq %rbx, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    iretq

quanta_arch_enter_user:
    mov %rdi, %cr3
    pushq $0x1b
    pushq %rdx
    pushq $0x202
    pushq $0x23
    pushq %rsi
    iretq

.section .rodata
.align 8
quanta_arch_exception_stubs:
    .quad .Lquanta_exception_0, .Lquanta_exception_1
    .quad .Lquanta_exception_2, .Lquanta_exception_3
    .quad .Lquanta_exception_4, .Lquanta_exception_5
    .quad .Lquanta_exception_6, .Lquanta_exception_7
    .quad .Lquanta_exception_8, .Lquanta_exception_9
    .quad .Lquanta_exception_10, .Lquanta_exception_11
    .quad .Lquanta_exception_12, .Lquanta_exception_13
    .quad .Lquanta_exception_14, .Lquanta_exception_15
    .quad .Lquanta_exception_16, .Lquanta_exception_17
    .quad .Lquanta_exception_18, .Lquanta_exception_19
    .quad .Lquanta_exception_20, .Lquanta_exception_21
    .quad .Lquanta_exception_22, .Lquanta_exception_23
    .quad .Lquanta_exception_24, .Lquanta_exception_25
    .quad .Lquanta_exception_26, .Lquanta_exception_27
    .quad .Lquanta_exception_28, .Lquanta_exception_29
    .quad .Lquanta_exception_30, .Lquanta_exception_31
