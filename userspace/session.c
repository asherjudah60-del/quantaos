#include <stdint.h>
#include <quanta/syscall.h>

#define BUFFER_SIZE 128U
#define PATH_SIZE 96U

extern long quanta_user_syscall(uint64_t number, uint64_t argument,
    const void *input, void *output, uint64_t length);

static long call(uint64_t number, uint64_t argument, const void *input,
    void *output, uint64_t length) {
    return quanta_user_syscall(number, argument, input, output, length);
}

static void write_text(const char *text);

static int read_char(void) {
    return (int)call(QUANTA_SYSCALL_CONSOLE_READ, 1, 0, 0, 0);
}

static void clear(void) { call(QUANTA_SYSCALL_CONSOLE_CLEAR, 1, 0, 0, 0); }

static uint64_t read_line(char *line, uint64_t capacity, int echo) {
    uint64_t length = 0;
    for (;;) {
        int input = read_char();
        if (input < 0) continue;
        if (input == '\r' || input == '\n') {
            line[length] = 0;
            write_text("\n");
            return length;
        }
        if ((input == 8 || input == 127) && length) {
            --length;
            if (echo) write_text("\b \b");
        } else if (input >= 32 && input < 127 && length + 1U < capacity) {
            line[length++] = (char)input;
            if (echo) {
                char character[2] = { (char)input, 0 };
                write_text(character);
            }
        }
    }
}

static void two_digits(char *text, uint8_t value) {
    text[0] = (char)('0' + value / 10U);
    text[1] = (char)('0' + value % 10U);
}

static void decimal(char *text, uint64_t value) {
    char digits[20]; uint64_t count = 0; uint64_t index;
    do { digits[count++] = (char)('0' + value % 10U); value /= 10U; } while (value);
    for (index = 0; index < count; ++index) text[index] = digits[count - index - 1U];
    text[count] = 0;
}

static int equal(const char *left, const char *right) {
    while (*left && *right && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static char logged_in_user[32] = "quanta";
static char active_drive[8] = "prime";
static char cwd[PATH_SIZE] = "prime:home>quanta";
static uint32_t screen_width = 1024U, screen_height = 768U;
static struct quanta_desktop_draw_command draw_commands[QUANTA_DESKTOP_DRAW_MAX_COMMANDS];
static uint32_t draw_command_count;
static uint32_t desktop_view;
static uint32_t desktop_selection;
static uint32_t explorer_selection;
static char explorer_listing[QUANTA_SYSCALL_MAX_BUFFER];
static char explorer_cached_path[PATH_SIZE];
static uint32_t explorer_listing_valid;
static uint32_t explorer_listing_count;
static uint32_t explorer_list_view;
static uint32_t theme_dark = 1U;
static uint32_t pointer_x = 512U, pointer_y = 384U;
static uint32_t pointer_buttons;
static uint32_t terminal_x = 150U, terminal_y = 105U;
static uint32_t terminal_width = 700U, terminal_height = 500U;
static uint32_t terminal_open;
static uint32_t terminal_minimized;
static uint32_t terminal_focused;
static uint32_t terminal_started;
static uint32_t terminal_dragging;
static uint32_t terminal_resizing;
static uint32_t terminal_drag_x, terminal_drag_y;
static uint32_t terminal_resize_x, terminal_resize_y;
static uint32_t terminal_resize_width, terminal_resize_height;
struct desktop_window {
    uint32_t x, y, width, height;
    uint32_t open, minimized, focused, dragging;
    uint32_t drag_x, drag_y;
    uint32_t maximized, restore_x, restore_y, restore_width, restore_height;
};
static struct desktop_window files_window = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
};
static struct desktop_window settings_window = {
    360U, 150U, 520U, 360U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
};
static char terminal_lines[18][81];
static uint32_t terminal_line, terminal_column;

enum { VIEW_WORKSPACE, VIEW_SETTINGS, VIEW_FILES, VIEW_TERMINAL, VIEW_PREVIEW };

static uint32_t desktop_foreground(void) { return theme_dark ? 0x00e8edf2U : 0x00192733U; }

static int system_info(struct quanta_system_info *info);
static void draw_files(void);
static void launch_files(void);
static void draw_files_window(void);
static void draw_settings(void);
static void draw_settings_window(void);
static void draw_flush(void);
static void draw_present(void);
static void draw_terminal_window(void);
static uint32_t explorer_count(void);
static uint32_t explorer_entry(uint32_t requested, char *name, uint32_t capacity);
static int explorer_entry_path(uint32_t requested, char *path, uint32_t capacity);
static void explorer_refresh(void);
static void explorer_open_selected(void);
static void reset_home_cwd(void);
static void launch_terminal(void);
static void prompt(void);
static void terminal_capture(const char *text);
static void draw_window_frame(const char *title,
    const struct desktop_window *window);

static void write_text(const char *text) {
    uint64_t length = 0;
    while (text[length] && length < QUANTA_SYSCALL_MAX_BUFFER) ++length;
    call(QUANTA_SYSCALL_CONSOLE_WRITE, 1, text, 0, length);
    terminal_capture(text);
}

static void draw_flush(void) {
    if (draw_command_count != 0U) {
        call(QUANTA_SYSCALL_DESKTOP_DRAW, 1, draw_commands, 0, draw_command_count);
        draw_command_count = 0;
    }
}

static struct quanta_desktop_draw_command *draw_begin(uint32_t operation) {
    if (draw_command_count == QUANTA_DESKTOP_DRAW_MAX_COMMANDS) draw_flush();
    struct quanta_desktop_draw_command *command = &draw_commands[draw_command_count++];
    command->operation = operation;
    command->x = command->y = command->width = command->height = command->color = 0;
    command->text_length = 0;
    return command;
}

static void draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
    uint32_t color) {
    struct quanta_desktop_draw_command *command = draw_begin(QUANTA_DESKTOP_DRAW_RECT);
    command->x = x; command->y = y; command->width = width;
    command->height = height; command->color = color;
}

static void draw_rounded_rect(uint32_t x, uint32_t y, uint32_t width,
    uint32_t height, uint32_t radius, uint32_t color) {
    struct quanta_desktop_draw_command *command =
        draw_begin(QUANTA_DESKTOP_DRAW_ROUNDED_RECT);
    command->x = x;
    command->y = y;
    command->width = width;
    command->height = height;
    command->text_length = radius;
    command->color = color;
}

static void draw_icon(uint32_t x, uint32_t y, uint32_t icon_id) {
    struct quanta_desktop_draw_command *command = draw_begin(QUANTA_DESKTOP_DRAW_ICON);
    command->x = x;
    command->y = y;
    command->color = icon_id;
}

static void draw_cursor(void) {
    for (uint32_t row = 0; row < 12U; ++row) {
        uint32_t width = row < 8U ? row + 2U : 10U;
        draw_rect(pointer_x, pointer_y + row, width, 1U, 0xffe8f4ffU);
        draw_rect(pointer_x + width, pointer_y + row, 1U, 1U, 0xff101820U);
    }
}

static void draw_text(uint32_t x, uint32_t y, const char *text, uint32_t color) {
    struct quanta_desktop_draw_command *command = draw_begin(QUANTA_DESKTOP_DRAW_TEXT);
    command->x = x; command->y = y; command->color = color;
    while (text[command->text_length] && command->text_length < sizeof(command->text)) {
        command->text[command->text_length] = text[command->text_length];
        ++command->text_length;
    }
}

static void draw_present(void) {
    draw_flush();
    call(QUANTA_SYSCALL_DESKTOP_PRESENT, 1, 0, 0, 0);
}

static void draw_files_window(void) {
    struct quanta_system_info info;
    draw_window_frame("Files", &files_window);
    uint32_t x = files_window.x, y = files_window.y;
    uint32_t width = files_window.width;
    draw_icon(x + 12U, y + 5U, QUANTA_DESKTOP_ICON_FILES);
    draw_text(x + 42U, y + 10U, "Files", 0x00edf6ffU);
    draw_text(x + width - 82U, y + 10U, "_", 0x00d8e5f1U);
    draw_text(x + width - 54U, y + 10U, "[]", 0x00d8e5f1U);
    draw_text(x + width - 25U, y + 10U, "x", 0x00d8e5f1U);

    uint32_t toolbar_y = y + 40U;
    draw_rounded_rect(x + 10U, toolbar_y, 28U, 28U, 7U, 0x9032445cU);
    draw_text(x + 19U, toolbar_y + 6U, "<", 0x00dce9f6U);
    draw_rounded_rect(x + 42U, toolbar_y, 28U, 28U, 7U, 0x9032445cU);
    draw_text(x + 51U, toolbar_y + 6U, ">", 0x00dce9f6U);
    uint32_t address_width = width * 36U / 100U;
    draw_rounded_rect(x + 78U, toolbar_y, address_width, 28U, 8U,
        0x9032445cU);
    draw_text(x + 91U, toolbar_y + 6U, "Home", 0x00dce9f6U);
    uint32_t search_x = x + 84U + address_width;
    uint32_t search_width = width > address_width + 210U ?
        width - address_width - 210U : 64U;
    draw_rounded_rect(search_x, toolbar_y, search_width, 28U, 8U,
        0x9032445cU);
    draw_text(search_x + 12U, toolbar_y + 6U, "Search in Home...", 0x008da9c7U);
    draw_rounded_rect(x + width - 108U, toolbar_y, 28U, 28U, 7U,
        explorer_list_view ? 0x90405268U : 0x9032445cU);
    draw_text(x + width - 101U, toolbar_y + 6U, "[]", 0x00dce9f6U);
    draw_rounded_rect(x + width - 76U, toolbar_y, 28U, 28U, 7U,
        explorer_list_view ? 0x9032445cU : 0x90405268U);
    draw_text(x + width - 69U, toolbar_y + 6U, "=", 0x00dce9f6U);
    draw_text(x + width - 36U, toolbar_y + 6U, ":", 0x00dce9f6U);

    uint32_t sidebar_x = x + 1U;
    uint32_t sidebar_y = y + 76U;
    uint32_t sidebar_width = 128U;
    draw_rect(sidebar_x, sidebar_y, sidebar_width, files_window.height - 77U,
        0xe6101d2cU);
    static const char *const places[] = {
        "Home", "Desktop", "Documents", "Downloads", "Music", "Pictures",
        "Videos", "Trash"
    };
    for (uint32_t place = 0; place < 8U; ++place) {
        uint32_t place_y = sidebar_y + 10U + place * 25U;
        if (place == 0U) {
            draw_rounded_rect(sidebar_x + 5U, place_y - 3U,
                sidebar_width - 10U, 22U, 5U, 0xc00078d4U);
        }
        draw_icon(sidebar_x + 10U, place_y - 2U, QUANTA_DESKTOP_ICON_FILES);
        draw_text(sidebar_x + 39U, place_y + 3U, places[place],
            place == 0U ? 0x00ffffffU : 0x00dce9f6U);
    }
    uint32_t device_y = sidebar_y + 220U;
    draw_rect(sidebar_x + 10U, device_y, sidebar_width - 20U, 1U, 0x70445a72U);
    draw_text(sidebar_x + 12U, device_y + 10U, "Devices", 0x008da9c7U);
    draw_icon(sidebar_x + 10U, device_y + 31U, QUANTA_DESKTOP_ICON_FILES);
    draw_text(sidebar_x + 39U, device_y + 36U, "Local Disk", 0x00dce9f6U);

    if (system_info(&info) && info.mount_read_only)
        draw_text(x + 145U, y + 78U, "LIVE VOLUME / READ ONLY", 0x009eb7d0U);
    uint32_t count = explorer_count();
    if (explorer_selection >= count && count != 0U)
        explorer_selection = count - 1U;
    uint32_t grid_x = x + 142U;
    uint32_t grid_y = y + 86U;
    uint32_t columns = (width - 150U) / 112U;
    if (columns == 0U) columns = 1U;
    for (uint32_t entry = 0; entry < count && entry < 24U; ++entry) {
        char name[QUANTA_NAME_MAX + 1U];
        if (explorer_entry(entry, name, sizeof(name)) == 0U) break;
        uint32_t column = entry % columns;
        uint32_t row = entry / columns;
        uint32_t item_x = grid_x + column * 112U;
        uint32_t item_y = grid_y + row * (explorer_list_view ? 34U : 88U);
        char path[PATH_SIZE];
        struct quanta_file_stat stat;
        uint32_t icon = QUANTA_DESKTOP_ICON_FILES;
        if (explorer_entry_path(entry, path, sizeof(path)) == 0 &&
            call(QUANTA_SYSCALL_FS_STAT, 1, path, &stat, sizeof(stat)) == 0 &&
            stat.type != QUANTA_FILE_DIRECTORY) icon = QUANTA_DESKTOP_ICON_FILE;
        if (explorer_list_view) {
            if (entry == explorer_selection)
                draw_rounded_rect(item_x, item_y - 3U, width - 154U, 28U,
                    5U, 0x80406c91U);
            draw_icon(item_x + 6U, item_y, icon);
            draw_text(item_x + 38U, item_y + 4U, name, 0x00e5edf6U);
        } else {
            if (entry == explorer_selection)
                draw_rounded_rect(item_x + 10U, item_y - 5U, 72U, 78U,
                    7U, 0x70406c91U);
            draw_rounded_rect(item_x + 19U, item_y, 52U, 48U, 8U,
                icon == QUANTA_DESKTOP_ICON_FILES ? 0xff168de0U : 0xd0344b68U);
            draw_icon(item_x + 33U, item_y + 12U, icon);
            uint32_t text_x = item_x + 8U;
            draw_text(text_x, item_y + 56U, name, 0x00e5edf6U);
        }
    }
}

static void draw_panel_and_dock(void) {
    uint32_t dock_width = screen_width * 67U / 1000U;
    if (dock_width < 64U) dock_width = 64U;
    draw_rect(0U, 0U, screen_width, 36U, 0xd6102030U);
    draw_rect(0U, 36U, dock_width, screen_height - 36U, 0xd6102030U);
    draw_text(12U, 10U, cwd, 0x00e5edf6U);
    uint32_t status_x = screen_width > 180U ? screen_width - 126U : dock_width;
    draw_icon(status_x, 6U, QUANTA_DESKTOP_ICON_WIFI);
    draw_icon(status_x + 28U, 6U, QUANTA_DESKTOP_ICON_SPEAKER);
    draw_icon(status_x + 56U, 6U, QUANTA_DESKTOP_ICON_BATTERY);
    draw_text(status_x + 86U, 10U, "12:34", 0x00e5edf6U);
    static const char *const items[] = { "Settings", "Files", "Terminal" };
    static const uint32_t icons[] = {
        QUANTA_DESKTOP_ICON_SETTINGS, QUANTA_DESKTOP_ICON_FILES,
        QUANTA_DESKTOP_ICON_TERMINAL
    };
    for (uint32_t item = 0; item < 3U; ++item) {
        uint32_t y = 56U + item * 88U;
        uint32_t icon_x = (dock_width - QUANTA_DESKTOP_ICON_SIZE) / 2U;
        if (item == desktop_selection)
            draw_rounded_rect(4U, y - 4U, dock_width - 8U, 70U, 9U,
                0x90405b76U);
        draw_icon(icon_x, y, icons[item]);
        draw_text(2U, y + 32U, items[item], 0x00dce9f6U);
    }
    draw_icon((dock_width - QUANTA_DESKTOP_ICON_SIZE) / 2U,
        screen_height - 54U, QUANTA_DESKTOP_ICON_MENU);
    draw_rounded_rect(dock_width / 2U - 9U, screen_height - 20U,
        18U, 3U, 1U, 0xff168de0U);
}

static void draw_workspace(void) {
    desktop_view = VIEW_WORKSPACE;
    draw_begin(QUANTA_DESKTOP_DRAW_WALLPAPER);
    draw_panel_and_dock();
    for (uint32_t pass = 0; pass < 2U; ++pass) {
        if (files_window.open && !files_window.minimized &&
            (files_window.focused == (pass != 0U))) draw_files_window();
        if (settings_window.open && !settings_window.minimized &&
            (settings_window.focused == (pass != 0U))) draw_settings_window();
        if (terminal_open && !terminal_minimized &&
            (terminal_focused == (pass != 0U))) draw_terminal_window();
    }
    draw_cursor();
    draw_present();
}

static void draw_window_frame(const char *title, const struct desktop_window *window) {
    uint32_t title_color = window->focused ? 0xd51a2b3eU : 0xd51b2837U;
    draw_rounded_rect(window->x + 7U, window->y + 9U, window->width,
        window->height, 11U, 0x80405061U);
    draw_rounded_rect(window->x, window->y, window->width, window->height,
        10U, 0xe60d1724U);
    draw_rounded_rect(window->x + 1U, window->y + 1U, window->width - 2U,
        36U, 9U, title_color);
    draw_rect(window->x + 1U, window->y + 19U, window->width - 2U, 18U,
        title_color);
    draw_rounded_rect(window->x + 1U, window->y + 34U, window->width - 2U,
        window->height - 35U, 9U, 0xc5121e2bU);
    draw_rect(window->x + 1U, window->y + 42U, window->width - 2U,
        window->height - 51U, 0xc5121e2bU);
    draw_text(window->x + 14U, window->y + 10U, title, 0x00e8f4ffU);
    draw_text(window->x + window->width - 74U, window->y + 10U, "-", 0x00dce9f6U);
    draw_text(window->x + window->width - 49U, window->y + 10U, "[]", 0x00dce9f6U);
    draw_text(window->x + window->width - 22U, window->y + 10U, "x", 0x00dce9f6U);
}

static void terminal_advance_line(void) {
    if (terminal_line + 1U < 18U) {
        ++terminal_line;
    } else {
        for (uint32_t row = 0; row < 17U; ++row) {
            for (uint32_t column = 0; column < 81U; ++column)
                terminal_lines[row][column] = terminal_lines[row + 1U][column];
        }
    }
    terminal_column = 0;
}

static void terminal_capture(const char *text) {
    for (uint32_t index = 0; text[index]; ++index) {
        char character = text[index];
        if (character == '\n') terminal_advance_line();
        else if (character == '\r') terminal_column = 0;
        else if (character == '\b') {
            if (terminal_column != 0U) terminal_lines[terminal_line][--terminal_column] = ' ';
        } else if ((uint8_t)character >= 32U && (uint8_t)character < 127U) {
            terminal_lines[terminal_line][terminal_column++] = character;
            if (terminal_column >= 80U) terminal_advance_line();
        }
    }
    terminal_lines[terminal_line][terminal_column] = 0;
}

static void draw_terminal_window(void) {
    if (!terminal_open) return;
    uint32_t title_color = terminal_focused ? 0x00234756U : 0x00384d55U;
    draw_rect(terminal_x + 6U, terminal_y + 8U, terminal_width, terminal_height, 0x00213843U);
    draw_rect(terminal_x, terminal_y, terminal_width, terminal_height, 0x000d141aU);
    draw_rect(terminal_x, terminal_y, terminal_width, 34U, title_color);
    draw_text(terminal_x + 14U, terminal_y + 10U, "Terminal  |  ", 0x00e8f4f5U);
    draw_text(terminal_x + 14U + 16U * 8U, terminal_y + 10U, cwd, 0x00e8f4f5U);
    draw_rect(terminal_x + terminal_width - 48U, terminal_y + 8U, 14U, 14U, 0x00e0ad55U);
    draw_rect(terminal_x + terminal_width - 26U, terminal_y + 8U, 14U, 14U, 0x00db665eU);
    draw_rect(terminal_x + 10U, terminal_y + 44U, terminal_width - 20U,
        terminal_height - 54U, 0x0017202eU);
    for (uint32_t row = 0; row < 18U; ++row) {
        uint32_t active_rows = terminal_line + 1U;
        uint32_t first_screen_row = 18U - active_rows;
        uint32_t source_row = row < first_screen_row ? 18U : row - first_screen_row;
        if (terminal_line >= 17U) source_row = (terminal_line + 1U + row) % 18U;
        uint32_t y = terminal_y + 52U + row * 19U;
        char text[81];
        if (source_row == 18U) {
            for (uint32_t column = 0; column < 80U; ++column) text[column] = ' ';
        } else {
            for (uint32_t column = 0; column < 80U; ++column)
                text[column] = terminal_lines[source_row][column];
        }
        text[80] = 0;
        uint32_t cwd_length = 0U;
        while (cwd[cwd_length] && text[cwd_length] == cwd[cwd_length]) ++cwd_length;
        if (cwd_length != 0U && cwd[cwd_length] == 0 && text[cwd_length] == '>') {
            draw_text(terminal_x + 20U, y, cwd, 0x0000a9dfU);
            draw_text(terminal_x + 20U + cwd_length * 8U, y, ">", 0x0000c5a8U);
            draw_text(terminal_x + 20U + (cwd_length + 1U) * 8U, y,
                text + cwd_length + 1U, 0x00e8edf2U);
        } else {
            draw_text(terminal_x + 20U, y, text, 0x00e8edf2U);
        }
    }
    if (terminal_focused) {
        uint32_t cursor_x = terminal_x + 20U + (terminal_column + 2U) * 8U;
        uint32_t cursor_y = terminal_y + 52U + 2U * 19U;
        draw_rect(cursor_x, cursor_y, 8U, 2U, 0x00ffffffU);
        draw_rect(cursor_x, cursor_y + 14U, 8U, 2U, 0x00ffffffU);
        draw_rect(cursor_x, cursor_y, 2U, 16U, 0x00ffffffU);
        draw_rect(cursor_x + 6U, cursor_y, 2U, 16U, 0x00ffffffU);
    }
    draw_rect(terminal_x + terminal_width - 14U,
        terminal_y + terminal_height - 14U, 10U, 10U, 0x005a8994U);
}

static int window_contains(const struct desktop_window *window,
    uint32_t x, uint32_t y) {
    return window->open && !window->minimized && x >= window->x &&
        x < window->x + window->width && y >= window->y &&
        y < window->y + window->height;
}

static void window_pointer_down(struct desktop_window *window,
    uint32_t x, uint32_t y) {
    window->focused = 1U;
    if (x >= window->x + window->width - 30U && y < window->y + 34U) {
        window->open = 0U;
        window->focused = 0U;
    } else if (x >= window->x + window->width - 55U && y < window->y + 36U) {
        if (!window->maximized) {
            window->restore_x = window->x;
            window->restore_y = window->y;
            window->restore_width = window->width;
            window->restore_height = window->height;
            uint32_t dock_width = screen_width * 67U / 1000U;
            if (dock_width < 64U) dock_width = 64U;
            window->x = dock_width;
            window->y = 36U;
            window->width = screen_width - dock_width;
            window->height = screen_height - 36U;
            window->maximized = 1U;
        } else {
            window->x = window->restore_x;
            window->y = window->restore_y;
            window->width = window->restore_width;
            window->height = window->restore_height;
            window->maximized = 0U;
        }
    } else if (x >= window->x + window->width - 81U && y < window->y + 36U) {
        window->minimized = 1U;
        window->focused = 0U;
    } else if (y < window->y + 36U) {
        if (window->maximized) {
            window->x = window->restore_x;
            window->y = window->restore_y;
            window->width = window->restore_width;
            window->height = window->restore_height;
            window->maximized = 0U;
        }
        window->dragging = 1U;
        window->drag_x = x - window->x;
        window->drag_y = y - window->y;
    } else if (x >= window->x + window->width - 20U &&
        y >= window->y + window->height - 20U) {
        window->dragging = 2U;
        window->drag_x = x;
        window->drag_y = y;
    }
}

static void files_pointer_down(uint32_t x, uint32_t y) {
    uint32_t relative_y = y - files_window.y;
    if (relative_y >= 40U && relative_y < 72U) {
        if (x >= files_window.x + files_window.width - 108U &&
            x < files_window.x + files_window.width - 44U) {
            explorer_list_view = !explorer_list_view;
        } else if (x < files_window.x + 40U && cwd[0] != 0) {
            uint32_t length = 0U, parent = 0U;
            while (cwd[length]) {
                if (cwd[length] == '>') parent = length;
                ++length;
            }
            if (parent != 0U) {
                cwd[parent] = 0;
                explorer_listing_valid = 0U;
            }
            explorer_selection = 0U;
        }
        return;
    }
    if (relative_y >= 86U && x < files_window.x + 130U) {
        static const char *const places[] = {
            "", "Desktop", "Documents", "Downloads", "Music", "Pictures",
            "Videos", "Trash"
        };
        uint32_t place = (relative_y - 86U) / 25U;
        if (place < 8U) {
            reset_home_cwd();
            if (place != 0U) {
                uint32_t length = 0U;
                while (cwd[length] && length + 1U < sizeof(cwd)) ++length;
                if (length + 1U < sizeof(cwd)) cwd[length++] = '>';
                for (uint32_t index = 0; places[place][index] &&
                    length + 1U < sizeof(cwd); ++index)
                    cwd[length++] = places[place][index];
                cwd[length] = 0;
            }
            explorer_selection = 0U;
            explorer_listing_valid = 0U;
        }
        return;
    }
    if (relative_y < 86U || x < files_window.x + 136U) return;
    uint32_t columns = (files_window.width - 150U) / 112U;
    if (columns == 0U) columns = 1U;
    uint32_t column = (x - files_window.x - 142U) / 112U;
    uint32_t item_y = explorer_list_view ? 86U : 86U;
    uint32_t row = (relative_y - item_y) / (explorer_list_view ? 34U : 88U);
    uint32_t entry = row * columns + column;
    if (column >= columns || entry >= explorer_count()) return;
    if (explorer_selection == entry) explorer_open_selected();
    else explorer_selection = entry;
}

static void launch_terminal(void) {
    desktop_selection = 2U;
    files_window.focused = 0U;
    settings_window.focused = 0U;
    terminal_open = 1U;
    terminal_minimized = 0U;
    terminal_focused = 1U;
    desktop_view = VIEW_TERMINAL;
    if (!terminal_started) {
        prompt();
        terminal_started = 1U;
    }
    static const char terminal_focused_marker[] = "QUANTA_APP_TERMINAL_FOCUSED\n";
    call(QUANTA_SYSCALL_CONSOLE_WRITE, 1, terminal_focused_marker, 0,
        sizeof(terminal_focused_marker) - 1U);
}

static void desktop_mouse_event(int input) {
    uint32_t old_buttons = pointer_buttons;
    uint32_t old_x = pointer_x, old_y = pointer_y;
    pointer_x = QUANTA_MOUSE_EVENT_X(input);
    pointer_y = QUANTA_MOUSE_EVENT_Y(input);
    pointer_buttons = QUANTA_MOUSE_EVENT_BUTTONS(input);
    if ((pointer_buttons & 1U) != 0U) {
        if (!terminal_dragging && !terminal_resizing && (old_buttons & 1U) == 0U) {
            if (window_contains(&files_window, pointer_x, pointer_y)) {
                files_window.focused = 1U;
                settings_window.focused = 0U;
                terminal_focused = 0U;
                desktop_view = VIEW_FILES;
                window_pointer_down(&files_window, pointer_x, pointer_y);
                if (files_window.open && !files_window.minimized &&
                    files_window.dragging == 0U)
                    files_pointer_down(pointer_x, pointer_y);
                if (!files_window.open || files_window.minimized)
                    desktop_view = VIEW_WORKSPACE;
            } else if (window_contains(&settings_window, pointer_x, pointer_y)) {
                settings_window.focused = 1U;
                files_window.focused = 0U;
                terminal_focused = 0U;
                desktop_view = VIEW_SETTINGS;
                window_pointer_down(&settings_window, pointer_x, pointer_y);
                if (!settings_window.open || settings_window.minimized)
                    desktop_view = VIEW_WORKSPACE;
            } else if (terminal_open && !terminal_minimized && pointer_x >= terminal_x &&
                pointer_x < terminal_x + terminal_width && pointer_y >= terminal_y &&
                pointer_y < terminal_y + 34U) {
                files_window.focused = 0U;
                settings_window.focused = 0U;
                terminal_focused = 1U;
                if (pointer_x >= terminal_x + terminal_width - 30U) {
                    terminal_open = 0U;
                    terminal_focused = 0U;
                } else if (pointer_x >= terminal_x + terminal_width - 54U) {
                    terminal_minimized = 1U;
                    terminal_focused = 0U;
                    desktop_view = VIEW_WORKSPACE;
                } else {
                    terminal_dragging = 1U;
                    terminal_drag_x = pointer_x - terminal_x;
                    terminal_drag_y = pointer_y - terminal_y;
                }
            } else if (terminal_open && !terminal_minimized &&
                pointer_x >= terminal_x + terminal_width - 20U &&
                pointer_y >= terminal_y + terminal_height - 20U &&
                pointer_x < terminal_x + terminal_width &&
                pointer_y < terminal_y + terminal_height) {
                terminal_resizing = 1U;
                terminal_resize_x = pointer_x;
                terminal_resize_y = pointer_y;
                terminal_resize_width = terminal_width;
                terminal_resize_height = terminal_height;
            } else if (pointer_x < (screen_width * 67U / 1000U < 64U ?
                64U : screen_width * 67U / 1000U) && pointer_y >= 52U &&
                pointer_y < 52U + 3U * 88U) {
                uint32_t app = (pointer_y - 52U) / 88U;
                desktop_selection = app;
                if (app == 0U) draw_settings();
                else if (app == 1U) launch_files();
                else launch_terminal();
            } else if (terminal_open && !terminal_minimized && pointer_x >= terminal_x &&
                pointer_x < terminal_x + terminal_width && pointer_y >= terminal_y &&
                pointer_y < terminal_y + terminal_height) {
                terminal_focused = 1U;
                desktop_view = VIEW_TERMINAL;
            } else {
                terminal_focused = 0U;
                desktop_view = VIEW_WORKSPACE;
            }
        }
        if (terminal_dragging) {
            terminal_x = pointer_x - terminal_drag_x;
            terminal_y = pointer_y - terminal_drag_y;
        } else if (files_window.dragging == 1U) {
            files_window.x = pointer_x - files_window.drag_x;
            files_window.y = pointer_y - files_window.drag_y;
        } else if (settings_window.dragging == 1U) {
            settings_window.x = pointer_x - settings_window.drag_x;
            settings_window.y = pointer_y - settings_window.drag_y;
        } else if (files_window.dragging == 2U) {
            int32_t width = (int32_t)files_window.width +
                (int32_t)pointer_x - (int32_t)files_window.drag_x;
            int32_t height = (int32_t)files_window.height +
                (int32_t)pointer_y - (int32_t)files_window.drag_y;
            if (width < 420) width = 420;
            if (width > 1100) width = 1100;
            if (height < 280) height = 280;
            if (height > 700) height = 700;
            files_window.width = (uint32_t)width;
            files_window.height = (uint32_t)height;
            files_window.drag_x = pointer_x;
            files_window.drag_y = pointer_y;
        } else if (settings_window.dragging == 2U) {
            int32_t width = (int32_t)settings_window.width +
                (int32_t)pointer_x - (int32_t)settings_window.drag_x;
            int32_t height = (int32_t)settings_window.height +
                (int32_t)pointer_y - (int32_t)settings_window.drag_y;
            if (width < 400) width = 400;
            if (width > 1000) width = 1000;
            if (height < 260) height = 260;
            if (height > 680) height = 680;
            settings_window.width = (uint32_t)width;
            settings_window.height = (uint32_t)height;
            settings_window.drag_x = pointer_x;
            settings_window.drag_y = pointer_y;
        } else if (terminal_resizing) {
            int32_t width = (int32_t)terminal_resize_width +
                (int32_t)pointer_x - (int32_t)terminal_resize_x;
            int32_t height = (int32_t)terminal_resize_height +
                (int32_t)pointer_y - (int32_t)terminal_resize_y;
            if (width < 440) width = 440;
            if (width > 1100) width = 1100;
            if (height < 280) height = 280;
            if (height > 680) height = 680;
            terminal_width = (uint32_t)width;
            terminal_height = (uint32_t)height;
        }
    } else {
        terminal_dragging = 0U;
        terminal_resizing = 0U;
        files_window.dragging = 0U;
        settings_window.dragging = 0U;
    }
    if ((old_buttons != pointer_buttons) || terminal_dragging || terminal_resizing) {
        draw_workspace();
    } else {
        struct quanta_desktop_draw_command *wallpaper =
            draw_begin(QUANTA_DESKTOP_DRAW_WALLPAPER);
        wallpaper->x = old_x;
        wallpaper->y = old_y;
        wallpaper->width = 14U;
        wallpaper->height = 14U;
        if (old_x < screen_width * 67U / 1000U || pointer_x < screen_width * 67U / 1000U ||
            old_y < 36U || pointer_y < 36U) draw_panel_and_dock();
        if (files_window.open && !files_window.minimized &&
            old_x + 14U > files_window.x && old_x < files_window.x + files_window.width &&
            old_y + 14U > files_window.y && old_y < files_window.y + files_window.height)
            draw_files_window();
        if (settings_window.open && !settings_window.minimized &&
            old_x + 14U > settings_window.x && old_x < settings_window.x + settings_window.width &&
            old_y + 14U > settings_window.y && old_y < settings_window.y + settings_window.height)
            draw_settings_window();
        if (terminal_open && !terminal_minimized && old_x + 14U > terminal_x &&
            old_x < terminal_x + terminal_width && old_y + 14U > terminal_y &&
            old_y < terminal_y + terminal_height) draw_terminal_window();
        draw_cursor();
        draw_present();
    }
}

static void draw_settings_window(void) {
    struct quanta_system_info info;
    draw_window_frame("Settings", &settings_window);
    draw_text(settings_window.x + 20U, settings_window.y + 58U, "Appearance", desktop_foreground());
    draw_text(settings_window.x + 20U, settings_window.y + 90U, theme_dark ? "Theme: Dark (Enter toggles)" : "Theme: Light (Enter toggles)", desktop_foreground());
    if (system_info(&info)) {
        draw_text(settings_window.x + 20U, settings_window.y + 138U, info.mount_read_only ? "Boot volume: read-only" : "Boot volume: writable", desktop_foreground());
        draw_text(settings_window.x + 20U, settings_window.y + 170U, "Only the boot volume is mounted", desktop_foreground());
    }
    draw_text(settings_window.x + 20U, settings_window.y + 218U, "Enter toggles appearance", desktop_foreground());
}

static void draw_settings(void) {
    desktop_selection = 0U;
    settings_window.open = 1U;
    settings_window.minimized = 0U;
    settings_window.focused = 1U;
    terminal_focused = 0U;
    desktop_view = VIEW_SETTINGS;
    draw_workspace();
}

static void desktop_dispatch(int input) {
    if (desktop_view == VIEW_WORKSPACE) {
        if (input == QUANTA_KEY_UP) {
            if (desktop_selection != 0U) --desktop_selection;
            draw_workspace();
        } else if (input == QUANTA_KEY_DOWN) {
            if (desktop_selection < 2U) ++desktop_selection;
            draw_workspace();
        } else if (input == '\r' || input == '\n') {
            if (desktop_selection == 0U) draw_settings();
            else if (desktop_selection == 1U) launch_files();
            else launch_terminal();
        } else if (input == 'f' || input == 'F') launch_files();
        else if (input == 's' || input == 'S') draw_settings();
    } else if (desktop_view == VIEW_SETTINGS) {
        if (input == '\r' || input == '\n') {
            theme_dark = !theme_dark;
            draw_settings();
        } else if (input == 27 || input == 8 || input == 127) draw_workspace();
    } else if (desktop_view == VIEW_FILES) {
        if (input == QUANTA_KEY_UP) {
            if (explorer_selection != 0U) --explorer_selection;
            draw_files();
        } else if (input == QUANTA_KEY_DOWN) {
            if (explorer_selection + 1U < explorer_count()) ++explorer_selection;
            draw_files();
        } else if (input == '\r' || input == '\n') explorer_open_selected();
        else if (input == 27 || input == 8 || input == 127) {
            if (desktop_view == VIEW_PREVIEW) draw_files();
            else {
                uint32_t length = 0, parent = 0;
                while (cwd[length]) { if (cwd[length] == '>') parent = length; ++length; }
                if (parent != 0U) cwd[parent] = 0;
                else if (length > 6U) cwd[5] = 0;
                explorer_selection = 0;
                draw_files();
            }
        }
    } else if (desktop_view == VIEW_PREVIEW) {
        if (input == 27 || input == 8 || input == 127) draw_files();
    }
}

static void prompt(void) {
    write_text(cwd);
    write_text("> ");
}

static const char *filesystem_name(uint32_t kind) {
    if (kind == QUANTA_FILESYSTEM_QFS1) return "qfs1";
    if (kind == QUANTA_FILESYSTEM_QFS2) return "qfs2";
    if (kind == QUANTA_FILESYSTEM_VFAT) return "vfat";
    if (kind == QUANTA_FILESYSTEM_FAT) return "fat";
    return "unknown";
}

static int system_info(struct quanta_system_info *info) {
    return call(QUANTA_SYSCALL_SYSTEM_INFO, 1, 0, info, sizeof(*info)) == 0;
}

static void storage_list(void) {
    struct quanta_storage_info devices[10];
    char size[24];
    long count = call(QUANTA_SYSCALL_STORAGE_LIST, 1, 0, devices, sizeof(devices));
    if (count < 0) {
        write_text("storage: unavailable\n");
        return;
    }
    write_text("DRIVE  SIZE       STATE\n");
    for (long index = 0; index < count; ++index) {
        write_text(devices[index].name);
        write_text("  ");
        decimal(size, devices[index].sector_count * 512U);
        write_text(size);
        write_text(" bytes  ");
        if ((devices[index].flags & QUANTA_STORAGE_LIVE) != 0U) write_text("live read-only");
        else if ((devices[index].flags & QUANTA_STORAGE_READ_ONLY) != 0U) write_text("read-only");
        else write_text("ready");
        write_text("\n");
    }
}

static void select_boot_drive(void) {
    struct quanta_system_info info;
    const char *name = system_info(&info) && info.mount_read_only ? "live" : "prime";
    uint64_t index = 0;
    while (name[index]) {
        active_drive[index] = name[index];
        ++index;
    }
    active_drive[index] = 0;
}

static int path_add_components(const char *text, char parts[8][24], uint32_t *count) {
    uint32_t index = 0;
    while (text[index]) {
        uint32_t start = index;
        while (text[index] && text[index] != '>') {
            if (text[index] == '/' || text[index] == '\\' || text[index] == ':') return -1;
            ++index;
        }
        uint32_t length = index - start;
        if (length == 1U && text[start] == '.') {
        } else if (length == 2U && text[start] == '.' && text[start + 1U] == '.') {
            if (*count != 0U) --*count;
        } else if (length != 0U) {
            if (*count >= 8U || length >= sizeof(parts[0])) return -1;
            for (uint32_t character = 0; character < length; ++character) {
                parts[*count][character] = text[start + character];
            }
            parts[*count][length] = 0;
            ++*count;
        }
        if (text[index] == '>') ++index;
    }
    return 0;
}

static int path_resolve(const char *input, char *output) {
    char parts[8][24];
    const char *cwd_path = cwd;
    uint32_t count = 0, drive_length = 0, output_length = 0;
    int explicit_drive = 0;
    if (input == 0 || output == 0) return -1;
    while (*input == ' ') ++input;
    while (input[drive_length] && input[drive_length] != ':' && input[drive_length] != '>') {
        if (input[drive_length] == '/') return -1;
        ++drive_length;
    }
    if (input[drive_length] == ':') {
        char requested_drive[8];
        if (drive_length == 0U || drive_length >= sizeof(requested_drive)) return -1;
        for (uint32_t index = 0; index < drive_length; ++index) requested_drive[index] = input[index];
        requested_drive[drive_length] = 0;
        if ((!equal(requested_drive, "prime") && !equal(requested_drive, "live")) ||
            !equal(requested_drive, active_drive)) return -1;
        explicit_drive = 1;
        input += drive_length + 1U;
    } else if (input[0] == '/' || input[0] == '\\' || input[1] == ':') {
        return -1;
    }
    if (!explicit_drive) {
        while (*cwd_path && *cwd_path != ':') ++cwd_path;
        if (*cwd_path != ':') return -1;
        if (path_add_components(cwd_path + 1, parts, &count) != 0) return -1;
    }
    if (path_add_components(input, parts, &count) != 0) return -1;
    while (active_drive[output_length] && output_length + 1U < PATH_SIZE) {
        output[output_length] = active_drive[output_length];
        ++output_length;
    }
    if (output_length + 1U >= PATH_SIZE) return -1;
    output[output_length++] = ':';
    for (uint32_t part = 0; part < count; ++part) {
        uint32_t character = 0;
        if (part != 0U) {
            if (output_length + 1U >= PATH_SIZE) return -1;
            output[output_length++] = '>';
        }
        while (parts[part][character]) {
            if (output_length + 1U >= PATH_SIZE) return -1;
            output[output_length++] = parts[part][character++];
        }
    }
    output[output_length] = 0;
    return 0;
}

static void set_cwd(const char *path) {
    uint64_t index = 0;
    while (path[index] && index + 1U < PATH_SIZE) {
        cwd[index] = path[index];
        ++index;
    }
    cwd[index] = 0;
    explorer_listing_valid = 0U;
}

static void explorer_refresh(void) {
    if (explorer_listing_valid && equal(explorer_cached_path, cwd)) return;
    long length = call(QUANTA_SYSCALL_FS_LIST, 1, cwd, explorer_listing,
        sizeof(explorer_listing));
    if (length < 0) {
        explorer_listing[0] = 0;
        explorer_listing_count = 0U;
        explorer_listing_valid = 0U;
        return;
    }
    uint32_t index = 0U;
    while (cwd[index] && index + 1U < sizeof(explorer_cached_path)) {
        explorer_cached_path[index] = cwd[index];
        ++index;
    }
    explorer_cached_path[index] = 0;
    explorer_listing_count = 0U;
    for (index = 0U; explorer_listing[index];) {
        while (explorer_listing[index] == ' ' || explorer_listing[index] == '\n') ++index;
        if (explorer_listing[index] == 0) break;
        ++explorer_listing_count;
        while (explorer_listing[index] && explorer_listing[index] != ' ' &&
            explorer_listing[index] != '\n') ++index;
    }
    explorer_listing_valid = 1U;
}

static uint32_t explorer_entry(uint32_t requested, char *name, uint32_t capacity) {
    explorer_refresh();
    if (!explorer_listing_valid) return 0U;
    uint32_t position = 0, entry = 0;
    while (explorer_listing[position]) {
        while (explorer_listing[position] == ' ' || explorer_listing[position] == '\n') ++position;
        if (explorer_listing[position] == 0) break;
        uint32_t start = position;
        while (explorer_listing[position] && explorer_listing[position] != ' ' &&
            explorer_listing[position] != '\n') ++position;
        if (entry++ != requested) continue;
        uint32_t length = position - start;
        if (length + 1U > capacity) return 0;
        for (uint32_t index = 0; index < length; ++index)
            name[index] = explorer_listing[start + index];
        name[length] = 0;
        return entry;
    }
    return 0;
}

static uint32_t explorer_count(void) {
    explorer_refresh();
    return explorer_listing_valid ? explorer_listing_count : 0U;
}

static int explorer_entry_path(uint32_t requested, char *path, uint32_t capacity) {
    char name[QUANTA_NAME_MAX + 1U];
    if (path == 0 || capacity == 0U || explorer_entry(requested, name,
        sizeof(name)) == 0U) return -1;
    uint32_t index = 0U;
    while (cwd[index] && index + 1U < capacity) {
        path[index] = cwd[index];
        ++index;
    }
    if (cwd[index] || index + 1U >= capacity) return -1;
    if (index != 0U && path[index - 1U] != ':') path[index++] = '>';
    for (uint32_t character = 0; name[character]; ++character) {
        if (index + 1U >= capacity) return -1;
        path[index++] = name[character];
    }
    path[index] = 0;
    return 0;
}

static void draw_files(void) {
    struct quanta_system_info info;
    if (system_info(&info) && info.display_width != 0U && info.display_height != 0U) {
        screen_width = info.display_width;
        screen_height = info.display_height;
    }
    files_window.x = screen_width * 145U / 1000U;
    files_window.y = screen_height * 122U / 1000U;
    files_window.width = screen_width * 489U / 1000U;
    files_window.height = screen_height * 496U / 1000U;
    if (files_window.width < 460U) files_window.width = 460U;
    if (files_window.height < 340U) files_window.height = 340U;
    files_window.open = 1U;
    files_window.minimized = 0U;
    files_window.focused = 1U;
    desktop_selection = 1U;
    settings_window.focused = 0U;
    terminal_focused = 0U;
    desktop_view = VIEW_FILES;
    draw_workspace();
}

static void launch_files(void) {
    reset_home_cwd();
    explorer_listing_valid = 0U;
    explorer_selection = 0U;
    draw_files();
    static const char files_focused_marker[] = "QUANTA_APP_FILES_FOCUSED\n";
    call(QUANTA_SYSCALL_CONSOLE_WRITE, 1, files_focused_marker, 0,
        sizeof(files_focused_marker) - 1U);
}

static void draw_file_preview(const char *path) {
    char content[QUANTA_SYSCALL_MAX_BUFFER];
    long length = call(QUANTA_SYSCALL_FS_READ, 1, path, content, sizeof(content));
    desktop_view = VIEW_PREVIEW;
    draw_workspace();
    draw_rect(files_window.x + 10U, files_window.y + 44U,
        files_window.width - 20U, files_window.height - 54U, 0x0017202eU);
    draw_text(files_window.x + 20U, files_window.y + 52U, path, desktop_foreground());
    if (length < 0) {
        draw_text(files_window.x + 20U, files_window.y + 90U, "Preview unavailable (binary or too large)", desktop_foreground());
    } else {
        uint32_t row = 0, column = 0;
        for (long index = 0; index < length && row < 14U; ++index) {
            char character[2] = { content[index], 0 };
            if (content[index] == '\n' || column == 68U) { ++row; column = 0; continue; }
            if ((uint8_t)content[index] >= 32U && (uint8_t)content[index] < 127U) {
                draw_text(files_window.x + 20U + column * 8U,
                    files_window.y + 90U + row * 18U, character, desktop_foreground());
                ++column;
            }
        }
    }
    draw_text(files_window.x + 20U, files_window.y + files_window.height - 48U,
        "Backspace returns to Files", desktop_foreground());
    draw_present();
}

static void explorer_open_selected(void) {
    char name[QUANTA_NAME_MAX + 1U];
    char path[PATH_SIZE];
    struct quanta_file_stat stat;
    if (explorer_entry(explorer_selection, name, sizeof(name)) == 0U) return;
    uint32_t index = 0;
    while (cwd[index] && index + 1U < sizeof(path)) { path[index] = cwd[index]; ++index; }
    if (index > 0U && path[index - 1U] != ':') path[index++] = '>';
    for (uint32_t character = 0; name[character] && index + 1U < sizeof(path); ++character)
        path[index++] = name[character];
    path[index] = 0;
    if (call(QUANTA_SYSCALL_FS_STAT, 1, path, &stat, sizeof(stat)) != 0) return;
    if (stat.type == QUANTA_FILE_DIRECTORY) {
        set_cwd(path);
        explorer_selection = 0;
        draw_files();
    } else {
        draw_file_preview(path);
    }
}

static void reset_home_cwd(void) {
    uint64_t index = 0;
    while (active_drive[index] && index + 1U < PATH_SIZE) {
        cwd[index] = active_drive[index];
        ++index;
    }
    cwd[index++] = ':';
    const char *home = "home>quanta";
    while (*home && index + 1U < PATH_SIZE) cwd[index++] = *home++;
    cwd[index] = 0;
}

static void login_geometry(uint32_t *field_x, uint32_t *field_width,
    uint32_t *username_y, uint32_t *password_y, uint32_t *button_y) {
    *field_width = screen_width * 38U / 100U;
    if (*field_width < 300U) *field_width = 300U;
    if (*field_width > 440U) *field_width = 440U;
    *field_x = (screen_width - *field_width) / 2U;
    uint32_t title_y = screen_height / 2U - 130U;
    *username_y = title_y + 104U;
    *password_y = title_y + 174U;
    *button_y = title_y + 246U;
}

static void draw_login_screen(const char *username, const char *password,
    uint32_t focused_field, int failed, uint32_t draw_background) {
    char masked[65];
    uint32_t field_x, field_width, username_y, password_y, button_y;
    uint32_t title_y = screen_height / 2U - 130U;
    uint32_t length = 0;
    while (password[length] && length + 1U < sizeof(masked)) {
        masked[length] = '*';
        ++length;
    }
    masked[length] = 0;
    login_geometry(&field_x, &field_width, &username_y, &password_y, &button_y);
    if (draw_background) {
        draw_begin(QUANTA_DESKTOP_DRAW_WALLPAPER);
        draw_rect(0U, 0U, screen_width, screen_height, 0x95071322U);
        draw_text(screen_width / 2U - 32U, title_y, "QuantaOS", 0x00f1f7ffU);
        draw_text(screen_width / 2U - 88U, title_y + 30U,
            "Welcome back, Quanta", 0x00b8cce0U);
        draw_text(field_x, username_y - 20U, "Account", 0x00c5d7e8U);
        draw_text(field_x, password_y - 20U, "Password", 0x00c5d7e8U);
    }
    draw_rounded_rect(field_x, username_y, field_width, 42U, 10U,
        focused_field == 0U ? 0xe02a4058U : 0xc0213144U);
    draw_text(field_x + 16U, username_y + 13U,
        username[0] ? username : "quanta",
        username[0] ? 0x00e8f4ffU : 0x006f9abeU);
    draw_rounded_rect(field_x, password_y, field_width, 42U, 10U,
        failed ? 0xd4553640U :
        (focused_field == 1U ? 0xe02a4058U : 0xc0213144U));
    draw_text(field_x + 16U, password_y + 13U,
        masked[0] ? masked : "Enter your password",
        masked[0] ? 0x00e8f4ffU : 0x006f9abeU);
    draw_rounded_rect(field_x, button_y, field_width, 42U, 10U, 0xff168de0U);
    draw_text(screen_width / 2U - 28U, button_y + 13U, "Sign in", 0x00ffffffU);
    if (failed) draw_text(field_x, button_y + 54U,
        "Incorrect password. Try again.", 0x00ffaaaaU);
    draw_cursor();
    draw_present();
}

static int split_command(const char *line, char *name, uint64_t name_size,
    const char **args) {
    uint64_t index = 0, length = 0;
    while (line[index] == ' ') ++index;
    while (line[index] && line[index] != ' ') {
        if (length + 1U >= name_size) return -1;
        name[length++] = line[index++];
    }
    name[length] = 0;
    while (line[index] == ' ') ++index;
    *args = line + index;
    return length == 0U ? -1 : 0;
}

static int login(void) {
    char username[32];
    char password[65];
    uint32_t temporary;
    uint32_t failed = 0U;
    uint32_t focus = 0U;
    uint64_t username_length = 0U;
    uint64_t password_length = 0U;
    uint32_t previous_buttons = 0U;
    struct quanta_system_info display_info;
    if (system_info(&display_info) && display_info.display_width != 0U &&
        display_info.display_height != 0U) {
        screen_width = display_info.display_width;
        screen_height = display_info.display_height;
    }
    username[0] = 0;
    password[0] = 0;
    static const char login_screen_ready[] = "QUANTA_LOGIN_SCREEN_READY\n";
    draw_login_screen(username, password, focus, failed != 0U, 1U);
    call(QUANTA_SYSCALL_CONSOLE_WRITE, 1, login_screen_ready, 0,
        sizeof(login_screen_ready) - 1U);
    for (;;) {
        int input = read_char();
        uint32_t submit = 0U;
        if (input < 0) continue;
        if ((input & QUANTA_KEY_MOUSE) != 0) {
            uint32_t field_x, field_width, username_y, password_y, button_y;
            login_geometry(&field_x, &field_width, &username_y, &password_y,
                &button_y);
            pointer_x = QUANTA_MOUSE_EVENT_X(input);
            pointer_y = QUANTA_MOUSE_EVENT_Y(input);
            pointer_buttons = QUANTA_MOUSE_EVENT_BUTTONS(input);
            if ((pointer_buttons & 1U) != 0U && (previous_buttons & 1U) == 0U) {
                if (pointer_x >= field_x && pointer_x < field_x + field_width &&
                    pointer_y >= username_y && pointer_y < username_y + 42U) focus = 0U;
                else if (pointer_x >= field_x && pointer_x < field_x + field_width &&
                    pointer_y >= password_y && pointer_y < password_y + 42U) focus = 1U;
                else if (pointer_x >= field_x && pointer_x < field_x + field_width &&
                    pointer_y >= button_y && pointer_y < button_y + 42U) submit = 1U;
            }
            previous_buttons = pointer_buttons;
            failed = 0U;
            if (submit == 0U) {
                draw_login_screen(username, password, focus, 0, 1U);
                continue;
            }
            input = '\n';
        }
        if (input == '\t') {
            focus = focus == 0U ? 1U : 0U;
        } else if (input == 8 || input == 127) {
            if (focus == 0U && username_length != 0U) username[--username_length] = 0;
            if (focus == 1U && password_length != 0U) password[--password_length] = 0;
            failed = 0U;
        } else if (input >= 32 && input < 127) {
            if (focus == 0U && username_length + 1U < sizeof(username)) {
                username[username_length++] = (char)input;
                username[username_length] = 0;
            } else if (focus == 1U && password_length + 1U < sizeof(password)) {
                password[password_length++] = (char)input;
                password[password_length] = 0;
            }
            failed = 0U;
        } else if (input == '\r' || input == '\n') {
            if (focus == 0U) {
                focus = 1U;
                draw_login_screen(username, password, focus, failed != 0U, 0U);
                continue;
            }
            if (equal(username, "quanta") &&
                call(QUANTA_SYSCALL_AUTH_VERIFY, 1, password, &temporary,
                    password_length) == 0) {
            uint64_t index;
            for (index = 0; index + 1U < sizeof(logged_in_user) && username[index]; ++index)
                logged_in_user[index] = username[index];
            logged_in_user[index] = 0;
                select_boot_drive();
                reset_home_cwd();
                explorer_listing_valid = 0U;
                call(QUANTA_SYSCALL_CONSOLE_CLEAR, 1, 0, 0, 0);
            for (uint32_t row = 0; row < 18U; ++row)
                for (uint32_t column = 0; column < 81U; ++column)
                    terminal_lines[row][column] = 0;
            terminal_line = 0U;
            terminal_column = 0U;
            call(QUANTA_SYSCALL_CONSOLE_CLEAR, 1, 0, 0, 0);
            static const char login_ready[] = "QUANTA_LOGIN_READY\n";
            call(QUANTA_SYSCALL_CONSOLE_WRITE, 1, login_ready, 0,
                sizeof(login_ready) - 1U);
            return 1;
            }
            failed = 1U;
            password_length = 0U;
            password[0] = 0;
        }
        draw_login_screen(username, password, focus, failed != 0U, 0U);
    }
}

static void trim_line(char *line) {
    uint64_t length = 0;
    while (line[length]) ++length;
    while (length && line[length - 1U] == ' ') line[--length] = 0;
}

struct manual_entry {
    const char *name;
    const char *description;
    const char *usage;
};

static const struct manual_entry manuals[] = {
    { "ls", "list directory contents", "ls [-a] [-l] [PATH]" },
    { "cat", "print files", "cat FILE..." },
    { "head", "print the beginning of a file", "head [-n LINES] FILE" },
    { "grep", "search text for a pattern", "grep PATTERN FILE..." },
    { "mkdir", "create a directory", "mkdir PATH..." },
    { "rm", "remove files", "rm [-r] PATH..." },
    { "pwd", "print the current directory", "pwd" },
    { "which", "locate a command", "which COMMAND" },
    { "type", "describe command resolution", "type COMMAND" },
    { "echo", "write arguments to the console", "echo [TEXT...]" },
    { "printf", "format console output", "printf FORMAT [ARGUMENT...]" },
    { "uname", "print system identity", "uname [-a]" },
    { "date", "print the real-time clock", "date" },
    { "lsblk", "list block devices", "lsblk" },
    { "mount", "show or attach filesystems", "mount [DEVICE] [PATH]" },
    { "umount", "detach a filesystem", "umount PATH" },
    { "mkfs", "format a volume", "mkfs [-t exfat|qfs] DEVICE" },
    { "diskpart", "inspect and change partitions", "diskpart COMMAND" },
    { "sync", "flush filesystem changes", "sync" },
    { "ps", "list processes", "ps" },
    { "free", "show memory information", "free" },
    { "df", "show filesystem space", "df [PATH]" },
    { "id", "show the current identity", "id" },
    { "whoami", "print the current user", "whoami" },
    { "hostname", "print the system hostname", "hostname" },
    { "udrive", "manage userspace driver capsules", "udrive list|add|remove|start|stop|status" },
    { "sudrive", "manage administrator driver operations", "sudrive add|remove|start|stop|status" },
    { "help", "show command documentation", "help [COMMAND]" },
    { "man", "show command documentation", "man COMMAND" },
};

static void manual(const char *name) {
    uint32_t index;
    for (index = 0; index < sizeof(manuals) / sizeof(manuals[0]); ++index) {
        if (equal(name, manuals[index].name)) {
            write_text("NAME\n  "); write_text(manuals[index].name);
            write_text(" - "); write_text(manuals[index].description);
            write_text("\n\nSYNOPSIS\n  "); write_text(manuals[index].usage);
            write_text("\n\nDESCRIPTION\n  "); write_text(manuals[index].description);
            write_text(".\n\nEXIT STATUS\n  0 on success; non-zero on failure.\n");
            return;
        }
    }
    write_text("No manual entry for "); write_text(name); write_text(".\n");
}

static void interactive_help(void) {
    char name[32];
    uint32_t index;
    write_text("QuantaOs command guide\n\nCommands:\n");
    for (index = 0; index < sizeof(manuals) / sizeof(manuals[0]); ++index) {
        write_text("  "); write_text(manuals[index].name);
        if ((index + 1U) % 4U == 0U) write_text("\n");
        else write_text("  ");
    }
    if (index % 4U != 0U) write_text("\n");
    write_text("\nmanual for (or Enter to return): ");
    if (read_line(name, sizeof(name), 1) != 0U) manual(name);
}

static void command(const char *line) {
    char buffer[QUANTA_SYSCALL_MAX_BUFFER];
    char name[32];
    const char *args = "";
    if (split_command(line, name, sizeof(name), &args) != 0) return;
    if (equal(name, "help")) {
        if (args[0] == 0) interactive_help();
        else manual(args);
    }
    else if (equal(name, "man")) {
        if (args[0] == 0) write_text("man: usage: man COMMAND\n");
        else manual(args);
    }
    else if (equal(name, "whoami") || equal(name, "id")) write_text("quanta\n");
    else if (equal(name, "hostname")) write_text("quanta\n");
    else if (equal(name, "pwd")) { write_text(cwd); write_text("\n"); }
    else if (equal(name, "cd")) {
        char path[PATH_SIZE]; struct quanta_file_stat stat;
        const char *target = args[0] == 0 ? cwd : args;
        if (path_resolve(target, path) == 0 &&
            call(QUANTA_SYSCALL_FS_STAT, 1, path, &stat, sizeof(stat)) == 0 &&
            stat.type == QUANTA_FILE_DIRECTORY) set_cwd(path);
        else write_text("cd: no such directory\n");
    }
    else if (equal(name, "mkdir")) {
        char path[PATH_SIZE];
        if (args[0] == 0) write_text("mkdir: usage: mkdir PATH...\n");
        else if (path_resolve(args, path) == 0 &&
            call(QUANTA_SYSCALL_FS_MKDIR, 1, path, 0, 0) == 0) write_text("\n");
        else write_text("mkdir: failed\n");
    }
    else if (equal(name, "touch")) {
        char path[PATH_SIZE]; uint64_t handle;
        struct quanta_file_stat stat;
        if (args[0] == 0) write_text("touch: usage: touch FILE\n");
        else if (path_resolve(args, path) != 0) write_text("touch: failed\n");
        else if (call(QUANTA_SYSCALL_FS_STAT, 1, path, &stat, sizeof(stat)) == 0)
            write_text("\n");
        else if (call(QUANTA_SYSCALL_FS_OPEN, 1, path, &handle,
            QUANTA_OPEN_READ | QUANTA_OPEN_WRITE | QUANTA_OPEN_CREATE) == 0) {
            call(QUANTA_SYSCALL_FS_CLOSE, handle, 0, 0, 0);
            write_text("\n");
        } else write_text("touch: failed\n");
    }
    else if (equal(name, "ls")) {
        char path[PATH_SIZE]; const char *target = args[0] == 0 ? "." : args;
        long result = path_resolve(target, path) == 0 ?
            call(QUANTA_SYSCALL_FS_LIST, 1, path, buffer, sizeof(buffer)) : -1;
        if (result > 0) write_text(buffer);
        else if (result == 0) { }
        else write_text("ls: not found\n");
    }
    else if (equal(name, "cat")) {
        char path[PATH_SIZE]; long result;
        if (args[0] == 0) write_text("cat: usage: cat FILE...\n");
        else {
            result = path_resolve(args, path) == 0 ?
                call(QUANTA_SYSCALL_FS_READ, 1, path, buffer, sizeof(buffer)) : -1;
            if (result >= 0) write_text(buffer); else write_text("cat: not found\n");
        }
    }
    else if (equal(name, "echo")) { write_text(args); write_text("\n"); }
    else if (equal(name, "printf")) {
        if (args[0] == 0) write_text("printf: usage: printf FORMAT\n");
        else { write_text(args); write_text("\n"); }
    }
    else if (equal(name, "uname")) write_text("QuantaOs x86_64 single-user\n");
    else if (equal(name, "ps")) write_text("PID STATE NAME\n1 running session\n2 ready console\n");
    else if (equal(name, "mem") || equal(name, "free")) write_text("memory: bootstrap frame allocator active\n");
    else if (equal(name, "date")) {
        uint8_t clock[6];
        char stamp[] = "20xx-xx-xx xx:xx:xx UTC\n";
        if (call(QUANTA_SYSCALL_RTC_READ, 1, 0, clock, sizeof(clock)) == 0) {
            two_digits(stamp + 2, clock[0]); two_digits(stamp + 5, clock[1]);
            two_digits(stamp + 8, clock[2]); two_digits(stamp + 11, clock[3]);
            two_digits(stamp + 14, clock[4]); two_digits(stamp + 17, clock[5]);
            write_text(stamp);
        } else write_text("date: unavailable\n");
    }
    else if (equal(name, "uptime")) write_text("0:00, 1 user, load average: 0.00\n");
    else if (equal(name, "env")) {
        write_text("USER=quanta\nHOME="); write_text(active_drive);
        write_text(":home>quanta\nSHELL="); write_text(active_drive);
        write_text(":bin>session\n");
    }
    else if (equal(name, "history")) write_text("history is session-local\n");
    else if (equal(name, "tty")) write_text("console0\n");
    else if (equal(name, "lsblk")) {
        storage_list();
    }
    else if (equal(name, "df")) {
        struct quanta_system_info info;
        char size[24];
        if (system_info(&info)) {
            decimal(size, info.mount_size_bytes);
            write_text("Filesystem  Size       Used     Avail\n");
            write_text(filesystem_name(info.filesystem_kind)); write_text("        ");
            write_text(size); write_text(" bytes  unknown  unknown\n");
        } else write_text("df: unavailable\n");
    }
    else if (equal(name, "mount")) {
        struct quanta_system_info info;
        char size[24];
        if (system_info(&info)) {
            decimal(size, info.mount_size_bytes);
            write_text(active_drive); write_text(": type ");
            write_text(filesystem_name(info.filesystem_kind));
            write_text(info.mount_read_only ? " (read-only, " : " (read-write, ");
            write_text(size);
            write_text(" bytes)\n");
            storage_list();
        } else write_text("mount: unavailable\n");
    }
    else if (equal(name, "umount")) write_text("umount: named boot drive cannot be detached\n");
    else if (equal(name, "sync")) write_text("\n");
    else if (equal(name, "dmesg")) write_text("QuantaOs kernel: console ps2 rtc ramfs online\n");
    else if (equal(name, "drivers")) write_text("vga: online\nps2: online\nserial: online\nrtc: online\n");
    else if (equal(name, "udrive")) {
        if (args[0] == 0 || equal(args, "list"))
            write_text("NAME STATE CAPSULE\nconsole online builtin\nstorage online builtin\n");
        else write_text("udrive: driver capsule service is not available in this session\n");
    }
    else if (equal(name, "sudrive"))
        write_text("sudrive: administrator driver authority is required\n");
    else if (equal(name, "login")) write_text("already logged in as quanta\n");
    else if (equal(name, "which")) {
        if (args[0] == 0) write_text("which: usage: which COMMAND\n");
        else if (equal(args, "cat") || equal(args, "echo") || equal(args, "session") ||
            equal(args, "mkdir") || equal(args, "ls") || equal(args, "pwd")) {
            write_text(active_drive); write_text(":bin>"); write_text(args); write_text("\n");
        } else write_text("which: not found\n");
    }
    else if (equal(name, "type")) {
        if (args[0] == 0) write_text("type: usage: type COMMAND\n");
        else write_text("type: shell builtin\n");
    }
    else if (equal(name, "head")) {
        char path[PATH_SIZE]; long result;
        if (args[0] == 0) write_text("head: usage: head FILE\n");
        else {
            result = path_resolve(args, path) == 0 ?
                call(QUANTA_SYSCALL_FS_READ, 1, path, buffer, sizeof(buffer)) : -1;
            if (result >= 0) write_text(buffer); else write_text("head: not found\n");
        }
    }
    else if (equal(name, "wc")) {
        char path[PATH_SIZE]; long result;
        if (args[0] == 0) write_text("wc: usage: wc FILE\n");
        else {
            result = path_resolve(args, path) == 0 ?
                call(QUANTA_SYSCALL_FS_READ, 1, path, buffer, sizeof(buffer)) : -1;
            if (result >= 0) {
                uint64_t lines = 0, words = 0, bytes = 0; int in_word = 0;
                while (buffer[bytes]) {
                    if (buffer[bytes] == '\n') ++lines;
                    if (buffer[bytes] == ' ' || buffer[bytes] == '\n') in_word = 0;
                    else if (!in_word) { in_word = 1; ++words; }
                    ++bytes;
                }
                char output[64]; decimal(output, lines); write_text(output); write_text(" ");
                decimal(output, words); write_text(output); write_text(" ");
                decimal(output, bytes); write_text(output); write_text("\n");
            } else write_text("wc: not found\n");
        }
    }
    else if (equal(name, "basename")) {
        const char *last;
        if (args[0] == 0) write_text("basename: usage: basename PATH\n");
        else {
            last = args;
            while (*args) { if (*args == '>' || *args == ':') last = args + 1; ++args; }
            write_text(last); write_text("\n");
        }
    }
    else if (equal(name, "dirname")) {
        char path[PATH_SIZE];
        uint32_t index = 0, root_end = 0, parent_end = 0;
        if (args[0] == 0) write_text("dirname: usage: dirname PATH\n");
        else if (path_resolve(args, path) != 0) write_text("dirname: invalid path\n");
        else {
            while (path[index]) {
                if (path[index] == ':') root_end = index + 1U;
                if (path[index] == '>') parent_end = index;
                ++index;
            }
            path[parent_end != 0U ? parent_end : root_end] = 0;
            write_text(path); write_text("\n");
        }
    }
    else if (equal(name, "clear")) clear();
    else if (equal(name, "passwd")) {
        char current[65], next[65], confirm[65]; uint32_t temporary; uint64_t current_length;
        write_text("current password: ");
        current_length = read_line(current, sizeof(current), 0);
        if (call(QUANTA_SYSCALL_AUTH_VERIFY, 1, current, &temporary, current_length) != 0) {
            write_text("passwd: current password is incorrect\n");
        } else {
            write_text("new password: ");
            uint64_t next_length = read_line(next, sizeof(next), 0);
            write_text("confirm password: ");
            uint64_t confirm_length = read_line(confirm, sizeof(confirm), 0);
            if (next_length == 0U || next_length != confirm_length || !equal(next, confirm))
                write_text("passwd: new passwords do not match\n");
            else if (call(QUANTA_SYSCALL_AUTH_CHANGE, 1, next, 0, next_length) != 0)
                write_text("passwd: update failed\n");
            else write_text("passwd: password updated\n");
        }
    }
    else if (equal(name, "true")) { }
    else if (equal(name, "false")) write_text("false\n");
    else if (equal(name, "yes")) write_text("yes\n");
    else if (equal(name, "exit") || equal(name, "logout")) {
        call(QUANTA_SYSCALL_SESSION_LOGOUT, 1, 0, 0, 0);
        login();
    }
    else if (equal(name, "shutdown")) {
        call(QUANTA_SYSCALL_SHUTDOWN, 1, 0, 0, 0);
    }
    else if (equal(name, "reboot") || equal(name, "poweroff")) write_text("unsupported: use shutdown\n");
    else write_text("command not found\n");
}

void _start(void) {
    char line[BUFFER_SIZE];
    uint64_t length = 0;
    login();
    draw_workspace();
    static const char desktop_ready[] = "QUANTA_DESKTOP_READY\n";
    call(QUANTA_SYSCALL_CONSOLE_WRITE, 1, desktop_ready, 0,
        sizeof(desktop_ready) - 1U);
    for (;;) {
        int input = read_char();
        if (input < 0) continue;
        if ((input & QUANTA_KEY_MOUSE) != 0) {
            uint32_t old_buttons = pointer_buttons;
            desktop_mouse_event(input);
            if (old_buttons != pointer_buttons) {
                static const char mouse_marker[] = "QUANTA_MOUSE_CLICK\n";
                call(QUANTA_SYSCALL_CONSOLE_WRITE, 1, mouse_marker, 0,
                    sizeof(mouse_marker) - 1U);
            }
            continue;
        }
        if (input == QUANTA_KEY_ALT_F2) {
            desktop_view = VIEW_WORKSPACE;
            terminal_focused = 0U;
            draw_workspace();
            static const char workspace_marker[] = "QUANTA_VIEW_WORKSPACE\n";
            call(QUANTA_SYSCALL_CONSOLE_WRITE, 1, workspace_marker, 0,
                sizeof(workspace_marker) - 1U);
            continue;
        }
        if (input == QUANTA_KEY_ALT_F1) {
            desktop_view = VIEW_TERMINAL;
            terminal_open = 1U;
            terminal_minimized = 0U;
            terminal_focused = 1U;
            if (!terminal_started) {
                prompt();
                terminal_started = 1U;
            }
            draw_workspace();
            static const char terminal_marker[] = "QUANTA_VIEW_TERMINAL\n";
            call(QUANTA_SYSCALL_CONSOLE_WRITE, 1, terminal_marker, 0,
                sizeof(terminal_marker) - 1U);
            static const char terminal_focused_marker[] = "QUANTA_APP_TERMINAL_FOCUSED\n";
            call(QUANTA_SYSCALL_CONSOLE_WRITE, 1, terminal_focused_marker, 0,
                sizeof(terminal_focused_marker) - 1U);
            continue;
        }
        if (!terminal_focused) {
            desktop_dispatch(input);
            continue;
        }
        if (input == '\r' || input == '\n') {
            line[length] = 0;
            trim_line(line);
            write_text("\n");
            command(line);
            length = 0;
            line[0] = 0;
            prompt();
            draw_workspace();
        } else if ((input == 8 || input == 127) && length) {
            --length;
            write_text("\b \b");
            draw_rect(terminal_x + 20U + terminal_column * 8U,
                terminal_y + 52U + 17U * 19U, 8U, 16U, 0x0017202eU);
            draw_present();
        } else if (input >= 32 && input < 127 && length + 1 < BUFFER_SIZE) {
            line[length++] = (char)input;
            line[length] = 0;
            write_text(line + length - 1);
            uint32_t column = terminal_column == 0U ? 79U : terminal_column - 1U;
            char character[2] = { (char)input, 0 };
            draw_text(terminal_x + 20U + column * 8U,
                terminal_y + 52U + 17U * 19U, character, 0x00e8edf2U);
            draw_present();
        }
    }
}
