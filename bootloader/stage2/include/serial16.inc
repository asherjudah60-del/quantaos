serial_puts:
    lodsb
    test al, al
    jz .done
    push ax
.wait:
    mov dx, 0x3fd
    in al, dx
    test al, 0x20
    jz .wait
    pop ax
    mov dx, 0x3f8
    out dx, al
    jmp serial_puts
.done:
    ret
