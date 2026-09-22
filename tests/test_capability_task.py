#!/usr/bin/env python3
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

SOURCE = (
    '#include <stdint.h>\n'
    '#define assert(x) do { if (!(x)) __builtin_trap(); } while (0)\n\n'
    'const uint8_t __user_client_start[1] = {0};\n'
    'const uint8_t __user_client_end[1] = {0};\n'
    'const uint8_t __user_server_start[1] = {0};\n'
    'const uint8_t __user_server_end[1] = {0};\n'
    'const uint8_t __user_fault_start[1] = {0};\n'
    'const uint8_t __user_fault_end[1] = {0};\n\n'
    'uint64_t quanta_address_space_create(void) { return 1; }\n'
    'uint64_t quanta_frame_allocate_or_panic(void) { return 0x1000; }\n'
    'void quanta_map_page(uint64_t root, uint64_t virtual_address, uint64_t physical_page, uint64_t flags) {\n'
    '    (void)root; (void)virtual_address; (void)physical_page; (void)flags;\n'
    '}\n'
    'void quanta_arch_write_marker(const char *marker) { (void)marker; }\n'
    'void quanta_arch_console_write(const char *text, uint64_t length) { (void)text; (void)length; }\n'
    'int quanta_arch_console_read_char(void) { return -1; }\n'
    'void quanta_arch_console_clear(void) {}\n'
    'void quanta_arch_shutdown(void) { __builtin_trap(); }\n'
    'void quanta_arch_read_rtc(uint8_t *year, uint8_t *month, uint8_t *day, uint8_t *hour, uint8_t *minute, uint8_t *second) {\n'
    '    (void)year; (void)month; (void)day; (void)hour; (void)minute; (void)second;\n'
    '}\n'
    'int quanta_ramfs_read(const char *path, const char **contents) { (void)path; (void)contents; return -1; }\n'
    'const char *quanta_ramfs_list(const char *path) { (void)path; return 0; }\n'
    'int quanta_qfs_read(const char *path, char *output, uint32_t capacity) { (void)path; (void)output; (void)capacity; return -1; }\n'
    'int quanta_qfs_list(const char *path, char *output, uint32_t capacity) { (void)path; (void)output; (void)capacity; return -1; }\n'
    'void quanta_arch_configure_syscalls(void) {}\n'
    'void quanta_arch_enter_user(uint64_t root, uint64_t ip, uint64_t sp) {\n'
    '    (void)root; (void)ip; (void)sp;\n'
    '}\n\n'
    '#include "' + (ROOT / 'kernel/core/task.c').as_posix() + '"\n\n'
    'int main(void) {\n'
    '    struct quanta_task task = {0};\n'
    '    quanta_capability_handle handle = QUANTA_CAPABILITY_INVALID;\n\n'
    '    assert(quanta_task_grant(&task, QUANTA_CAP_ENDPOINT, 0x1234, &handle) == QUANTA_STATUS_OK);\n'
    '    assert(handle != QUANTA_CAPABILITY_INVALID);\n'
    '    assert(task.capabilities[0].kind == QUANTA_CAP_ENDPOINT);\n'
    '    assert(task.capabilities[0].resource == 0x1234);\n'
    '    assert(quanta_task_revoke(&task, handle) == QUANTA_STATUS_OK);\n'
    '    assert(task.capabilities[0].kind == 0);\n'
    '    return 0;\n'
    '}\n'
)


def main() -> int:
    with tempfile.TemporaryDirectory() as tmpdir:
        src = pathlib.Path(tmpdir) / 'capability_task_test.c'
        exe = pathlib.Path(tmpdir) / 'capability_task_test'
        src.write_text(SOURCE)
        compile_cmd = [
            'clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
            '-I' + str(ROOT / 'abi/include'),
            '-I' + str(ROOT / 'kernel/core/include'),
            '-I' + str(ROOT / 'kernel/mm/include'),
            '-I' + str(ROOT / 'kernel/vfs/include'),
            '-I' + str(ROOT / 'kernel/arch/x86_64/include'),
            str(src), '-o', str(exe),
        ]
        try:
            subprocess.run(compile_cmd, check=True, cwd=str(ROOT), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            subprocess.run([str(exe)], check=True, cwd=str(ROOT), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        except subprocess.CalledProcessError as exc:
            sys.stderr.write(exc.stdout or exc.stderr or str(exc))
            return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
