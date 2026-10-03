; Physical addresses reserved by the bootstrap loader. All are identity mapped.
%define MEMORY_MAP          0x5000
%define BOOT_INFO           0x9000
%define BIOS_FONT           0x6000
%define VBE_MODE_INFO       0x7100
%define KERNEL_BUFFER       0x20000
%define PML4                0x100000
%define PDPT                0x101000
%define PD_LOW              0x102000
%define PD_HIGH             0x103000
%define PT_KERNEL           0x104000
%define PT_KERNEL2          0x105000

%define KERNEL_CODE_32      0x08
%define KERNEL_DATA_32      0x10
%define KERNEL_CODE_64      0x18
%define PAGE_PRESENT_WRITE  0x03
%define PAGE_LARGE          0x83
%define KERNEL_PHYSICAL     0x00200000
%define KERNEL_PAGE_OFFSET  (256 * 8)
%define ISO_STORAGE_PHYSICAL 0x00300000
%define ISO_STORAGE_TEMP     0x00070000
%define VBE_MODE            0x0118
%define VBE_MODE_LFB        0x4118
%define FRAMEBUFFER_VIRTUAL 0x40000000
