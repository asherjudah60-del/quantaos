serial_puts32:
    lodsb
    test al, al
    jz .done
    push eax
.wait:
    mov dx, 0x3fd
    in al, dx
    test al, 0x20
    jz .wait
    pop eax
    mov dx, 0x3f8
    out dx, al
    jmp serial_puts32
.done:
    ret
