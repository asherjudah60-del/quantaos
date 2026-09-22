bits 64
%define IPC_CALL 1
%define TASK_EXIT 4
; Handle 1 is an explicit send-only grant, not ambient authority.
    mov rax, IPC_CALL
    mov rdi, 1
    lea rsi, [rel message]
    lea rdx, [rel payload]
    mov r10, 5
    syscall
    mov rax, TASK_EXIT
    xor rdi, rdi
    syscall
.halt: pause
    jmp .halt
message: times 56 db 0
payload: db 'ping', 0
