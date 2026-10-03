serial_puts64:
    push rcx
.next:
    lodsb
    test al, al
    jz .done
    push rax
    mov ecx, 0xffff
.wait:
    mov dx, 0x3fd
    in al, dx
    test al, 0x20
    jnz .ready
    loop .wait
    pop rax
    jmp .next
.ready:
    pop rax
    mov dx, 0x3f8
    out dx, al
    jmp .next
.done:
    pop rcx
    ret
