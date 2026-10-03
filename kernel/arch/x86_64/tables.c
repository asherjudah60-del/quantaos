#include <stdint.h>
#include <quanta/boot_info.h>
#include <quanta/syscall.h>
#include <quanta/arch.h>
#include <quanta/memory.h>

struct descriptor_pointer { uint16_t limit; uint64_t base; } __attribute__((packed));
struct task_state_segment {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} __attribute__((packed));
struct idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

extern void quanta_arch_reload_gdt(const struct descriptor_pointer *pointer);
extern void quanta_arch_int80_stub(void);
extern void (*quanta_arch_exception_stubs[32])(void);
extern void quanta_arch_enter_user(uint64_t root, uint64_t ip, uint64_t stack,
    uint64_t argc, uint64_t argv) __attribute__((noreturn));
extern void quanta_task_user_fault(uint64_t vector) __attribute__((noreturn));
extern const uint8_t _binary_build_assets_default_background_pal_start[];
extern const uint8_t _binary_build_assets_default_background_pal_end[];
extern const uint8_t _binary_build_assets_ubuntu_mono_8x16_bin_start[];
extern const uint8_t _binary_build_assets_ubuntu_mono_8x16_bin_end[];
extern const uint8_t _binary_build_assets_system_icons_rgba_start[];
extern const uint8_t _binary_build_assets_system_icons_rgba_end[];

static uint64_t gdt[7] __attribute__((aligned(16)));
static struct task_state_segment tss;
static struct idt_gate idt[256] __attribute__((aligned(16)));
static struct {
    uint64_t user_rsp;
    uint64_t kernel_rsp;
} syscall_state;
static volatile uint64_t timer_ticks;
static uint16_t *const vga = (uint16_t *)(uintptr_t)0xb8000;
static uint8_t vga_x, vga_y;
static uint8_t alt_down;
static uint8_t terminal_cells[80U * 25U];
static uint8_t framebuffer_workspace_visible;
static uint8_t *framebuffer;
static uint8_t *framebuffer_scanout;
static uint64_t framebuffer_bytes;
static uint32_t framebuffer_width, framebuffer_height, framebuffer_pitch;
static uint8_t framebuffer_bytes_per_pixel;
static uint8_t framebuffer_format;
static uint8_t bga_page_flip;
static uint32_t bga_front_y;
static uint8_t bga_flip_reported;
static uint8_t framebuffer_dirty;
static uint32_t dirty_left, dirty_top, dirty_right, dirty_bottom;
static uint8_t unsynced_present_reported;
static uint8_t framebuffer_font[4096];
static uint8_t shift_down;
static uint8_t mouse_packet[3];
static uint8_t mouse_packet_index;
static uint8_t mouse_buttons;
static uint8_t mouse_ready;
static uint32_t mouse_x, mouse_y;
static uint64_t framebuffer_bytes_written;
static uint8_t desktop_view;
static uint8_t desktop_selection;
static const uint8_t *memory_storage;
static uint64_t memory_storage_sectors;
struct ata_device_info {
    uint16_t io_base;
    uint16_t control_base;
    uint8_t drive;
    uint8_t present;
    uint64_t sectors;
};
static struct ata_device_info ata_devices[4];
static void probe_ata_devices(void);
static void framebuffer_fill_rect(uint32_t left, uint32_t top, uint32_t width,
    uint32_t height, uint32_t color);
static void framebuffer_draw_wallpaper(uint32_t left, uint32_t top,
    uint32_t width, uint32_t height);
static void framebuffer_show_workspace(void);
static void framebuffer_show_terminal(void);
static void serial_write_marker(const char *marker);
static void framebuffer_draw_text(uint32_t left, uint32_t top, const char *text,
    uint32_t color, uint32_t scale);

#define FRAMEBUFFER_VIRTUAL 0xffffffffc0000000ULL
#define FRAMEBUFFER_BACKBUFFER_VIRTUAL 0xffffffffc1000000ULL
#define FRAMEBUFFER_MAX_BYTES 0x01000000ULL
#define FRAMEBUFFER_BACKGROUND 0x00131a21U
#define FRAMEBUFFER_FOREGROUND 0x00e8edf2U
#define FRAMEBUFFER_TERMINAL_BACKGROUND 0x0017202eU
#define FRAMEBUFFER_TERMINAL_X 40U
#define FRAMEBUFFER_TERMINAL_Y 280U
#define FRAMEBUFFER_PROMPT_CYAN 0x0000a9dfU
#define FRAMEBUFFER_PROMPT_BLUE 0x00005bd8U
#define FRAMEBUFFER_PROMPT_GREEN 0x0000c5a8U
#define FRAMEBUFFER_PANEL_WIDTH 72U
#define FRAMEBUFFER_VIEW_WORKSPACE 0U
#define FRAMEBUFFER_VIEW_SETTINGS 1U
#define FRAMEBUFFER_VIEW_FILES 2U

static void framebuffer_draw_glyph(uint32_t left, uint32_t top, uint8_t character);
static void outb(uint16_t port, uint8_t value);
static uint8_t inb(uint16_t port);
static void outw(uint16_t port, uint16_t value);
static uint16_t inw(uint16_t port);

static void framebuffer_mark_dirty(uint32_t left, uint32_t top,
    uint32_t width, uint32_t height) {
    if (width == 0U || height == 0U || left >= framebuffer_width ||
        top >= framebuffer_height) return;
    if (width > framebuffer_width - left) width = framebuffer_width - left;
    if (height > framebuffer_height - top) height = framebuffer_height - top;
    if (!framebuffer_dirty) {
        dirty_left = left;
        dirty_top = top;
        dirty_right = left + width;
        dirty_bottom = top + height;
        framebuffer_dirty = 1U;
    } else {
        if (left < dirty_left) dirty_left = left;
        if (top < dirty_top) dirty_top = top;
        if (left + width > dirty_right) dirty_right = left + width;
        if (top + height > dirty_bottom) dirty_bottom = top + height;
    }
}

static void framebuffer_draw_icon(uint32_t left, uint32_t top, uint32_t icon_id) {
    const uint32_t icon_bytes = QUANTA_DESKTOP_ICON_SIZE * QUANTA_DESKTOP_ICON_SIZE * 4U;
    const uint8_t *atlas = _binary_build_assets_system_icons_rgba_start;
    if (icon_id > QUANTA_DESKTOP_ICON_FILE ||
        (uint64_t)(_binary_build_assets_system_icons_rgba_end - atlas) !=
            icon_bytes * (QUANTA_DESKTOP_ICON_FILE + 1U) ||
        left >= framebuffer_width || top >= framebuffer_height) return;
    uint32_t width = framebuffer_width - left < QUANTA_DESKTOP_ICON_SIZE ?
        framebuffer_width - left : QUANTA_DESKTOP_ICON_SIZE;
    uint32_t height = framebuffer_height - top < QUANTA_DESKTOP_ICON_SIZE ?
        framebuffer_height - top : QUANTA_DESKTOP_ICON_SIZE;
    framebuffer_mark_dirty(left, top, width, height);
    const uint8_t *source = atlas + (uint64_t)icon_id * icon_bytes;
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t *destination = framebuffer + (uint64_t)(top + y) * framebuffer_pitch +
            (uint64_t)left * framebuffer_bytes_per_pixel;
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t alpha = source[3];
            if (alpha != 0U) {
                uint8_t red = (uint8_t)(((uint32_t)0xdcU * alpha +
                    (uint32_t)destination[framebuffer_format == QUANTA_FRAMEBUFFER_FORMAT_RGBX8888 ? 0U : 2U] * (255U - alpha)) / 255U);
                uint8_t green = (uint8_t)(((uint32_t)0xeeU * alpha +
                    (uint32_t)destination[1] * (255U - alpha)) / 255U);
                uint8_t blue = (uint8_t)(((uint32_t)0xffU * alpha +
                    (uint32_t)destination[framebuffer_format == QUANTA_FRAMEBUFFER_FORMAT_RGBX8888 ? 2U : 0U] * (255U - alpha)) / 255U);
                destination[0] = framebuffer_format == QUANTA_FRAMEBUFFER_FORMAT_RGBX8888 ? red : blue;
                destination[1] = green;
                destination[2] = framebuffer_format == QUANTA_FRAMEBUFFER_FORMAT_RGBX8888 ? blue : red;
            }
            destination += framebuffer_bytes_per_pixel;
            source += 4U;
        }
    }
}

static void framebuffer_copy_dirty(uint8_t *destination, const uint8_t *source,
    uint32_t destination_y, uint32_t left, uint32_t top, uint32_t right,
    uint32_t bottom);

static uint16_t bga_read(uint16_t index) {
    outw(0x01ceU, index);
    return inw(0x01cfU);
}

static void bga_write(uint16_t index, uint16_t value) {
    outw(0x01ceU, index);
    outw(0x01cfU, value);
}

static void framebuffer_copy(uint8_t *destination, const uint8_t *source,
    uint64_t bytes) {
    uint64_t words = bytes / sizeof(uint64_t);
    uint64_t remainder = bytes % sizeof(uint64_t);
    __asm__ volatile("cld; rep movsq"
        : "+D"(destination), "+S"(source), "+c"(words)
        : : "memory");
    for (uint64_t byte = 0; byte < remainder; ++byte)
        destination[byte] = source[byte];
}

static void framebuffer_copy_dirty(uint8_t *destination, const uint8_t *source,
    uint32_t destination_y, uint32_t left, uint32_t top, uint32_t right,
    uint32_t bottom) {
    uint64_t row_bytes = (uint64_t)(right - left) * framebuffer_bytes_per_pixel;
    uint64_t x_offset = (uint64_t)left * framebuffer_bytes_per_pixel;
    for (uint32_t row = top; row < bottom; ++row)
        framebuffer_copy(destination + (uint64_t)(destination_y + row) * framebuffer_pitch + x_offset,
            source + (uint64_t)row * framebuffer_pitch + x_offset, row_bytes);
}

static int bga_initialize(uint32_t width, uint32_t height) {
    uint16_t version = bga_read(0U);
    uint64_t required_bytes = (uint64_t)width * 4U * height * 2U;
    uint64_t available_bytes = (uint64_t)bga_read(0x0aU) * 65536U;
    if (version < 0xb0c4U || width > 0xffffU || height > 0x7fffU ||
        required_bytes > available_bytes)
        return 0;
    bga_write(4U, 0U);
    bga_write(1U, (uint16_t)width);
    bga_write(2U, (uint16_t)height);
    bga_write(3U, 32U);
    bga_write(6U, (uint16_t)width);
    bga_write(7U, (uint16_t)(height * 2U));
    bga_write(8U, 0U);
    bga_write(9U, 0U);
    bga_write(4U, 0x0041U);
    if (bga_read(1U) != width || bga_read(2U) != height ||
        bga_read(3U) != 32U || bga_read(6U) != width ||
        bga_read(7U) < height * 2U) {
        bga_write(4U, 0U);
        return 0;
    }
    return 1;
}

static int bga_wait_retrace(void) {
    uint32_t attempts;
    for (attempts = 0; attempts < 10000000U; ++attempts)
        if ((inb(0x03daU) & 8U) == 0U) break;
    if (attempts == 10000000U) return 0;
    for (attempts = 0; attempts < 10000000U; ++attempts)
        if ((inb(0x03daU) & 8U) != 0U) return 1;
    return 0;
}

static int ps2_wait_input_clear(void) {
    for (uint32_t attempt = 0; attempt < 100000U; ++attempt) {
        if ((inb(0x64) & 2U) == 0U) return 1;
    }
    return 0;
}

static int ps2_wait_mouse_byte(uint8_t *value) {
    for (uint32_t attempt = 0; attempt < 100000U; ++attempt) {
        uint8_t status = inb(0x64);
        if ((status & 1U) != 0U && (status & 0x20U) != 0U) {
            *value = inb(0x60);
            return 1;
        }
    }
    return 0;
}

static int ps2_mouse_command(uint8_t command) {
    uint8_t response;
    if (!ps2_wait_input_clear()) return 0;
    outb(0x64, 0xd4);
    if (!ps2_wait_input_clear()) return 0;
    outb(0x60, command);
    return ps2_wait_mouse_byte(&response) && response == 0xfaU;
}

static void ps2_mouse_initialize(void) {
    uint8_t response;
    if (!ps2_wait_input_clear()) return;
    outb(0x64, 0xa8);
    if (!ps2_wait_input_clear()) return;
    outb(0x64, 0x20);
    for (uint32_t attempt = 0; attempt < 100000U; ++attempt) {
        uint8_t status = inb(0x64);
        if ((status & 1U) != 0U && (status & 0x20U) == 0U) {
            response = inb(0x60);
            if (!ps2_wait_input_clear()) return;
            outb(0x64, 0x60);
            if (!ps2_wait_input_clear()) return;
            outb(0x60, (uint8_t)(response | 2U));
            if (ps2_mouse_command(0xf6U) && ps2_mouse_command(0xf4U)) {
                mouse_ready = 1U;
                serial_write_marker("QUANTA_PS2_MOUSE_READY\n");
            }
            return;
        }
    }
}

static int ps2_mouse_read_event(void) {
    uint8_t status = inb(0x64);
    if ((status & 1U) == 0U || (status & 0x20U) == 0U) return -1;
    uint8_t value = inb(0x60);
    if (!mouse_ready) return -1;
    if (mouse_packet_index == 0U && (value & 8U) == 0U) return -1;
    mouse_packet[mouse_packet_index++] = value;
    if (mouse_packet_index < 3U) return -1;
    mouse_packet_index = 0U;
    uint8_t flags = mouse_packet[0];
    uint8_t buttons = (uint8_t)(flags & 7U);
    uint8_t changed = buttons != mouse_buttons;
    mouse_buttons = buttons;
    if ((flags & 0xc0U) == 0U) {
        int32_t dx = (int8_t)mouse_packet[1];
        int32_t dy = (int8_t)mouse_packet[2];
        int32_t next_x = (int32_t)mouse_x + dx;
        int32_t next_y = (int32_t)mouse_y - dy;
        if (next_x < 0) next_x = 0;
        if (next_y < 0) next_y = 0;
        if (framebuffer_width != 0U && (uint32_t)next_x >= framebuffer_width)
            next_x = (int32_t)framebuffer_width - 1;
        if (framebuffer_height != 0U && (uint32_t)next_y >= framebuffer_height)
            next_y = (int32_t)framebuffer_height - 1;
        changed |= (uint32_t)next_x != mouse_x || (uint32_t)next_y != mouse_y;
        mouse_x = (uint32_t)next_x;
        mouse_y = (uint32_t)next_y;
    }
    if (!changed) return -1;
    return QUANTA_MOUSE_EVENT(mouse_x, mouse_y, mouse_buttons);
}

void quanta_arch_storage_initialize(const struct quanta_boot_info *boot_info) {
    if (boot_info != 0 && boot_info->storage_physical != 0 && boot_info->storage_sectors != 0) {
        memory_storage = (const uint8_t *)(uintptr_t)boot_info->storage_physical;
        memory_storage_sectors = boot_info->storage_sectors;
    }
    probe_ata_devices();
}

uint64_t quanta_arch_storage_physical(void) { return (uint64_t)(uintptr_t)memory_storage; }
uint64_t quanta_arch_storage_sectors(void) { return memory_storage_sectors; }

static void framebuffer_fill_rect(uint32_t left, uint32_t top, uint32_t width,
    uint32_t height, uint32_t color) {
    uint32_t alpha = color >> 24;
    if (alpha == 0U) alpha = 255U;
    if (left >= framebuffer_width || top >= framebuffer_height) return;
    if (width > framebuffer_width - left) width = framebuffer_width - left;
    if (height > framebuffer_height - top) height = framebuffer_height - top;
    if (!framebuffer_dirty) {
        dirty_left = left;
        dirty_top = top;
        dirty_right = left + width;
        dirty_bottom = top + height;
        framebuffer_dirty = 1U;
    } else {
        if (left < dirty_left) dirty_left = left;
        if (top < dirty_top) dirty_top = top;
        if (left + width > dirty_right) dirty_right = left + width;
        if (top + height > dirty_bottom) dirty_bottom = top + height;
    }
    for (uint32_t row = 0; row < height; ++row) {
        uint8_t *pixel = framebuffer + (uint64_t)(top + row) * framebuffer_pitch +
            (uint64_t)left * framebuffer_bytes_per_pixel;
        for (uint32_t column = 0; column < width; ++column) {
            uint8_t *red = pixel +
                (framebuffer_format == QUANTA_FRAMEBUFFER_FORMAT_RGBX8888 ? 0U : 2U);
            uint8_t *blue = pixel +
                (framebuffer_format == QUANTA_FRAMEBUFFER_FORMAT_RGBX8888 ? 2U : 0U);
            if (alpha == 255U) {
                *red = (uint8_t)(color >> 16);
                pixel[1] = (uint8_t)(color >> 8);
                *blue = (uint8_t)color;
            } else {
                *red = (uint8_t)(((uint32_t)(color >> 16 & 0xffU) * alpha +
                    (uint32_t)*red * (255U - alpha)) / 255U);
                pixel[1] = (uint8_t)(((uint32_t)(color >> 8 & 0xffU) * alpha +
                    (uint32_t)pixel[1] * (255U - alpha)) / 255U);
                *blue = (uint8_t)(((uint32_t)(color & 0xffU) * alpha +
                    (uint32_t)*blue * (255U - alpha)) / 255U);
            }
            if (framebuffer_bytes_per_pixel == 4U) pixel[3] = 0;
            pixel += framebuffer_bytes_per_pixel;
        }
    }
    framebuffer_bytes_written += (uint64_t)width * height * framebuffer_bytes_per_pixel;
}

static void framebuffer_fill_rounded_rect(uint32_t left, uint32_t top,
    uint32_t width, uint32_t height, uint32_t radius, uint32_t color) {
    if (width == 0U || height == 0U || radius == 0U ||
        radius > width / 2U || radius > height / 2U) return;
    for (uint32_t row = 0; row < height; ++row) {
        uint32_t inset = 0U;
        uint32_t edge_y = row < radius ? radius - row - 1U :
            (row >= height - radius ? row - (height - radius) : 0U);
        if (edge_y != 0U) {
            int32_t dy = (int32_t)edge_y;
            int32_t radius_squared = (int32_t)(radius * radius);
            for (uint32_t candidate = 0; candidate <= radius; ++candidate) {
                int32_t dx = (int32_t)(radius - candidate);
                if (dx * dx + dy * dy <= radius_squared) {
                    inset = candidate;
                    break;
                }
            }
        }
        framebuffer_fill_rect(left + inset, top + row, width - inset * 2U,
            1U, color);
    }
}

static void framebuffer_draw_wallpaper(uint32_t left, uint32_t top,
    uint32_t width, uint32_t height) {
    const uint8_t *image = _binary_build_assets_default_background_pal_start;
    if ((uint64_t)(_binary_build_assets_default_background_pal_end - image) !=
        512U * 288U + 256U * 4U) return;
    const uint8_t *palette = image + 512U * 288U;
    if (width == 0U || height == 0U) {
        left = 0U;
        top = 0U;
        width = framebuffer_width;
        height = framebuffer_height;
    }
    if (left >= framebuffer_width || top >= framebuffer_height) return;
    if (width > framebuffer_width - left) width = framebuffer_width - left;
    if (height > framebuffer_height - top) height = framebuffer_height - top;
    framebuffer_mark_dirty(left, top, width, height);
    for (uint32_t y = top; y < top + height; ++y) {
        uint32_t source_y = (uint64_t)y * 288U / framebuffer_height;
        uint8_t *destination = framebuffer + (uint64_t)y * framebuffer_pitch;
        for (uint32_t x = left; x < left + width; ++x) {
            uint32_t source_x = (uint64_t)x * 512U / framebuffer_width;
            const uint8_t *source = palette + (uint32_t)image[
                (uint64_t)source_y * 512U + source_x] * 4U;
            uint8_t *pixel = destination + (uint64_t)x * framebuffer_bytes_per_pixel;
            pixel[0] = framebuffer_format == QUANTA_FRAMEBUFFER_FORMAT_RGBX8888 ?
                source[0] : source[2];
            pixel[1] = source[1];
            pixel[2] = framebuffer_format == QUANTA_FRAMEBUFFER_FORMAT_RGBX8888 ?
                source[2] : source[0];
            if (framebuffer_bytes_per_pixel == 4U) pixel[3] = 0;
        }
    }
    framebuffer_bytes_written += (uint64_t)width * height *
        framebuffer_bytes_per_pixel;
}

static void framebuffer_draw_glyph_color(uint32_t left, uint32_t top,
    uint8_t character, uint32_t foreground, uint32_t background, uint32_t scale) {
    if (scale == 0U || scale > 8U || left + 8U * scale > framebuffer_width ||
        top + 16U * scale > framebuffer_height) return;
    framebuffer_mark_dirty(left, top, 8U * scale, 16U * scale);
    for (uint32_t row = 0; row < 16U; ++row) {
        uint8_t bits = framebuffer_font[(uint32_t)character * 16U + row];
        for (uint32_t repeat_y = 0; repeat_y < scale; ++repeat_y) {
            uint8_t *pixel = framebuffer + (uint64_t)(top + row * scale + repeat_y) * framebuffer_pitch +
                (uint64_t)left * framebuffer_bytes_per_pixel;
            for (uint32_t column = 0; column < 8U; ++column) {
                uint32_t foreground_pixel = (bits & (0x80U >> column)) != 0U;
                uint32_t color = foreground_pixel ? foreground : background;
                for (uint32_t repeat_x = 0; repeat_x < scale; ++repeat_x) {
                    if (foreground_pixel || background != 0xffffffffU) {
                        pixel[0] = (uint8_t)color;
                        pixel[1] = (uint8_t)(color >> 8);
                        pixel[2] = (uint8_t)(color >> 16);
                        if (framebuffer_bytes_per_pixel == 4U) pixel[3] = 0;
                    }
                    pixel += framebuffer_bytes_per_pixel;
                }
            }
        }
    }
    framebuffer_bytes_written += (uint64_t)8U * 16U * scale * scale * framebuffer_bytes_per_pixel;
}

static void framebuffer_draw_glyph(uint32_t left, uint32_t top, uint8_t character) {
    framebuffer_draw_glyph_color(left, top, character,
        FRAMEBUFFER_FOREGROUND, FRAMEBUFFER_BACKGROUND, 1U);
}

static void framebuffer_draw_text(uint32_t left, uint32_t top, const char *text,
    uint32_t color, uint32_t scale) {
    uint32_t index = 0;
    if (text == 0 || scale == 0U) return;
    while (text[index] && left + (index + 1U) * 8U * scale <= framebuffer_width) {
        framebuffer_draw_glyph_color(left + index * 8U * scale, top,
            (uint8_t)text[index], color, 0xffffffffU, scale);
        ++index;
    }
}

void quanta_arch_boot_splash(uint32_t stage) {
    static const char *const status[] = {
        "[ OK ] Kernel initialized (x86_64)",
        "[ OK ] Memory manager ready",
        "[ OK ] Filesystem driver loaded (QuantaFS)",
        "[ OK ] Graphics subsystem initialized (VBE/GOP)",
        "[ OK ] Input service initialized (PS/2)",
        "[ OK ] Launching graphical login manager...",
    };
    uint32_t scale, left, top, width, progress;
    if (framebuffer == 0 || framebuffer_width < 640U || framebuffer_height < 480U) return;
    if (stage >= sizeof(status) / sizeof(status[0])) stage = (uint32_t)(sizeof(status) / sizeof(status[0]) - 1U);
    scale = framebuffer_width >= 1280U ? 2U : 1U;
    left = framebuffer_width / 5U;
    top = framebuffer_height / 12U;
    width = framebuffer_width * 3U / 5U;
    framebuffer_fill_rect(0U, 0U, framebuffer_width, framebuffer_height, 0x00030508U);
    /* A compact Quanta mark: concentric rings plus the Q tail. */
    uint32_t logo_x = framebuffer_width / 2U, logo_y = top + 74U * scale;
    for (uint32_t radius = 12U * scale; radius <= 36U * scale; radius += 12U * scale) {
        framebuffer_fill_rect(logo_x - radius, logo_y - radius, radius * 2U, 2U * scale, 0x000095ffU);
        framebuffer_fill_rect(logo_x - radius, logo_y + radius, radius * 2U, 2U * scale, 0x000095ffU);
        framebuffer_fill_rect(logo_x - radius, logo_y - radius, 2U * scale, radius * 2U, 0x000095ffU);
        framebuffer_fill_rect(logo_x + radius, logo_y - radius, 2U * scale, radius * 2U, 0x000095ffU);
    }
    framebuffer_fill_rect(logo_x + 8U * scale, logo_y + 8U * scale, 34U * scale, 6U * scale, 0x000095ffU);
    framebuffer_draw_text(logo_x - 32U * 8U * scale, top + 130U * scale,
        "QuantaOS", 0x000095ffU, 2U * scale);
    framebuffer_draw_text(logo_x - 15U * 8U * scale, top + 166U * scale,
        "SMALLER / FASTER / YOURS", 0x00000ca8ffU, scale);
    for (uint32_t index = 0; index <= stage; ++index)
        framebuffer_draw_text(left, top + 210U * scale + index * 22U * scale,
            status[index], 0x0000a9ffU, scale);
    progress = (stage + 1U) * width / (uint32_t)(sizeof(status) / sizeof(status[0]));
    framebuffer_fill_rect(left, top + 360U * scale, width, 10U * scale, 0x00071c2aU);
    framebuffer_fill_rect(left, top + 360U * scale, progress, 10U * scale, 0x0000095ffU);
    framebuffer_draw_text(left, top + 382U * scale, status[stage], 0x0000a9ffU, scale);
    framebuffer_draw_text(framebuffer_width - 170U * scale, framebuffer_height - 44U * scale,
        "QuantaOS v1.0.0", 0x000095ffU, scale);
    framebuffer_workspace_visible = 1U;
    (void)quanta_arch_desktop_present();
}

static void framebuffer_draw_terminal_prompt(const struct quanta_desktop_draw_command *command) {
    uint32_t scale = framebuffer_width / 360U;
    if (scale < 2U) scale = 2U;
    if (scale > 6U) scale = 6U;
    uint32_t x = framebuffer_width * 13U / 100U;
    uint32_t y = framebuffer_height / 5U;
    uint32_t cursor = 0;
    framebuffer_fill_rect(0, 0, framebuffer_width, framebuffer_height, FRAMEBUFFER_TERMINAL_BACKGROUND);
    framebuffer_fill_rect(0, 0, 10U, framebuffer_height, 0x000477cbU);
    framebuffer_fill_rect(0, framebuffer_height * 54U / 100U, 10U, 12U, FRAMEBUFFER_TERMINAL_BACKGROUND);
    for (uint32_t character = 0; character < command->text_length; ++character) {
        if (command->text[character] == '$') { cursor = character; break; }
        framebuffer_draw_glyph_color(x + character * 8U * scale, y,
            (uint8_t)command->text[character], FRAMEBUFFER_PROMPT_CYAN,
            FRAMEBUFFER_TERMINAL_BACKGROUND, scale);
    }
    if (cursor == 0U) cursor = command->text_length;
    uint32_t prompt_y = y + 16U * scale + 20U;
    uint32_t bracket_x = x > 72U ? x - 72U : 12U;
    uint32_t bracket_height = 16U * scale + 16U;
    framebuffer_fill_rect(bracket_x, prompt_y, 4U, bracket_height, FRAMEBUFFER_PROMPT_GREEN);
    framebuffer_fill_rect(bracket_x, prompt_y, 64U, 4U, FRAMEBUFFER_PROMPT_GREEN);
    framebuffer_fill_rect(bracket_x, prompt_y + bracket_height - 4U, 64U, 4U, FRAMEBUFFER_PROMPT_GREEN);
    framebuffer_draw_glyph_color(x - 24U, prompt_y + 8U,
        '$', FRAMEBUFFER_PROMPT_BLUE, FRAMEBUFFER_TERMINAL_BACKGROUND, scale);
    uint32_t input_x = x + 16U * scale;
    uint32_t input_y = prompt_y + 8U;
    for (uint32_t character = cursor + 1U; character < command->text_length; ++character) {
        framebuffer_draw_glyph_color(input_x, input_y, (uint8_t)command->text[character],
            FRAMEBUFFER_FOREGROUND, FRAMEBUFFER_TERMINAL_BACKGROUND, scale);
        input_x += 8U * scale;
    }
    uint32_t cursor_left = input_x + 8U;
    uint32_t cursor_top = input_y;
    uint32_t cursor_width = 6U * scale;
    uint32_t cursor_height = 16U * scale;
    framebuffer_fill_rect(cursor_left, cursor_top, cursor_width, 2U, FRAMEBUFFER_FOREGROUND);
    framebuffer_fill_rect(cursor_left, cursor_top + cursor_height - 2U,
        cursor_width, 2U, FRAMEBUFFER_FOREGROUND);
    framebuffer_fill_rect(cursor_left, cursor_top, 2U, cursor_height, FRAMEBUFFER_FOREGROUND);
    framebuffer_fill_rect(cursor_left + cursor_width - 2U, cursor_top,
        2U, cursor_height, FRAMEBUFFER_FOREGROUND);
}

int quanta_arch_desktop_draw(const void *commands, uint32_t count) {
    const struct quanta_desktop_draw_command *draw = commands;
    if (framebuffer == 0 || draw == 0 || count == 0U ||
        count > QUANTA_DESKTOP_DRAW_MAX_COMMANDS) return -1;
    for (uint32_t index = 0; index < count; ++index) {
        if (draw[index].operation == QUANTA_DESKTOP_DRAW_RECT) {
            framebuffer_fill_rect(draw[index].x, draw[index].y,
                draw[index].width, draw[index].height, draw[index].color);
        } else if (draw[index].operation == QUANTA_DESKTOP_DRAW_ROUNDED_RECT) {
            framebuffer_fill_rounded_rect(draw[index].x, draw[index].y,
                draw[index].width, draw[index].height, draw[index].text_length,
                draw[index].color);
        } else if (draw[index].operation == QUANTA_DESKTOP_DRAW_TEXT) {
            if (draw[index].text_length > sizeof(draw[index].text)) return -1;
            uint32_t scale = draw[index].height == 0U ? 1U : draw[index].height;
            for (uint32_t character = 0; character < draw[index].text_length; ++character) {
                framebuffer_draw_glyph_color(draw[index].x + character * 8U * scale,
                    draw[index].y, (uint8_t)draw[index].text[character],
                    draw[index].color, 0xffffffffU, scale);
            }
        } else if (draw[index].operation == QUANTA_DESKTOP_DRAW_TERMINAL_PROMPT) {
            if (draw[index].text_length > sizeof(draw[index].text)) return -1;
            framebuffer_draw_terminal_prompt(&draw[index]);
        } else if (draw[index].operation == QUANTA_DESKTOP_DRAW_WALLPAPER) {
            framebuffer_draw_wallpaper(draw[index].x, draw[index].y,
                draw[index].width, draw[index].height);
        } else if (draw[index].operation == QUANTA_DESKTOP_DRAW_ICON) {
            framebuffer_draw_icon(draw[index].x, draw[index].y, draw[index].color);
        } else {
            return -1;
        }
    }
    return 0;
}

int quanta_arch_desktop_present(void) {
    if (framebuffer == 0 || framebuffer_scanout == 0 || framebuffer_bytes == 0U)
        return -1;
    if (!framebuffer_dirty) return 0;
    if (bga_page_flip) {
        uint32_t back_y = bga_front_y == 0U ? framebuffer_height : 0U;
        framebuffer_copy_dirty(framebuffer_scanout, framebuffer, back_y,
            dirty_left, dirty_top, dirty_right, dirty_bottom);
        if (!bga_wait_retrace()) {
            serial_write_marker("QUANTA_BGA_RETRACE_TIMEOUT\n");
            return -1;
        }
        bga_write(9U, (uint16_t)back_y);
        if (bga_read(9U) != back_y) return -1;
        framebuffer_copy_dirty(framebuffer_scanout, framebuffer, bga_front_y,
            dirty_left, dirty_top, dirty_right, dirty_bottom);
        bga_front_y = back_y;
        if (!bga_flip_reported) {
            serial_write_marker("QUANTA_BGA_PAGE_FLIP\n");
            bga_flip_reported = 1U;
        }
    } else {
        framebuffer_copy_dirty(framebuffer_scanout, framebuffer, 0U,
            dirty_left, dirty_top, dirty_right, dirty_bottom);
        if (!unsynced_present_reported) {
            serial_write_marker("QUANTA_DISPLAY_PRESENT_UNSYNCED\n");
            unsynced_present_reported = 1U;
        }
    }
    framebuffer_workspace_visible = 1U;
    framebuffer_dirty = 0U;
    return 0;
}

int quanta_arch_desktop_view(uint32_t view) {
    if (framebuffer == 0) return -1;
    if (view == QUANTA_DESKTOP_VIEW_TERMINAL) {
        framebuffer_show_terminal();
        serial_write_marker("QUANTA_VIEW_TERMINAL\n");
    } else if (view == QUANTA_DESKTOP_VIEW_WORKSPACE) {
        framebuffer_show_workspace();
        serial_write_marker("QUANTA_VIEW_WORKSPACE\n");
    } else {
        return -1;
    }
    return 0;
}

static void framebuffer_draw_char(uint8_t x, uint8_t y, uint8_t character) {
    framebuffer_draw_glyph(FRAMEBUFFER_TERMINAL_X + (uint32_t)x * 8U,
        FRAMEBUFFER_TERMINAL_Y + (uint32_t)y * 16U, character);
}

static void framebuffer_draw_terminal_frame(void) {
    static const char *items[] = { "Home", "Files", "Settings", "Terminal" };
    framebuffer_fill_rect(0, 0, framebuffer_width, framebuffer_height, FRAMEBUFFER_BACKGROUND);
    framebuffer_fill_rect(0, 0, FRAMEBUFFER_PANEL_WIDTH, framebuffer_height, 0x00151d26U);
    framebuffer_fill_rect(FRAMEBUFFER_PANEL_WIDTH, 0, framebuffer_width - FRAMEBUFFER_PANEL_WIDTH,
        32U, 0x00334250U);
    static const char title[] = "Quanta Workspace / Terminal";
    for (uint32_t index = 0; title[index]; ++index)
        framebuffer_draw_glyph(FRAMEBUFFER_PANEL_WIDTH + 12U + index * 8U, 8U, (uint8_t)title[index]);
    for (uint32_t item = 0; item < 4U; ++item) {
        uint32_t y = 64U + item * 44U;
        if (item == 3U) framebuffer_fill_rect(6U, y - 5U,
            FRAMEBUFFER_PANEL_WIDTH - 12U, 36U, 0x00334250U);
        for (uint32_t character = 0; items[item][character]; ++character)
            framebuffer_draw_glyph(12U + character * 8U, y, (uint8_t)items[item][character]);
    }
    framebuffer_fill_rect(FRAMEBUFFER_TERMINAL_X - 8U, FRAMEBUFFER_TERMINAL_Y - 8U,
        80U * 8U + 16U, 25U * 16U + 16U, 0x00131a21U);
}

static void framebuffer_show_workspace(void) {
    static const char *items[] = { "Home", "Files", "Settings", "Terminal" };
    desktop_view = FRAMEBUFFER_VIEW_WORKSPACE;
    framebuffer_fill_rect(0, 0, framebuffer_width, framebuffer_height, 0x002a4658U);
    framebuffer_fill_rect(0, 0, FRAMEBUFFER_PANEL_WIDTH, framebuffer_height, 0x00151d26U);
    framebuffer_fill_rect(FRAMEBUFFER_PANEL_WIDTH, 0, framebuffer_width - FRAMEBUFFER_PANEL_WIDTH,
        32U, 0x00334250U);
    framebuffer_fill_rect(FRAMEBUFFER_PANEL_WIDTH + 12U, 44U,
        framebuffer_width - FRAMEBUFFER_PANEL_WIDTH - 24U, framebuffer_height - 68U, 0x00131a21U);
    static const char title[] = "QUANTA WORKSPACE";
    for (uint32_t index = 0; title[index]; ++index)
        framebuffer_draw_glyph(FRAMEBUFFER_PANEL_WIDTH + 12U + index * 8U, 8U, (uint8_t)title[index]);
    for (uint32_t item = 0; item < 4U; ++item) {
        uint32_t y = 64U + item * 44U;
        if (item == desktop_selection) framebuffer_fill_rect(6U, y - 5U,
            FRAMEBUFFER_PANEL_WIDTH - 12U, 36U, 0x00334250U);
        for (uint32_t character = 0; items[item][character]; ++character)
            framebuffer_draw_glyph(12U + character * 8U, y, (uint8_t)items[item][character]);
    }
    static const char prompt[] = "Use Up/Down and Enter";
    for (uint32_t index = 0; prompt[index]; ++index)
        framebuffer_draw_glyph(FRAMEBUFFER_PANEL_WIDTH + 24U + index * 8U, 72U, (uint8_t)prompt[index]);
    static const char shortcuts[] = "Alt+F1: Terminal";
    for (uint32_t index = 0; shortcuts[index]; ++index)
        framebuffer_draw_glyph(FRAMEBUFFER_PANEL_WIDTH + 24U + index * 8U, 104U, (uint8_t)shortcuts[index]);
    framebuffer_workspace_visible = 1U;
    framebuffer_bytes_written += (uint64_t)framebuffer_width * framebuffer_height * framebuffer_bytes_per_pixel;
}

static void framebuffer_show_terminal(void) {
    framebuffer_workspace_visible = 0U;
    framebuffer_draw_terminal_frame();
    for (uint32_t row = 0; row < 25U; ++row) {
        for (uint32_t column = 0; column < 80U; ++column) {
            uint8_t character = terminal_cells[row * 80U + column];
            if (character != ' ') framebuffer_draw_char((uint8_t)column,
                (uint8_t)row, character);
        }
    }
}

static void framebuffer_scroll(void) {
    uint32_t left = FRAMEBUFFER_TERMINAL_X;
    uint32_t top = FRAMEBUFFER_TERMINAL_Y;
    uint32_t width = 80U * 8U;
    uint32_t height = 25U * 16U;
    uint32_t row_bytes = width * framebuffer_bytes_per_pixel;
    framebuffer_mark_dirty(left, top, width, height);
        if (!framebuffer_dirty) {
            dirty_left = left;
            dirty_top = top;
            dirty_right = left + width;
            dirty_bottom = top + height;
            framebuffer_dirty = 1U;
        } else {
            if (left < dirty_left) dirty_left = left;
            if (top < dirty_top) dirty_top = top;
            if (left + width > dirty_right) dirty_right = left + width;
            if (top + height > dirty_bottom) dirty_bottom = top + height;
        }
    for (uint32_t row = 0; row < height - 16U; ++row) {
        uint8_t *destination = framebuffer + (uint64_t)(top + row) * framebuffer_pitch +
            (uint64_t)left * framebuffer_bytes_per_pixel;
        uint8_t *source = destination + (uint64_t)framebuffer_pitch * 16U;
        for (uint32_t byte = 0; byte < row_bytes; ++byte) destination[byte] = source[byte];
    }
    framebuffer_fill_rect(left, top + height - 16U, width, 16U, FRAMEBUFFER_BACKGROUND);
    framebuffer_bytes_written += (uint64_t)row_bytes * (height - 16U);
}

void quanta_arch_display_initialize(const struct quanta_boot_info *boot_info) {
    uint64_t bytes, scanout_bytes, physical_page, page_offset, map_bytes, pages, root;
    uint64_t display_physical;
    uint32_t display_pitch;
    uint64_t backbuffer_page_count;
    if (boot_info == 0 || boot_info->framebuffer_physical == 0U ||
        boot_info->framebuffer_width < 640U || boot_info->framebuffer_width > 4096U ||
        boot_info->framebuffer_height < 480U || boot_info->framebuffer_height > 2160U ||
        (boot_info->framebuffer_bpp != 24U && boot_info->framebuffer_bpp != 32U) ||
        (boot_info->framebuffer_format != QUANTA_FRAMEBUFFER_FORMAT_BGR888 &&
            boot_info->framebuffer_format != QUANTA_FRAMEBUFFER_FORMAT_BGRX8888 &&
            boot_info->framebuffer_format != QUANTA_FRAMEBUFFER_FORMAT_RGBX8888)) return;
    framebuffer_bytes_per_pixel = (uint8_t)(boot_info->framebuffer_bpp / 8U);
    if ((boot_info->framebuffer_format == QUANTA_FRAMEBUFFER_FORMAT_BGR888 && framebuffer_bytes_per_pixel != 3U) ||
        ((boot_info->framebuffer_format == QUANTA_FRAMEBUFFER_FORMAT_BGRX8888 ||
            boot_info->framebuffer_format == QUANTA_FRAMEBUFFER_FORMAT_RGBX8888) &&
            framebuffer_bytes_per_pixel != 4U) ||
        (uint64_t)boot_info->framebuffer_width * framebuffer_bytes_per_pixel > boot_info->framebuffer_pitch) return;
    framebuffer_format = boot_info->framebuffer_format;
    bytes = (uint64_t)boot_info->framebuffer_pitch * boot_info->framebuffer_height;
    if (bytes == 0U || bytes > FRAMEBUFFER_MAX_BYTES ||
        boot_info->framebuffer_physical > ~0ULL - bytes) return;
    display_physical = boot_info->framebuffer_physical;
    display_pitch = boot_info->framebuffer_pitch;
    bga_page_flip = (uint8_t)bga_initialize(boot_info->framebuffer_width,
        boot_info->framebuffer_height);
    if (bga_page_flip) {
        framebuffer_bytes_per_pixel = 4U;
        framebuffer_format = QUANTA_FRAMEBUFFER_FORMAT_BGRX8888;
        display_pitch = boot_info->framebuffer_width * 4U;
        bytes = (uint64_t)display_pitch * boot_info->framebuffer_height;
    }
    scanout_bytes = bga_page_flip ? bytes * 2U : bytes;
    if (scanout_bytes > FRAMEBUFFER_MAX_BYTES * 2U ||
        display_physical > ~0ULL - scanout_bytes) return;
    page_offset = display_physical & 0xfffU;
    physical_page = display_physical & ~0xfffULL;
    map_bytes = (page_offset + scanout_bytes + 0xfffU) & ~0xfffULL;
    pages = map_bytes / 0x1000U;
    if (map_bytes >= 0x40000000ULL) return;
    root = quanta_arch_read_cr3() & ~0xfffULL;
    if ((uint64_t)(_binary_build_assets_ubuntu_mono_8x16_bin_end -
        _binary_build_assets_ubuntu_mono_8x16_bin_start) != sizeof(framebuffer_font)) return;
    for (uint32_t byte = 0; byte < sizeof(framebuffer_font); ++byte)
        framebuffer_font[byte] = _binary_build_assets_ubuntu_mono_8x16_bin_start[byte];
    for (uint64_t page = 0; page < pages; ++page) {
        quanta_map_page(root, FRAMEBUFFER_VIRTUAL + page * 0x1000U,
            physical_page + page * 0x1000U,
            QUANTA_PAGE_PRESENT | QUANTA_PAGE_WRITABLE | QUANTA_PAGE_NO_EXECUTE);
    }
    framebuffer_scanout = (uint8_t *)(uintptr_t)(FRAMEBUFFER_VIRTUAL + page_offset);
    framebuffer_width = boot_info->framebuffer_width;
    framebuffer_height = boot_info->framebuffer_height;
    framebuffer_pitch = display_pitch;
    framebuffer_bytes = bytes;
    backbuffer_page_count = (bytes + 0xfffU) / 0x1000U;
    for (uint64_t page = 0; page < backbuffer_page_count; ++page) {
        quanta_physical_address backbuffer_page;
        if (quanta_frame_allocate(&backbuffer_page) != 0) return;
        quanta_map_page(root, FRAMEBUFFER_BACKBUFFER_VIRTUAL + page * 0x1000U,
            backbuffer_page, QUANTA_PAGE_PRESENT | QUANTA_PAGE_WRITABLE |
                QUANTA_PAGE_NO_EXECUTE);
    }
    framebuffer = (uint8_t *)(uintptr_t)FRAMEBUFFER_BACKBUFFER_VIRTUAL;
    mouse_x = framebuffer_width / 2U;
    mouse_y = framebuffer_height / 2U;
    if (bga_page_flip) serial_write_marker("QUANTA_BGA_DOUBLE_BUFFER_READY\n");
    else serial_write_marker("QUANTA_DISPLAY_BACKBUFFER_READY_UNSYNCED\n");
    framebuffer_draw_terminal_frame();
    if (bga_page_flip) (void)quanta_arch_desktop_present();
    quanta_arch_write_marker("QUANTA_FRAMEBUFFER_READY\n");
}

void quanta_arch_display_dimensions(uint32_t *width, uint32_t *height) {
    if (width != 0) *width = framebuffer_width;
    if (height != 0) *height = framebuffer_height;
}

int quanta_arch_memory_storage_read(uint32_t lba, void *buffer) {
    uint8_t *output = buffer;
    const uint8_t *input;
    uint32_t index;
    if (memory_storage == 0 || buffer == 0 || (uint64_t)lba >= memory_storage_sectors) {
        return -1;
    }
    input = memory_storage + (uint64_t)lba * 512U;
    for (index = 0; index < 512U; ++index) output[index] = input[index];
    return 0;
}

static void set_gate(uint8_t vector, void (*handler)(void), uint8_t attributes) {
    uint64_t address = (uint64_t)(uintptr_t)handler;
    idt[vector].offset_low = address;
    idt[vector].selector = 0x08;
    idt[vector].ist = 0;
    idt[vector].attributes = attributes;
    idt[vector].offset_middle = address >> 16;
    idt[vector].offset_high = address >> 32;
    idt[vector].reserved = 0;
}

static void outb(uint16_t port, uint8_t value) { __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port)); }
static void outw(uint16_t port, uint16_t value) { __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port)); }
static uint8_t inb(uint16_t port) { uint8_t value; __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }
static uint16_t inw(uint16_t port) { uint16_t value; __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port)); return value; }

static uint8_t ata_status(uint16_t io_base) { return inb(io_base + 7U); }

static void ata_delay_400ns(uint16_t control_base) {
    (void)inb(control_base);
    (void)inb(control_base);
    (void)inb(control_base);
    (void)inb(control_base);
}

static int ata_wait_ready(uint16_t io_base, int require_data) {
    uint32_t timeout = 1000000U;
    while (timeout--) {
        uint8_t status = ata_status(io_base);
        if (status == 0U || status == 0xffU || (status & 0x01U) != 0U) return -1;
        if ((status & 0x80U) == 0U && (!require_data || (status & 0x08U) != 0U)) return 0;
    }
    return -1;
}

static void serial_write_char(uint8_t character) {
    uint32_t timeout = 100000U;
    while (timeout-- != 0U) {
        if ((inb(0x3fd) & 0x20U) != 0U) {
            outb(0x3f8, character);
            return;
        }
    }
}

static void serial_write_marker(const char *marker) {
    while (*marker) serial_write_char((uint8_t)*marker++);
}

static void probe_ata_devices(void) {
    static const uint16_t io_bases[2] = { 0x1f0U, 0x170U };
    static const uint16_t control_bases[2] = { 0x3f6U, 0x376U };
    for (uint32_t index = 0; index < 4U; ++index) {
        struct ata_device_info *device = &ata_devices[index];
        uint16_t identify[256];
        uint32_t channel = index / 2U;
        device->io_base = io_bases[channel];
        device->control_base = control_bases[channel];
        device->drive = (uint8_t)(index & 1U);
        device->present = 0U;
        device->sectors = 0U;
        outb(device->io_base + 6U, (uint8_t)(0xa0U | (device->drive << 4)));
        ata_delay_400ns(device->control_base);
        outb(device->io_base + 2U, 0U);
        outb(device->io_base + 3U, 0U);
        outb(device->io_base + 4U, 0U);
        outb(device->io_base + 5U, 0U);
        outb(device->io_base + 7U, 0xecU);
        if (ata_status(device->io_base) == 0U || ata_wait_ready(device->io_base, 0) != 0) continue;
        if (inb(device->io_base + 4U) != 0U || inb(device->io_base + 5U) != 0U ||
            ata_wait_ready(device->io_base, 1) != 0) continue;
        for (uint32_t word = 0; word < 256U; ++word) identify[word] = inw(device->io_base);
        device->sectors = (uint64_t)identify[60] | ((uint64_t)identify[61] << 16);
        if (device->sectors != 0U) device->present = 1U;
    }
}

uint64_t quanta_arch_ata_sector_count(uint32_t device_index) {
    if (device_index >= 4U || !ata_devices[device_index].present) return 0U;
    return ata_devices[device_index].sectors;
}

int quanta_arch_ata_read(uint32_t device_index, uint32_t lba, void *buffer) {
    struct ata_device_info *device;
    uint16_t *output = buffer;
    if (device_index >= 4U || buffer == 0 || !ata_devices[device_index].present) return -1;
    device = &ata_devices[device_index];
    if ((uint64_t)lba >= device->sectors || lba >= (1U << 28)) return -1;
    outb(device->io_base + 6U, (uint8_t)(0xe0U | (device->drive << 4) | ((lba >> 24) & 0x0fU)));
    ata_delay_400ns(device->control_base);
    outb(device->io_base + 2U, 1U);
    outb(device->io_base + 3U, (uint8_t)lba);
    outb(device->io_base + 4U, (uint8_t)(lba >> 8));
    outb(device->io_base + 5U, (uint8_t)(lba >> 16));
    outb(device->io_base + 7U, 0x20U);
    if (ata_wait_ready(device->io_base, 1) != 0) return -1;
    for (uint32_t word = 0; word < 256U; ++word) output[word] = inw(device->io_base);
    return 0;
}

int quanta_arch_ata_write(uint32_t device_index, uint32_t lba, const void *buffer) {
    struct ata_device_info *device;
    const uint16_t *input = buffer;
    if (device_index >= 4U || buffer == 0 || !ata_devices[device_index].present) return -1;
    device = &ata_devices[device_index];
    if ((uint64_t)lba >= device->sectors || lba >= (1U << 28)) return -1;
    outb(device->io_base + 6U, (uint8_t)(0xe0U | (device->drive << 4) | ((lba >> 24) & 0x0fU)));
    ata_delay_400ns(device->control_base);
    outb(device->io_base + 2U, 1U);
    outb(device->io_base + 3U, (uint8_t)lba);
    outb(device->io_base + 4U, (uint8_t)(lba >> 8));
    outb(device->io_base + 5U, (uint8_t)(lba >> 16));
    outb(device->io_base + 7U, 0x30U);
    if (ata_wait_ready(device->io_base, 1) != 0) return -1;
    for (uint32_t word = 0; word < 256U; ++word) outw(device->io_base, input[word]);
    outb(device->io_base + 7U, 0xe7U);
    return ata_wait_ready(device->io_base, 0);
}

void quanta_arch_timer_interrupt(void) {
    ++timer_ticks;
    outb(0x20, 0x20);
}

uint64_t quanta_arch_timer_ticks(void) { return timer_ticks; }

void quanta_arch_exception_dispatch(uint64_t vector, uint64_t error_code,
    uint64_t code_segment) {
    if ((code_segment & 3U) == 3U) quanta_task_user_fault(vector);
    serial_write_char('K');
    serial_write_char('E');
    serial_write_char('R');
    serial_write_char('N');
    serial_write_char('E');
    serial_write_char('L');
    serial_write_char('_');
    serial_write_char('E');
    serial_write_char('X');
    serial_write_char('C');
    serial_write_char('E');
    serial_write_char('P');
    serial_write_char('T');
    serial_write_char('I');
    serial_write_char('O');
    serial_write_char('N');
    serial_write_char('_');
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t digit = (uint8_t)((vector >> shift) & 0x0fU);
        serial_write_char((uint8_t)(digit < 10U ? '0' + digit : 'A' + digit - 10U));
    }
    serial_write_char('_');
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t digit = (uint8_t)((error_code >> shift) & 0x0fU);
        serial_write_char((uint8_t)(digit < 10U ? '0' + digit : 'A' + digit - 10U));
    }
    serial_write_char('\n');
    for (;;) __asm__ volatile("hlt");
}

static void initialize_pic_pit(void) {
    uint16_t divisor = (uint16_t)(1193182U / 100U);
    outb(0x21, 0xff);
    outb(0xa1, 0xff);
    outb(0x20, 0x11);
    outb(0x80, 0);
    outb(0xa0, 0x11);
    outb(0x80, 0);
    outb(0x21, 0x20);
    outb(0x80, 0);
    outb(0xa1, 0x28);
    outb(0x80, 0);
    outb(0x21, 0x04);
    outb(0x80, 0);
    outb(0xa1, 0x02);
    outb(0x80, 0);
    outb(0x21, 0x01);
    outb(0x80, 0);
    outb(0xa1, 0x01);
    outb(0x80, 0);
    outb(0x21, 0xfe);
    outb(0xa1, 0xff);
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)divisor);
    outb(0x40, (uint8_t)(divisor >> 8));
}

static void vga_cursor(void) { uint16_t pos = (uint16_t)(vga_y * 80 + vga_x); outb(0x3d4, 14); outb(0x3d5, pos >> 8); outb(0x3d4, 15); outb(0x3d5, pos); }
static void vga_scroll(void) {
    for (uint32_t row = 0; row < 24U; ++row) {
        for (uint32_t column = 0; column < 80U; ++column) {
            terminal_cells[row * 80U + column] = terminal_cells[(row + 1U) * 80U + column];
            vga[row * 80U + column] = (uint16_t)(0x0700U | terminal_cells[row * 80U + column]);
        }
    }
    for (uint32_t column = 0; column < 80U; ++column) {
        terminal_cells[24U * 80U + column] = ' ';
        vga[24U * 80U + column] = 0x0720;
    }
    if (framebuffer != 0 && !framebuffer_workspace_visible) framebuffer_scroll();
    vga_y = 24;
}

static void vga_put(char ch) {
    if (ch == '\n') { vga_x = 0; ++vga_y; }
    else if (ch == '\r') vga_x = 0;
    else if (ch == '\b') {
        if (vga_x) --vga_x;
        terminal_cells[vga_y * 80U + vga_x] = ' ';
        vga[vga_y * 80U + vga_x] = 0x0720;
        if (framebuffer != 0 && !framebuffer_workspace_visible) framebuffer_draw_char(vga_x, vga_y, ' ');
    } else {
        terminal_cells[vga_y * 80U + vga_x] = (uint8_t)ch;
        vga[vga_y * 80U + vga_x] = (uint16_t)(0x0700U | (uint8_t)ch);
        if (framebuffer != 0 && !framebuffer_workspace_visible) framebuffer_draw_char(vga_x, vga_y, (uint8_t)ch);
        if (++vga_x == 80U) { vga_x = 0; ++vga_y; }
    }
    if (vga_y >= 25U) vga_scroll();
    if (framebuffer == 0) vga_cursor();
}

void quanta_arch_write_marker(const char *marker) {
    while (*marker) {
        serial_write_char((uint8_t)*marker);
        vga_put(*marker++);
    }
}

void quanta_arch_console_write(const char *text, uint64_t length) {
    while (length--) {
        serial_write_char((uint8_t)*text);
        vga_put(*text++);
    }
}

int quanta_arch_disk_read(uint32_t lba, void *buffer) {
    return quanta_arch_ata_read(0U, lba, buffer);
}

int quanta_arch_disk_write(uint32_t lba, const void *buffer) {
    return quanta_arch_ata_write(0U, lba, buffer);
}

void quanta_arch_console_clear(void) { uint16_t i; for (i = 0; i < 80 * 25; ++i) { vga[i] = 0x0720; terminal_cells[i] = ' '; } if (framebuffer != 0) framebuffer_draw_terminal_frame(); vga_x = vga_y = 0; if (framebuffer == 0) vga_cursor(); }
void quanta_arch_console_initialize(void) { quanta_arch_console_clear(); quanta_arch_write_marker("QUANTA_VGA_READY\nQUANTA_PS2_READY\n"); ps2_mouse_initialize(); }

static int keycode(uint8_t code) {
    static const char normal[59] = "\000\0331234567890-=\b\tqwertyuiop[]\n\000asdfghjkl;'`\000\\zxcvbnm,./\000*\000 ";
    static const char shifted[59] = "\000\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\000ASDFGHJKL:\"~\000|ZXCVBNM<>?\000*\000 ";
    if (code >= 58) return -1; return (shift_down ? shifted : normal)[code];
}
int quanta_arch_console_read_char(void) {
    int mouse_event = ps2_mouse_read_event();
    if (mouse_event >= 0) return mouse_event;
    if (inb(0x64) & 1) {
        uint8_t code = inb(0x60);
        if (code == 0x2a || code == 0x36) { shift_down = 1; return -1; }
        if (code == 0xaa || code == 0xb6) { shift_down = 0; return -1; }
        if (code == 0x38) { alt_down = 1; return -1; }
        if (code == 0xb8) { alt_down = 0; return -1; }
        if ((code & 0x80) == 0 && alt_down && code == 0x3b) {
            return QUANTA_KEY_ALT_F1;
        }
        if ((code & 0x80) == 0 && alt_down && code == 0x3c) {
            return QUANTA_KEY_ALT_F2;
        }
        if ((code & 0x80) == 0 && code == 0x48) return QUANTA_KEY_UP;
        if ((code & 0x80) == 0 && code == 0x50) return QUANTA_KEY_DOWN;
        if ((code & 0x80) == 0) return keycode(code);
    }
    return quanta_arch_serial_read_char();
}

static uint8_t bcd(uint8_t value) { return (uint8_t)((value & 15) + (value >> 4) * 10); }
void quanta_arch_read_rtc(uint8_t *year, uint8_t *month, uint8_t *day, uint8_t *hour, uint8_t *minute, uint8_t *second) {
    uint8_t status, h; outb(0x70, 0x0b); status = inb(0x71); outb(0x70, 0); *second = inb(0x71); outb(0x70, 2); *minute = inb(0x71); outb(0x70, 4); h = inb(0x71); outb(0x70, 7); *day = inb(0x71); outb(0x70, 8); *month = inb(0x71); outb(0x70, 9); *year = inb(0x71); if (!(status & 4)) { *second=bcd(*second); *minute=bcd(*minute); h=bcd(h & 0x7f) | (h & 0x80); *day=bcd(*day); *month=bcd(*month); *year=bcd(*year); } if (!(status & 2) && (h & 0x80)) h = (uint8_t)(((h & 0x7f) + 12) % 24); *hour = h & 0x7f;
}

int quanta_arch_serial_read_char(void) {
    if ((inb(0x3fd) & 1) == 0) return -1;
    return inb(0x3f8);
}

void quanta_arch_shutdown(void) {
    outw(0x604, 0x2000); outw(0xb004, 0x2000);
    for (;;) __asm__ volatile("hlt");
}

static void wrmsr(uint32_t index, uint64_t value) { __asm__ volatile("wrmsr" : : "c"(index), "a"((uint32_t)value), "d"((uint32_t)(value >> 32))); }

void quanta_arch_configure_syscalls(void) {
    uint32_t low, high; uint64_t efer;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0xc0000080));
    efer = ((uint64_t)high << 32) | low;
    wrmsr(0xc0000080, efer | 1); /* EFER.SCE */
    wrmsr(0xc0000081, ((uint64_t)0x08 << 32) | ((uint64_t)0x13 << 48));
    extern void quanta_arch_syscall_entry(void);
    wrmsr(0xc0000082, (uint64_t)(uintptr_t)quanta_arch_syscall_entry);
    wrmsr(0xc0000084, (1ULL << 9));
    syscall_state.kernel_rsp = tss.rsp0;
    wrmsr(0xc0000101, 0);
    wrmsr(0xc0000102, (uint64_t)(uintptr_t)&syscall_state);
    __asm__ volatile("sti" ::: "memory");
}

void quanta_arch_initialize(uint64_t kernel_stack_top) {
    __asm__ volatile("cli" ::: "memory");
    tss.rsp0 = kernel_stack_top;
    tss.io_map_base = sizeof(tss);
    gdt[1] = 0x00af9a000000ffffULL;
    gdt[2] = 0x00af92000000ffffULL;
    gdt[3] = 0x00aff2000000ffffULL;
    gdt[4] = 0x00affa000000ffffULL;
        uint64_t cr4;
        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= 1ULL << 9;
        __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
        uint64_t tss_address = (uint64_t)(uintptr_t)&tss;
    gdt[5] = 0x0000890000000000ULL | ((tss_address & 0xffffffULL) << 16) |
        ((tss_address & 0xff000000ULL) << 32) | ((uint64_t)(sizeof(tss) - 1));
    gdt[6] = tss_address >> 32;
    struct descriptor_pointer gdt_pointer = { sizeof(gdt) - 1, (uint64_t)(uintptr_t)gdt };
    quanta_arch_reload_gdt(&gdt_pointer);
    set_gate(0x80, quanta_arch_int80_stub, 0xee);
    extern void quanta_arch_timer_stub(void);
    set_gate(0x20, quanta_arch_timer_stub, 0x8e);
    for (uint8_t vector = 0; vector < 32U; ++vector) {
        set_gate(vector, quanta_arch_exception_stubs[vector], 0x8e);
    }
    struct descriptor_pointer idt_pointer = { sizeof(idt) - 1, (uint64_t)(uintptr_t)idt };
    __asm__ volatile("lidt %0" : : "m"(idt_pointer));
    initialize_pic_pit();
}

uint64_t quanta_arch_read_cr3(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}
