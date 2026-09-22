serial_puts64:
    lodsb
    test al, al
    jz .done
    push rax
.wait:
    mov dx, 0x3fd
    in al, dx
    test al, 0x20
    jz .wait
    pop rax
    mov dx, 0x3f8
    out dx, al
    jmp serial_puts64
.done:
    ret
