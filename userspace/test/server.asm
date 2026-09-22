bits 64
%define IPC_RECEIVE 2
%define IPC_REPLY 3
    mov rax, IPC_RECEIVE
    mov rdi, 2
    xor rsi, rsi
    xor rdx, rdx
    xor r10, r10
    syscall
    mov rax, IPC_REPLY
    mov rdi, 2
    xor rsi, rsi
    xor rdx, rdx
    xor r10, r10
    syscall
.halt: pause
    jmp .halt
