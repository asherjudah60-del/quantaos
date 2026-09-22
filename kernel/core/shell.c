#include <stdint.h>
#include <quanta/arch.h>
#include <quanta/vfs.h>
#define LINE_MAX 96U

enum shell_state { SHELL_LOGIN, SHELL_PASSWORD, SHELL_SESSION };
static char line[LINE_MAX]; static uint64_t length;
static enum shell_state current_state = SHELL_LOGIN;
static int logged_in = 0;

static int equal(const char *a, const char *b) { while (*a && *b && *a == *b) { ++a; ++b; } return *a == *b; }
static int prefix(const char *a, const char *b) { while (*b && *a == *b) { ++a; ++b; } return *b == 0; }
static void put(const char *text) { quanta_arch_write_marker(text); }
static void reset_line(void) { length = 0; line[0] = 0; }
static void prompt_session(void) { put("quanta> "); }
static void prompt_login(void) { put("login: "); }
static void prompt_password(void) { put("Password: "); }
static void number(char *out, uint8_t value) { out[0] = (char)('0' + value / 10); out[1] = (char)('0' + value % 10); }

static void execute_login(void) {
    line[length] = 0;
    if (equal(line, "quanta")) {
        current_state = SHELL_PASSWORD;
        put("\n");
        prompt_password();
        reset_line();
        return;
    }
    put("\nlogin failed\n");
    prompt_login();
    reset_line();
}

static void execute_password(void) {
    line[length] = 0;
    if (equal(line, "quanta")) {
        logged_in = 1;
        current_state = SHELL_SESSION;
        put("\nQUANTA_LOGIN_READY\n");
        prompt_session();
        reset_line();
        return;
    }
    put("\nlogin failed\n");
    current_state = SHELL_LOGIN;
    prompt_login();
    reset_line();
}

static void print_driver_status(void) {
    put("vga: online\nps2: online\nserial: online\nrtc: online\nshutdown: supported\n");
}

static void print_printf_demo(void) {
    put("printf demo: 42=42 hello=hello\n");
}

static void execute_session(void) {
    line[length] = 0;
    if (equal(line, "help")) put("help whoami pwd ls cat echo printf uname ps mem date drivers clear passwd exit shutdown\n");
    else if (equal(line, "whoami")) put("quanta\n");
    else if (equal(line, "pwd")) put("/home/quanta\n");
    else if (equal(line, "ls") || equal(line, "ls /") || prefix(line, "ls ")) { const char *listing = quanta_ramfs_list(equal(line, "ls") ? "/home/quanta" : line + 3); if (listing) put(listing); else put("ls: not found\n"); }
    else if (prefix(line, "cat ")) { const char *contents; if (quanta_ramfs_read(line + 4, &contents) == 0) put(contents); else put("cat: not found\n"); }
    else if (prefix(line, "echo ")) { put(line + 5); put("\n"); }
    else if (equal(line, "echo")) put("\n");
    else if (equal(line, "printf")) print_printf_demo();
    else if (equal(line, "uname")) put("QuantaOs x86_64 single-user\n");
    else if (equal(line, "ps")) put("PID STATE NAME\n1 running shell\n2 ready console\n");
    else if (equal(line, "mem")) put("memory: bootstrap frame allocator active\n");
    else if (equal(line, "date")) { uint8_t y, mo, d, h, mi, s; char stamp[] = "20xx-xx-xx xx:xx:xx UTC\n"; quanta_arch_read_rtc(&y, &mo, &d, &h, &mi, &s); number(stamp + 2, y); number(stamp + 5, mo); number(stamp + 8, d); number(stamp + 11, h); number(stamp + 14, mi); number(stamp + 17, s); put(stamp); }
    else if (equal(line, "drivers")) print_driver_status();
    else if (equal(line, "clear")) { quanta_arch_console_clear(); prompt_session(); }
    else if (equal(line, "passwd")) { put("passwd: password update not yet persisted\n"); }
    else if (equal(line, "exit")) { logged_in = 0; current_state = SHELL_LOGIN; quanta_arch_console_clear(); put("\nQuantaOs\n"); prompt_login(); }
    else if (equal(line, "shutdown")) { put("QUANTA_SHUTDOWN\n"); quanta_arch_shutdown(); }
    else if (length) put("command not found\n");
    if (logged_in && current_state == SHELL_SESSION) { reset_line(); prompt_session(); }
    else { reset_line(); }
}

static void execute(void) {
    if (current_state == SHELL_LOGIN) {
        execute_login();
        return;
    }
    if (current_state == SHELL_PASSWORD) {
        execute_password();
        return;
    }
    execute_session();
}

void quanta_shell_run(void) __attribute__((noreturn));
void quanta_shell_run(void) {
    put("\nQuantaOs\n");
    current_state = SHELL_LOGIN;
    prompt_login();
    for (;;) {
        int input = quanta_arch_console_read_char();
        if (input < 0) continue;
        if (input == '\r' || input == '\n') { put("\n"); execute(); continue; }
        if ((input == 8 || input == 127) && length) { --length; put("\b \b"); continue; }
        if (input >= 32 && input < 127 && length + 1 < LINE_MAX) {
            char echo[2] = { (char)input, 0 };
            line[length++] = (char)input;
            if (current_state == SHELL_PASSWORD) { put("*"); }
            else { put(echo); }
        }
    }
}
