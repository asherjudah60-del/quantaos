serial_puts:
    push cx
.next:
    lodsb
    test al, al
    jz .done
    push ax
    mov cx, 0xffff
.wait:
    mov dx, 0x3fd
    in al, dx
    test al, 0x20
    jnz .ready
    loop .wait
    pop ax
    jmp .next
.ready:
    pop ax
    mov dx, 0x3f8
    out dx, al
    jmp .next
.done:
    pop cx
    ret
