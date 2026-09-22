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
    out 0x92, al
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
    mov esi, 0x7c00 + 1024 + (STAGE2_SECTORS * 512)
    mov edi, KERNEL_BUFFER
    mov ecx, KERNEL_SECTORS * 128
    a32 rep movsd
    mov esi, 0x7c00 + 1024 + ((STAGE2_SECTORS + KERNEL_SECTORS) * 512)
    mov edi, ISO_STORAGE_PHYSICAL
    mov ecx, STORAGE_SECTORS * 128
    a32 rep movsd
    clc
    ret
%else
    mov si, dap_kernel
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    ret
%endif

bits 32
protected_entry:
    mov ax, KERNEL_DATA_32
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x7c00
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
memory_entries: dw 0
remaining_segments: dw 0
program_headers: dd 0
kernel_entry: dq 0
stage2_ready: db 'QUANTA_BOOT_STAGE2_READY', 10, 0
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
