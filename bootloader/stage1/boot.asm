; BIOS MBR: verify EDD, load contiguous stage 2 at 0000:8000, transfer control.
bits 16
org 0x7c00
%include "layout.inc"

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    mov [boot_drive], dl
    call serial_init
    mov si, stage1_ready
    call serial_puts
%ifdef ISO_BOOT
    mov dl, [boot_drive]
    jmp 0x0000:0x8000
%endif
    mov dl, [boot_drive]
    mov bx, 0x55aa
    mov ah, 0x41
    int 0x13
    jc disk_extensions_error
    cmp bx, 0xaa55
    jne disk_extensions_error
    test cx, 1
    jz disk_extensions_error
    mov si, dap_stage2
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc disk_read_error
    mov dl, [boot_drive]
    jmp 0x0000:0x8000

disk_extensions_error: mov si, error_edd
    jmp boot_error
disk_read_error: mov si, error_read
boot_error:
    call serial_puts
.halt: hlt
    jmp .halt

serial_init:
    mov dx, 0x3f9
    xor al, al
    out dx, al
    mov dx, 0x3fb
    mov al, 0x80
    out dx, al
    mov dx, 0x3f8
    mov al, 0x03
    out dx, al
    mov dx, 0x3f9
    xor al, al
    out dx, al
    mov dx, 0x3fb
    mov al, 0x03
    out dx, al
    ret
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
.done: ret

boot_drive: db 0
stage1_ready: db 'QUANTA_BOOT_STAGE1_READY', 10, 0
error_edd: db 'QUANTA_BOOT_ERROR_EDD', 10, 0
error_read: db 'QUANTA_BOOT_ERROR_STAGE2_READ', 10, 0
align 4
dap_stage2:
    db 0x10, 0
    dw STAGE2_SECTORS
    dw 0x8000, 0
    dq STAGE2_LBA
times 510-($-$$) db 0
dw 0xaa55
