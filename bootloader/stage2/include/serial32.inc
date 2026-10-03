serial_puts32:
    push ecx
.next:
    lodsb
    test al, al
    jz .done
    push eax
    mov ecx, 0xffff
.wait:
    mov dx, 0x3fd
    in al, dx
    test al, 0x20
    jnz .ready
    loop .wait
    pop eax
    jmp .next
.ready:
    pop eax
    mov dx, 0x3f8
    out dx, al
    jmp .next
.done:
    pop ecx
    ret
