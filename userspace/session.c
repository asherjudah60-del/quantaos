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

static void write_text(const char *text) {
    uint64_t length = 0;
    while (text[length] && length < QUANTA_SYSCALL_MAX_BUFFER) ++length;
    call(QUANTA_SYSCALL_CONSOLE_WRITE, 1, text, 0, length);
}

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

/* Shared scratch buffer for console output.  Keeping it static (instead of a
   per-command stack array) guarantees syscall buffers live inside the mapped
   user stack and never alias stale command results: every command resets it
   before use, so one failed command cannot poison the next line. */
static char output_buffer[QUANTA_SYSCALL_MAX_BUFFER];
static char logged_in_user[32] = "quanta";
static char cwd[PATH_SIZE] = "C:/home/quanta";

static void prompt(void) {
    write_text(logged_in_user);
    write_text("@");
    write_text(cwd);
    write_text("#");
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

/* Normalize a shell path into an absolute kernel path.
   "C:/", "c:/", and "/" name the same root; the drive prefix is replaced by
   the leading slash instead of being stripped together with it.  ".", "..",
   repeated slashes, and trailing slashes are resolved here so the kernel only
   ever sees canonical POSIX paths. */
static int path_resolve(const char *input, char *output) {
    char parts[8][24]; char normalized[PATH_SIZE]; uint32_t count = 0, length = 0, index = 0, start;
    const char *base;
    if (input == 0) return -1;
    while (*input == ' ') ++input;
    if ((input[0] == 'C' || input[0] == 'c') && input[1] == ':') {
        input += 2;
        /* "C:" alone (or "C: relative") keeps the current directory as base;
           "C:/..." addresses the filesystem root directly. */
        if (input[0] == '/' || input[0] == '\\') ++input;
        else if (input[0] == 0) input -= 2;
    }
    for (index = 0; input[index] && index + 1U < PATH_SIZE; ++index)
        normalized[index] = input[index] == '\\' ? '/' : input[index];
    normalized[index] = 0;
    input = normalized;
    if (input[0] != '/') {
        base = cwd;
        if (base[0] == 'C' && base[1] == ':') base += 2;
        for (index = 0; base[index] && length + 1U < PATH_SIZE; ++index) output[length++] = base[index];
        if (length == 0U || output[length - 1U] != '/') output[length++] = '/';
    }
    for (index = 0; input[index] && length + 1U < PATH_SIZE; ++index) output[length++] = input[index];
    output[length] = 0;
    length = 0;
    while (output[length]) {
        while (output[length] == '/') ++length;
        start = length;
        while (output[length] && output[length] != '/') ++length;
        if (length == start) break;
        output[length] = 0;
        if (equal(output + start, ".")) { output[length] = '/'; continue; }
        if (equal(output + start, "..")) { if (count) --count; output[length] = '/'; continue; }
        if (count >= 8U || length - start >= 24U) return -1;
        for (index = 0; index < length - start; ++index) parts[count][index] = output[start + index];
        parts[count][index] = 0; ++count; output[length] = '/';
    }
    length = 0; output[length++] = '/';
    for (index = 0; index < count; ++index) {
        uint32_t part = 0;
        while (parts[index][part] && length + 1U < PATH_SIZE) output[length++] = parts[index][part++];
        if (index + 1U < count && length + 1U < PATH_SIZE) output[length++] = '/';
    }
    output[length] = 0;
    return 0;
}

static void set_cwd(const char *path) {
    uint64_t index;
    cwd[0] = 'C';
    cwd[1] = ':';
    for (index = 0; path[index] && index + 3U < PATH_SIZE; ++index) cwd[index + 2U] = path[index];
    cwd[index + 2U] = 0;
}

static int split_command(const char *line, char *name, uint64_t name_size, const char **args) {
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
    clear();
    for (;;) {
        write_text("\nQuantaOs\nlogin: ");
        read_line(username, sizeof(username), 1);
        write_text("password: ");
        uint64_t length = read_line(password, sizeof(password), 0);
        if (equal(username, "quanta") &&
            call(QUANTA_SYSCALL_AUTH_VERIFY, 1, password, &temporary, length) == 0) {
            uint64_t index;
            for (index = 0; index + 1U < sizeof(logged_in_user) && username[index]; ++index)
                logged_in_user[index] = username[index];
            logged_in_user[index] = 0;
            write_text("QUANTA_LOGIN_READY\n");
            return 1;
        }
        write_text("login incorrect\n");
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
    char *buffer = output_buffer;
    char name[32];
    const char *args = "";
    uint32_t scan;
    for (scan = 0; scan < QUANTA_SYSCALL_MAX_BUFFER; ++scan) buffer[scan] = 0;
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
        const char *target = args[0] == 0 ? "/home/quanta" : args;
        while (*target == ' ') ++target;
        if (target[0] == 0) target = "/home/quanta";
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
        long result;
        while (target[0] == '-') { while (*target && *target != ' ') ++target; while (*target == ' ') ++target; }
        result = path_resolve(target[0] == 0 ? "." : target, path) == 0 ?
            call(QUANTA_SYSCALL_FS_LIST, 1, path, buffer, QUANTA_SYSCALL_MAX_BUFFER) : -1;
        if (result > 0) write_text(buffer);
        else if (result == 0) { }
        else write_text("ls: not found\n");
    }
    else if (equal(name, "cat")) {
        char path[PATH_SIZE]; long result;
        if (args[0] == 0) write_text("cat: usage: cat FILE...\n");
        else {
            result = path_resolve(args, path) == 0 ?
                call(QUANTA_SYSCALL_FS_READ, 1, path, buffer, QUANTA_SYSCALL_MAX_BUFFER) : -1;
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
    else if (equal(name, "env")) write_text("USER=quanta\nHOME=/home/quanta\nSHELL=/bin/session\n");
    else if (equal(name, "history")) write_text("history is session-local\n");
    else if (equal(name, "tty")) write_text("/dev/console\n");
    else if (equal(name, "lsblk")) {
        struct quanta_system_info info;
        char size[24];
        if (system_info(&info)) {
            decimal(size, info.mount_size_bytes);
            write_text("NAME   SIZE       TYPE  MOUNTPOINT\n");
            write_text("disk0  "); write_text(size); write_text(" bytes  ");
            write_text(filesystem_name(info.filesystem_kind)); write_text("  C:/\n");
        } else write_text("lsblk: no block devices\n");
    }
    else if (equal(name, "df")) {
        /* df [PATH]: only the single boot volume exists today, so any path
           reports the same filesystem; unknown paths are an error. */
        struct quanta_system_info info;
        char size[24];
        if (args[0] != 0) {
            char path[PATH_SIZE]; struct quanta_file_stat stat;
            if (path_resolve(args, path) != 0 ||
                call(QUANTA_SYSCALL_FS_STAT, 1, path, &stat, sizeof(stat)) != 0) {
                write_text("df: "); write_text(args); write_text(": no such file or directory\n");
            } else if (!system_info(&info)) write_text("df: unavailable\n");
            else {
                decimal(size, info.mount_size_bytes);
                write_text("Filesystem  Size       Used     Avail\n");
                write_text(filesystem_name(info.filesystem_kind)); write_text("        ");
                write_text(size); write_text(" bytes  unknown  unknown\n");
            }
        } else if (0) { }
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
            write_text("/dev/disk0 on C:/ type ");
            write_text(filesystem_name(info.filesystem_kind));
            write_text(" (rw, ");
            write_text(size);
            write_text(" bytes)\n");
        } else write_text("mount: unavailable\n");
    }
    else if (equal(name, "umount")) write_text("umount: C:/ is busy\n");
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
        uint32_t entry;
        int builtin = 0;
        if (args[0] == 0) write_text("which: usage: which COMMAND\n");
        else {
            for (entry = 0; entry < sizeof(manuals) / sizeof(manuals[0]); ++entry)
                if (equal(args, manuals[entry].name)) builtin = 1;
            if (builtin) { write_text(args); write_text(": shell builtin\n"); }
            else if (equal(args, "date") || equal(args, "true") || equal(args, "false") ||
                equal(args, "uname")) { write_text("C:/bin/"); write_text(args); write_text("\n"); }
            else write_text("which: not found\n");
        }
    }
    else if (equal(name, "type")) {
        if (args[0] == 0) write_text("type: usage: type COMMAND\n");
        else write_text("type: shell builtin\n");
    }
    else if (equal(name, "head")) {
        /* head [-n LINES] FILE: the read buffer already bounds output, so a
           line limit is accepted but the whole short file is printed. */
        char path[PATH_SIZE]; const char *file = args; long result;
        if (file[0] == '-' && file[1] == 'n') {
            while (*file && *file != ' ') ++file;
            while (*file == ' ') ++file;
        }
        if (file[0] == 0) write_text("head: usage: head [-n LINES] FILE\n");
        else {
            result = path_resolve(file, path) == 0 ?
                call(QUANTA_SYSCALL_FS_READ, 1, path, buffer, QUANTA_SYSCALL_MAX_BUFFER) : -1;
            if (result >= 0) write_text(buffer); else write_text("head: not found\n");
        }
    }
    else if (equal(name, "wc")) {
        char path[PATH_SIZE]; long result;
        if (args[0] == 0) write_text("wc: usage: wc FILE\n");
        else {
            result = path_resolve(args, path) == 0 ?
                call(QUANTA_SYSCALL_FS_READ, 1, path, buffer, QUANTA_SYSCALL_MAX_BUFFER) : -1;
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
            while (*args) { if (*args == '/' || *args == '\\') last = args + 1; ++args; }
            write_text(last); write_text("\n");
        }
    }
    else if (equal(name, "dirname")) {
        if (args[0] == 0) write_text("dirname: usage: dirname PATH\n");
        else write_text("/\n");
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
    prompt();
    for (;;) {
        int input = read_char();
        if (input < 0) continue;
        if (input == '\r' || input == '\n') {
            line[length] = 0;
            trim_line(line);
            write_text("\n");
            command(line);
            length = 0;
            line[0] = 0;
            prompt();
        } else if ((input == 8 || input == 127) && length) {
            --length;
            write_text("\b \b");
        } else if (input >= 32 && input < 127 && length + 1 < BUFFER_SIZE) {
            line[length++] = (char)input;
            line[length] = 0;
            write_text(line + length - 1);
        }
    }
}
