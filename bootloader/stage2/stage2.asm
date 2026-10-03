; Stage 2 owns BIOS discovery, kernel ELF loading, paging, and boot-info production.
bits 16
org 0x8000
%include "layout.inc"
%include "boot_info.inc"
%include "constants.inc"

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov [boot_drive], dl
    call enable_a20
    mov si, stage2_ready
    call serial_puts
    call collect_e820
    jc e820_error
    call load_kernel
    jc kernel_read_error
    call initialize_vbe
    lgdt [gdt32_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp KERNEL_CODE_32:protected_entry

e820_error: mov si, error_e820
    jmp boot_error
kernel_read_error: mov si, error_kernel_read
boot_error:
    call serial_puts
.halt: hlt
    jmp .halt

enable_a20:
    in al, 0x92
    or al, 2
    and al, 0xfe
    out 0x92, al
    call test_a20
    jnc .enabled
    call enable_a20_8042
    jc .failed
    call test_a20
    ret
.enabled:
    clc
    ret
.failed:
    stc
    ret

initialize_vbe:
    mov byte [vbe_available], 0
    mov ax, 0x4f01
    mov cx, VBE_MODE
    xor bx, bx
    mov es, bx
    mov di, VBE_MODE_INFO
    int 0x10
    cmp ax, 0x004f
    jne .fallback
    mov ax, [es:VBE_MODE_INFO]
    and ax, 0x0091
    cmp ax, 0x0091
    jne .fallback
    mov al, [es:VBE_MODE_INFO + 25]
    cmp al, 24
    je .pixel_size_valid
    cmp al, 32
    jne .fallback
.pixel_size_valid:
    mov [vbe_bpp], al
    shr al, 3
    movzx edx, al
    cmp byte [es:VBE_MODE_INFO + 27], 6
    jne .fallback
    cmp byte [es:VBE_MODE_INFO + 31], 8
    jne .fallback
    cmp byte [es:VBE_MODE_INFO + 32], 16
    jne .fallback
    cmp byte [es:VBE_MODE_INFO + 33], 8
    jne .fallback
    cmp byte [es:VBE_MODE_INFO + 34], 8
    jne .fallback
    cmp byte [es:VBE_MODE_INFO + 35], 8
    jne .fallback
    cmp byte [es:VBE_MODE_INFO + 36], 0
    jne .fallback
    movzx eax, word [es:VBE_MODE_INFO + 18]
    cmp eax, 640
    jb .fallback
    mov [vbe_width], eax
    movzx ebx, word [es:VBE_MODE_INFO + 20]
    cmp ebx, 480
    jb .fallback
    mov [vbe_height], ebx
    movzx ecx, word [es:VBE_MODE_INFO + 16]
    mov [vbe_pitch], ecx
    imul edx, eax
    cmp ecx, edx
    jb .fallback
    imul ecx, ebx
    jo .fallback
    cmp ecx, 0x01000000
    ja .fallback
    mov eax, [es:VBE_MODE_INFO + 40]
    test eax, eax
    jz .fallback
    mov [vbe_physical], eax
    add eax, ecx
    jc .fallback
    cmp byte [vbe_bpp], 32
    jne .format_bgr888
    mov byte [vbe_format], QUANTA_FRAMEBUFFER_FORMAT_BGRX8888
    jmp .copy_font
.format_bgr888:
    mov byte [vbe_format], QUANTA_FRAMEBUFFER_FORMAT_BGR888

.copy_font:
    ; Retain the BIOS 8x16 ROM font for the kernel's first framebuffer console.
    push ds
    push es
    mov ax, 0x1130
    mov bh, 0x06
    int 0x10
    push es
    push bp
    mov ax, es
    mov ds, ax
    mov si, bp
    xor ax, ax
    mov es, ax
    mov di, BIOS_FONT
    mov cx, 2048
    rep movsw
    pop bp
    add sp, 2
    pop es
    pop ds

    mov ax, 0x4f02
    mov bx, VBE_MODE_LFB
    int 0x10
    cmp ax, 0x004f
    jne .fallback
    mov byte [vbe_available], 1
    mov si, vbe_ready
    call serial_puts
.fallback:
    ret

test_a20:
    push ax
    push bx
    push es
    xor ax, ax
    mov es, ax
    mov bl, [es:0x0500]
    mov ax, 0xffff
    mov es, ax
    mov bh, [es:0x0510]
    xor ax, ax
    mov es, ax
    mov byte [es:0x0500], 0
    mov ax, 0xffff
    mov es, ax
    mov byte [es:0x0510], 0xff
    xor ax, ax
    mov es, ax
    cmp byte [es:0x0500], 0
    jne .disabled
    mov ax, 0xffff
    mov es, ax
    cmp byte [es:0x0510], 0xff
    jne .disabled
    mov byte [a20_result], 1
    jmp .restore
.disabled:
    mov byte [a20_result], 0
.restore:
    mov ax, 0xffff
    mov es, ax
    mov [es:0x0510], bh
    xor ax, ax
    mov es, ax
    mov [es:0x0500], bl
    cmp byte [a20_result], 0
    pop es
    pop bx
    pop ax
    jne .enabled
    stc
    ret
.enabled:
    clc
    ret

enable_a20_8042:
    push ax
    push bx
    call wait_8042_input_empty
    jc .failed
    mov al, 0xad
    out 0x64, al
    call wait_8042_input_empty
    jc .failed
    mov al, 0xd0
    out 0x64, al
    call wait_8042_output_full
    jc .failed
    in al, 0x60
    or al, 2
    mov bl, al
    call wait_8042_input_empty
    jc .failed
    mov al, 0xd1
    out 0x64, al
    call wait_8042_input_empty
    jc .failed
    mov al, bl
    out 0x60, al
    call wait_8042_input_empty
    jc .failed
    mov al, 0xae
    out 0x64, al
    clc
    jmp .done
.failed:
    mov al, 0xae
    out 0x64, al
    stc
.done:
    pop bx
    pop ax
    ret

wait_8042_input_empty:
    mov cx, 0xffff
.wait:
    in al, 0x64
    test al, 2
    jz .ready
    loop .wait
    stc
    ret
.ready:
    clc
    ret

wait_8042_output_full:
    mov cx, 0xffff
.wait:
    in al, 0x64
    test al, 1
    jnz .ready
    loop .wait
    stc
    ret
.ready:
    clc
    ret

%include "serial16.inc"

collect_e820:
    xor ebx, ebx
    mov di, MEMORY_MAP
    xor bp, bp
.next:
    cmp bp, BOOT_MAX_MEMORY_REGIONS
    jae .done
    mov eax, 0xe820
    mov edx, 0x534d4150
    mov ecx, BOOT_MEMORY_REGION_SIZE
    int 0x15
    jc .failed
    cmp eax, 0x534d4150
    jne .failed
    add di, BOOT_MEMORY_REGION_SIZE
    inc bp
    test ebx, ebx
    jnz .next
.done:
    mov [memory_entries], bp
    clc
    ret
.failed:
    stc
    ret

load_kernel:
%ifdef ISO_BOOT
    ; The firmware loaded only stage1 + pad + stage2.  Read aligned payload
    ; blocks from the El Torito boot-image extent recorded by stage1 at 7b00.
    ; El Torito LBAs are 2048-byte blocks; an isohybrid USB exposes 512-byte
    ; blocks, so convert after querying EDD geometry.
    call iso_sector_multiplier
    jc .read_failed
    mov eax, [0x7b00]
    mul word [iso_multiplier]
    movzx ecx, word [iso_multiplier]
    imul ecx, ISO_LOADER_BLOCKS
    add eax, ecx
    mov [dap_iso + 8], eax
    mov dword [dap_iso + 12], 0
    mov ax, ISO_KERNEL_BLOCKS
    mul word [iso_multiplier]
    mov [dap_iso + 2], ax
    mov [iso_remaining_blocks], ax
    mov word [dap_iso + 4], 0
    mov word [dap_iso + 6], KERNEL_BUFFER >> 4
    call iso_read_blocks
    jc .read_failed
    mov eax, [dap_iso + 8]
    movzx ecx, word [dap_iso + 2]
    add eax, ecx
    mov [dap_iso + 8], eax
    mov ax, ISO_STORAGE_BLOCKS
    mul word [iso_multiplier]
    mov [dap_iso + 2], ax
    mov [iso_remaining_blocks], ax
    mov word [dap_iso + 4], 0
    mov word [dap_iso + 6], ISO_STORAGE_TEMP >> 4
    call iso_read_blocks
    jc .read_failed
    clc
    ret
.read_failed:
    stc
    ret
%else
    mov word [dap_remaining], KERNEL_SECTORS
    mov word [dap_kernel + 4], 0
    mov word [dap_kernel + 6], 0x2000
    mov dword [dap_kernel + 8], KERNEL_LBA
    mov dword [dap_kernel + 12], 0
.read_chunk:
    cmp word [dap_remaining], 0
    je .read_done
    mov ax, [dap_remaining]
    cmp ax, 64
    jbe .chunk_size_ready
    mov ax, 64
.chunk_size_ready:
    mov [dap_kernel + 2], ax
    mov si, dap_kernel
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc .read_failed
    mov ax, [dap_kernel + 2]
    sub [dap_remaining], ax
    add [dap_kernel + 6], word 0x0800
    movzx eax, ax
    add dword [dap_kernel + 8], eax
    jmp .read_chunk
.read_done:
    clc
    ret
.read_failed:
    stc
    ret
%endif

%ifdef ISO_BOOT
iso_sector_multiplier:
    mov word [edd_parameters], 0x1e
    mov si, edd_parameters
    mov dl, [boot_drive]
    mov ah, 0x48
    int 0x13
    jc .failed
    mov ax, [edd_parameters + 24]
    cmp ax, 2048
    je .cdrom
    cmp ax, 512
    jne .failed
    mov word [iso_multiplier], 4
    clc
    ret
.cdrom:
    mov word [iso_multiplier], 1
    clc
    ret
.failed:
    stc
    ret

; BIOS DAP transfers cannot cross a 64 KiB real-mode segment.  Read no more
; than 31 physical blocks at a time and advance the segment and LBA together.
iso_read_blocks:
.next:
    mov ax, [dap_iso + 2]
    test ax, ax
    jz .done
    cmp ax, 31
    jbe .chunk_ready
    mov ax, 31
.chunk_ready:
    mov [iso_chunk_blocks], ax
    mov [dap_iso + 2], ax
    mov si, dap_iso
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc .failed
    mov ax, [iso_chunk_blocks]
    mov cx, ax
    shl cx, 7
    add [dap_iso + 6], cx
    movzx eax, ax
    add dword [dap_iso + 8], eax
    mov ax, [iso_remaining_blocks]
    sub ax, [iso_chunk_blocks]
    mov [iso_remaining_blocks], ax
    mov [dap_iso + 2], ax
    jmp .next
.done:
    clc
    ret
.failed:
    stc
    ret
%endif

bits 32
protected_entry:
    mov ax, KERNEL_DATA_32
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x7c00
%ifdef ISO_BOOT
    mov esi, ISO_STORAGE_TEMP
    mov edi, ISO_STORAGE_PHYSICAL
    mov ecx, STORAGE_SECTORS * 128
    rep movsd
%endif
    call initialize_boot_info
    call load_elf_segments
    jc elf_error
    call build_page_tables
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    mov eax, PML4
    mov cr3, eax
    mov ecx, 0xc0000080
    rdmsr
    or eax, (1 << 8) | (1 << 11)
    wrmsr
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    jmp KERNEL_CODE_64:long_mode_entry

elf_error:
    call serial_puts32
.halt: hlt
    jmp .halt

initialize_boot_info:
    mov edi, BOOT_INFO
    xor eax, eax
    mov ecx, BOOT_INFO_SIZE / 4
    rep stosd
    mov dword [BOOT_INFO], BOOT_INFO_MAGIC_LOW
    mov dword [BOOT_INFO + 4], BOOT_INFO_MAGIC_HIGH
    mov dword [BOOT_INFO + 8], BOOT_INFO_VERSION
    mov dword [BOOT_INFO + 12], BOOT_INFO_SIZE
    cmp byte [vbe_available], 0
    je .no_framebuffer
    mov eax, [vbe_physical]
    mov [BOOT_INFO + BOOT_INFO_FRAMEBUFFER_PHYS], eax
    mov eax, [vbe_width]
    mov [BOOT_INFO + BOOT_INFO_FRAMEBUFFER_WIDTH], eax
    mov eax, [vbe_height]
    mov [BOOT_INFO + BOOT_INFO_FRAMEBUFFER_HEIGHT], eax
    mov eax, [vbe_pitch]
    mov [BOOT_INFO + BOOT_INFO_FRAMEBUFFER_PITCH], eax
    mov al, [vbe_bpp]
    mov [BOOT_INFO + BOOT_INFO_FRAMEBUFFER_BPP], al
    mov al, [vbe_format]
    mov [BOOT_INFO + BOOT_INFO_FRAMEBUFFER_FORMAT], al
.no_framebuffer:
%ifdef ISO_BOOT
    mov dword [BOOT_INFO + BOOT_INFO_STORAGE_PHYSICAL], ISO_STORAGE_PHYSICAL
    mov dword [BOOT_INFO + BOOT_INFO_STORAGE_SECTORS], STORAGE_SECTORS
%endif
    mov dword [BOOT_INFO + BOOT_INFO_MMAP_PHYSICAL], MEMORY_MAP
    movzx eax, word [memory_entries]
    mov [BOOT_INFO + BOOT_INFO_MMAP_ENTRIES], eax
    ret

load_elf_segments:
    cmp dword [KERNEL_BUFFER], 0x464c457f
    jne .bad_magic
    cmp byte [KERNEL_BUFFER + 4], 2
    jne .bad_class
    cmp word [KERNEL_BUFFER + 18], 0x3e
    jne .bad_machine
    cmp word [KERNEL_BUFFER + 54], 56
    jne .bad_headers
    mov eax, [KERNEL_BUFFER + 32]
    movzx ebx, word [KERNEL_BUFFER + 56]
    imul ebx, 56
    add eax, ebx
    jc .bad
    cmp eax, KERNEL_BYTES
    ja .bad
    mov eax, [KERNEL_BUFFER + 32]
    add eax, KERNEL_BUFFER
    mov [program_headers], eax
    mov ax, [KERNEL_BUFFER + 56]
    mov [remaining_segments], ax
    movzx ebp, word [KERNEL_BUFFER + 54]
    mov eax, [KERNEL_BUFFER + 24]
    mov edx, [KERNEL_BUFFER + 28]
    mov [kernel_entry], eax
    mov [kernel_entry + 4], edx
.next:
    cmp word [remaining_segments], 0
    jz .good
    mov edx, [program_headers]
    cmp dword [edx], 1
    jne .skip
    mov esi, [edx + 8]
    mov eax, esi
    add eax, [edx + 32]
    jc .bad
    cmp eax, KERNEL_BYTES
    ja .bad
    add esi, KERNEL_BUFFER
    mov edi, [edx + 24]
    mov eax, [edx + 32]
    cmp eax, [edx + 40]
    ja .bad
    mov ebx, [edx + 40]
    mov ecx, eax
    rep movsb
    sub ebx, eax
    mov ecx, ebx
    xor eax, eax
    rep stosb
.skip:
    add dword [program_headers], ebp
    dec word [remaining_segments]
    jmp .next
.good:
    clc
    ret
.bad:
    mov esi, error_elf_segment
    stc
    ret
.bad_magic: mov esi, error_elf_magic
    stc
    ret
.bad_class: mov esi, error_elf_class
    stc
    ret
.bad_machine: mov esi, error_elf_machine
    stc
    ret
.bad_headers: mov esi, error_elf_headers
    stc
    ret

build_page_tables:
    mov edi, PML4
    xor eax, eax
    mov ecx, (6 * 4096) / 4
    rep stosd
    mov dword [PML4], PDPT | PAGE_PRESENT_WRITE
    mov dword [PML4 + 511 * 8], PDPT | PAGE_PRESENT_WRITE
    mov dword [PDPT], PD_LOW | PAGE_PRESENT_WRITE
    mov dword [PDPT + 510 * 8], PD_HIGH | PAGE_PRESENT_WRITE
    mov edi, PD_LOW
    mov eax, PAGE_LARGE
    mov ecx, 512
.identity:
    mov [edi], eax
    mov dword [edi + 4], 0
    add eax, 0x200000
    add edi, 8
    loop .identity
    mov dword [PD_HIGH], PT_KERNEL | PAGE_PRESENT_WRITE
    mov dword [PD_HIGH + 8], PT_KERNEL2 | PAGE_PRESENT_WRITE
    mov edi, PT_KERNEL + KERNEL_PAGE_OFFSET
    mov eax, KERNEL_PHYSICAL | PAGE_PRESENT_WRITE
    mov ecx, 256
.kernel:
    mov [edi], eax
    mov dword [edi + 4], 0
    add eax, 0x1000
    add edi, 8
    loop .kernel
    mov edi, PT_KERNEL2
    mov eax, KERNEL_PHYSICAL + (256 * 0x1000) | PAGE_PRESENT_WRITE
    mov ecx, 256
.kernel2:
    mov [edi], eax
    mov dword [edi + 4], 0
    add eax, 0x1000
    add edi, 8
    loop .kernel2
    ret

%include "serial32.inc"

bits 64
long_mode_entry:
    mov esi, long_mode_ready
    call serial_puts64
    mov rdi, BOOT_INFO
    mov rax, [abs kernel_entry]
    jmp rax
%include "serial64.inc"

align 8
gdt32:
    dq 0
    dq 0x00cf9a000000ffff
    dq 0x00cf92000000ffff
    dq 0x00af9a000000ffff
gdt32_end:
gdt32_descriptor:
    dw gdt32_end - gdt32 - 1
    dd gdt32
boot_drive: db 0
a20_result: db 0
vbe_available: db 0
vbe_bpp: db 0
vbe_format: db 0
vbe_physical: dd 0
vbe_width: dd 0
vbe_height: dd 0
vbe_pitch: dd 0
memory_entries: dw 0
remaining_segments: dw 0
program_headers: dd 0
kernel_entry: dq 0
%ifdef ISO_BOOT
iso_multiplier: dw 0
iso_remaining_blocks: dw 0
iso_chunk_blocks: dw 0
align 4
dap_iso:
    db 0x10, 0
    dw 0
    dw 0, 0
    dq 0
edd_parameters: times 30 db 0
%endif
stage2_ready: db 'QUANTA_BOOT_STAGE2_READY', 10, 0
vbe_ready: db 'QUANTA_VBE_READY', 10, 0
long_mode_ready: db 'QUANTA_LONG_MODE_READY', 10, 0
error_e820: db 'QUANTA_BOOT_ERROR_E820', 10, 0
error_kernel_read: db 'QUANTA_BOOT_ERROR_KERNEL_READ', 10, 0
error_elf: db 'QUANTA_BOOT_ERROR_ELF', 10, 0
error_elf_magic: db 'QUANTA_BOOT_ERROR_ELF_MAGIC', 10, 0
error_elf_class: db 'QUANTA_BOOT_ERROR_ELF_CLASS', 10, 0
error_elf_machine: db 'QUANTA_BOOT_ERROR_ELF_MACHINE', 10, 0
error_elf_headers: db 'QUANTA_BOOT_ERROR_ELF_HEADERS', 10, 0
error_elf_segment: db 'QUANTA_BOOT_ERROR_ELF_SEGMENT', 10, 0
align 4
dap_kernel:
    db 0x10, 0
    dw KERNEL_SECTORS
    dw 0x0000, 0x2000
    dq KERNEL_LBA
dap_remaining: dw 0
