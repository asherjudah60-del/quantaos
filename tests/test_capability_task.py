#!/usr/bin/env python3
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

SOURCE = (
    '#include <stdint.h>\n'
    '#include <setjmp.h>\n'
    '#include <quanta/ipc.h>\n'
    '#define assert(x) do { if (!(x)) __builtin_trap(); } while (0)\n\n'
    'static jmp_buf task_jump;\n'
    'static struct quanta_ipc_message request_message;\n'
    'static struct quanta_ipc_message received_message;\n'
    'static struct quanta_ipc_message reply_message;\n'
    'static struct quanta_ipc_message invalid_message;\n'
    'static char request_payload[4] = {\'p\', \'i\', \'n\', \'g\'};\n'
    'static char received_payload[QUANTA_IPC_MAX_PAYLOAD];\n'
    'static char reply_payload[4] = {\'p\', \'o\', \'n\', \'g\'};\n'
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
    'void quanta_arch_enter_user(uint64_t root, uint64_t ip, uint64_t sp, uint64_t argc, uint64_t argv) {\n'
    '    (void)root; (void)ip; (void)sp; (void)argc; (void)argv; longjmp(task_jump, 1);\n'
    '}\n\n'
    '#include "' + (ROOT / 'kernel/core/task.c').as_posix() + '"\n\n'
    'uint64_t quanta_arch_timer_ticks(void) { return 1; }\n'
    'uint64_t quanta_arch_storage_physical(void) { return 0; }\n'
    'uint64_t quanta_arch_storage_sectors(void) { return 0; }\n'
    'int quanta_arch_desktop_draw(const void *commands, uint32_t count) { (void)commands; (void)count; return 0; }\n'
    'int quanta_arch_desktop_view(uint32_t view) { (void)view; return 0; }\n'
    'int quanta_arch_desktop_present(void) { return 0; }\n'
    'const struct quanta_mount *quanta_vfs_boot_mount(void) { return 0; }\n'
    'uint32_t quanta_storage_device_count(void) { return 0; }\n'
    'const struct quanta_block_device *quanta_storage_device_at(uint32_t index) { (void)index; return 0; }\n'
    'const struct quanta_block_device *quanta_storage_boot_device(void) { return 0; }\n'
    'int quanta_qfs2_create(const char *path) { (void)path; return -1; }\n'
    'int quanta_qfs2_mkdir(const char *path) { (void)path; return -1; }\n'
    'int quanta_account_verify(const char *password, uint64_t length, uint32_t *temporary) { (void)password; (void)length; (void)temporary; return -1; }\n'
    'int quanta_account_change(const char *password, uint64_t length) { (void)password; (void)length; return -1; }\n'
    'int main(void) {\n'
    '    struct quanta_task task = {0};\n'
    '    quanta_capability_handle handle = QUANTA_CAPABILITY_INVALID;\n\n'
    '    assert(quanta_task_grant(&task, QUANTA_CAP_ENDPOINT, 0x1234, &handle) == QUANTA_STATUS_OK);\n'
    '    assert(handle != QUANTA_CAPABILITY_INVALID);\n'
    '    assert(task.capabilities[0].kind == QUANTA_CAP_ENDPOINT);\n'
    '    assert(task.capabilities[0].resource == 0x1234);\n'
    '    assert(quanta_task_revoke(&task, handle) == QUANTA_STATUS_OK);\n'
    '    assert(task.capabilities[0].kind == 0);\n'

    '    tasks[0].code_page_count = 64;\n'
    '    tasks[1].code_page_count = 64;\n'
    '    assert(quanta_task_grant(&tasks[0], QUANTA_CAP_ENDPOINT, 0x1234, &handle) == QUANTA_STATUS_OK);\n'
    '    tasks[0].endpoint = handle;\n'
    '    tasks[0].state = TASK_RUNNABLE;\n'
    '    request_message.version = QUANTA_IPC_VERSION;\n'
    '    request_message.service = 3;\n'
    '    request_message.operation = 7;\n'
    '    request_message.correlation_id = 0x55;\n'
    '    request_message.payload_length = 4;\n'
    '    request_message.capability_count = 0;\n'
    '    request_message.flags = 0;\n'
    '    if (setjmp(task_jump) == 0) {\n'
    '        (void)quanta_task_syscall(QUANTA_SYSCALL_IPC_CALL, handle, (uint64_t)(uintptr_t)&request_message, (uint64_t)(uintptr_t)request_payload, 4, 0x1234, 0x202);\n'
    '        __builtin_trap();\n'
    '    }\n'
    '    assert(current == 1 && tasks[0].state == TASK_WAIT_REPLY);\n'
    '    assert(ipc_request_pending == 1 && ipc_request_length == 4);\n'
    '    assert(ipc_request_payload[0] == \'p\' && ipc_request_payload[3] == \'g\');\n'
    '    assert(quanta_task_grant(&tasks[1], QUANTA_CAP_ENDPOINT, 0x1234, &handle) == QUANTA_STATUS_OK);\n'
    '    tasks[1].endpoint = handle;\n'
    '    assert(quanta_task_syscall(QUANTA_SYSCALL_IPC_RECEIVE, handle, (uint64_t)(uintptr_t)&received_message, (uint64_t)(uintptr_t)received_payload, QUANTA_IPC_MAX_PAYLOAD, 0, 0) == QUANTA_STATUS_OK);\n'
    '    assert(received_message.service == 3 && received_message.operation == 7);\n'
    '    assert(received_payload[0] == \'p\' && received_payload[3] == \'g\');\n'
    '    reply_message.version = QUANTA_IPC_VERSION;\n'
    '    reply_message.service = 3;\n'
    '    reply_message.operation = 8;\n'
    '    reply_message.correlation_id = 0x55;\n'
    '    reply_message.payload_length = 4;\n'
    '    reply_message.capability_count = 0;\n'
    '    reply_message.flags = 0;\n'
    '    if (setjmp(task_jump) == 0) {\n'
    '        (void)quanta_task_syscall(QUANTA_SYSCALL_IPC_REPLY, handle, (uint64_t)(uintptr_t)&reply_message, (uint64_t)(uintptr_t)reply_payload, 4, 0, 0);\n'
    '        __builtin_trap();\n'
    '    }\n'
    '    assert(ipc_request_pending == 0 && ipc_reply_length == 4);\n'
    '    assert(ipc_reply.operation == 8 && ipc_reply.correlation_id == 0x55);\n'
    '    assert(ipc_reply_payload[0] == \'p\' && ipc_reply_payload[3] == \'g\');\n'
    '    invalid_message = request_message;\n'
    '    invalid_message.version = 0;\n'
    '    current = 0;\n'
    '    assert(quanta_task_syscall(QUANTA_SYSCALL_IPC_CALL, tasks[0].endpoint, (uint64_t)(uintptr_t)&invalid_message, (uint64_t)(uintptr_t)request_payload, 4, 0x1234, 0x202) == QUANTA_STATUS_INVALID);\n'
    '    return 0;\n'
    '}\n'
)


def main() -> int:
    with tempfile.TemporaryDirectory() as tmpdir:
        src = pathlib.Path(tmpdir) / 'capability_task_test.c'
        exe = pathlib.Path(tmpdir) / 'capability_task_test'
        src.write_text(SOURCE)
        compile_cmd = [
            'clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-no-pie',
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
