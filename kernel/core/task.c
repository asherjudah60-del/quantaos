#include <stdint.h>
#ifndef NULL
#define NULL ((void *)0)
#endif
#include <quanta/arch.h>
#include <quanta/account.h>
#include <quanta/memory.h>
#include <quanta/syscall.h>
#include <quanta/task.h>
#include <quanta/vfs.h>

#define TASK_MAX_CODE_PAGES 16U
#define USER_CODE_BASE 0x400000U
#define USER_STACK_BASE 0x7fffffffd000ULL
#define USER_STACK_END 0x7ffffffff000ULL
#define TASK_MAX_FILE_HANDLES 16U

extern const uint8_t __user_client_start[], __user_client_end[];
extern const uint8_t __user_server_start[], __user_server_end[];
extern const uint8_t __user_fault_start[], __user_fault_end[];

enum task_state { TASK_NEW, TASK_RUNNABLE, TASK_WAIT_REPLY, TASK_DONE };
struct quanta_task_capability {
    enum quanta_capability_kind kind;
    uint64_t resource;
    quanta_capability_handle handle;
    uint64_t rights;
};
struct quanta_task_file {
    uint32_t active;
    uint32_t flags;
    uint64_t offset;
    char path[QUANTA_PATH_MAX];
};
struct quanta_task {
    uint64_t root, code, stack, ip, sp, return_ip, return_flags;
    uint64_t code_pages[TASK_MAX_CODE_PAGES];
    uint32_t code_page_count;
    enum task_state state;
    quanta_capability_handle endpoint;
    uint32_t authenticated;
    struct quanta_task_capability capabilities[QUANTA_MAX_CAPABILITIES];
    uint32_t capability_count;
    struct quanta_task_file files[TASK_MAX_FILE_HANDLES];
};
static struct quanta_task tasks[QUANTA_MAX_TASKS];
static uint64_t current;

static int has_namespace_capability(const struct quanta_task *task,
    quanta_capability_handle handle) {
    uint32_t index;
    for (index = 0; index < task->capability_count; ++index) {
        const struct quanta_task_capability *capability = &task->capabilities[index];
        if (capability->handle == handle && capability->kind == QUANTA_CAP_NAMESPACE &&
            (capability->rights & QUANTA_CAP_RIGHT_SEND) != 0U) return 1;
    }
    return 0;
}

static int user_range_valid(const struct quanta_task *task, uint64_t address,
    uint64_t length, int writable) {
    uint64_t end;
    uint64_t code_end = USER_CODE_BASE + (uint64_t)task->code_page_count * 0x1000U;
    (void)writable;
    /* A zero-length transfer carries no data and may not reference a buffer. */
    if (length == 0U) return 1;
    if (address == 0U) return 0;
    if (address > ~0ULL - length) return 0;
    end = address + length;
    if (address >= USER_STACK_BASE && end <= USER_STACK_END) return 1;
    if (address >= USER_CODE_BASE && end <= code_end) return 1;
    return 0;
}

/* Validate a NUL-terminated user string that must fit inside QUANTA_PATH_MAX
   bytes without reading past the mapped range. */
static int user_path_valid(const struct quanta_task *task, uint64_t address) {
    uint64_t index;
    const char *text = (const char *)(uintptr_t)address;
    if (!user_range_valid(task, address, 1U, 0)) return 0;
    for (index = 0; index < QUANTA_PATH_MAX; ++index) {
        if (text[index] == 0) return 1;
        if (!user_range_valid(task, address + index + 1U, 1U, 0)) return 0;
    }
    return 0;
}

static struct quanta_task_file *file_for_handle(struct quanta_task *task,
    uint64_t handle) {
    if (handle == QUANTA_FILE_HANDLE_INVALID || handle > TASK_MAX_FILE_HANDLES) return NULL;
    if (!task->files[handle - 1U].active) return NULL;
    return &task->files[handle - 1U];
}

static int copy_path(const char *input, char *output) {
    uint32_t index;
    if (input == NULL || output == NULL) return -1;
    for (index = 0; index + 1U < QUANTA_PATH_MAX; ++index) {
        output[index] = input[index];
        if (input[index] == 0) return 0;
    }
    output[QUANTA_PATH_MAX - 1U] = 0;
    return -1;
}

static void copy_image(uint64_t page, const uint8_t *begin, const uint8_t *end) {
    volatile uint8_t *output = (volatile uint8_t *)(uintptr_t)page;
    while (begin != end) *output++ = *begin++;
}
static void create(uint64_t slot, const uint8_t *begin, const uint8_t *end, uint64_t cap) {
    struct quanta_task *task = &tasks[slot];
    uint64_t size = (uint64_t)(end - begin);
    uint32_t page;
    task->root = quanta_address_space_create();
    task->code_page_count = (uint32_t)((size + 0xfffU) / 0x1000U);
    if (task->code_page_count == 0U || task->code_page_count > TASK_MAX_CODE_PAGES) for (;;) __asm__ volatile("hlt");
    for (page = 0; page < task->code_page_count; ++page) {
        const uint8_t *page_begin = begin + (uint64_t)page * 0x1000U;
        uint64_t remaining = size - (uint64_t)page * 0x1000U;
        const uint8_t *page_end = page_begin + (remaining > 0x1000U ? 0x1000U : remaining);
        task->code_pages[page] = quanta_frame_allocate_or_panic();
        copy_image(task->code_pages[page], page_begin, page_end);
        quanta_map_page(task->root, 0x400000U + (uint64_t)page * 0x1000U,
            task->code_pages[page], QUANTA_PAGE_PRESENT | QUANTA_PAGE_WRITABLE | QUANTA_PAGE_USER);
    }
    task->code = task->code_pages[0]; task->stack = quanta_frame_allocate_or_panic();
    quanta_map_page(task->root, USER_STACK_BASE, task->stack,
        QUANTA_PAGE_PRESENT | QUANTA_PAGE_WRITABLE | QUANTA_PAGE_USER | QUANTA_PAGE_NO_EXECUTE);
    quanta_map_page(task->root, USER_STACK_BASE + 0x1000U,
        quanta_frame_allocate_or_panic(),
        QUANTA_PAGE_PRESENT | QUANTA_PAGE_WRITABLE | QUANTA_PAGE_USER | QUANTA_PAGE_NO_EXECUTE);
    quanta_map_page(task->root, 0xb8000, 0xb8000,
        QUANTA_PAGE_PRESENT | QUANTA_PAGE_WRITABLE | QUANTA_PAGE_USER);
    if (quanta_arch_storage_physical() != 0U) {
        uint64_t storage_pages = (quanta_arch_storage_sectors() + 7U) / 8U;
        for (page = 0; page < storage_pages; ++page) {
            uint64_t physical = quanta_arch_storage_physical() + (uint64_t)page * 0x1000U;
            quanta_map_page(task->root, physical, physical,
                QUANTA_PAGE_PRESENT | QUANTA_PAGE_USER | QUANTA_PAGE_NO_EXECUTE);
        }
    }
    task->ip = 0x400000; task->sp = USER_STACK_END - 8U; task->state = TASK_RUNNABLE; task->endpoint = cap;
    if (slot == 0U) {
        quanta_capability_handle namespace_handle;
        if (quanta_task_grant(task, QUANTA_CAP_NAMESPACE, 0, &namespace_handle) != QUANTA_STATUS_OK ||
            namespace_handle != 1U) for (;;) __asm__ volatile("hlt");
    }
}
static void run(uint64_t slot) __attribute__((noreturn));
static void run(uint64_t slot) {
    current = slot;
    quanta_arch_enter_user(tasks[slot].root, tasks[slot].ip, tasks[slot].sp, 0, 0);
}
void quanta_task_bootstrap(void) {
    create(0, __user_client_start, __user_client_end, 1);
    create(1, __user_server_start, __user_server_end, 2);
    create(2, __user_fault_start, __user_fault_end, 0);
    quanta_arch_configure_syscalls();
    quanta_arch_write_marker("QUANTA_USER_SESSION_STARTING\n");
    run(0);
}
long quanta_task_syscall(uint64_t number, uint64_t handle, uint64_t message,
    uint64_t payload, uint64_t length, uint64_t return_ip, uint64_t flags) {
    struct quanta_task *task = &tasks[current];
    const char *contents;
    const char *listing;
    const struct quanta_mount *mount;
    uint64_t index;
    long read_result;
    (void)message;
    (void)return_ip;
    (void)flags;
    if (number == QUANTA_SYSCALL_RTC_READ) quanta_arch_write_marker("QUANTA_RTC_ENTER\n");
    mount = quanta_vfs_boot_mount();
    if ((number == QUANTA_SYSCALL_IPC_CALL || number == QUANTA_SYSCALL_IPC_RECEIVE ||
        number == QUANTA_SYSCALL_IPC_REPLY) && length > QUANTA_IPC_MAX_PAYLOAD) {
        return QUANTA_STATUS_LIMIT;
    }
    if (length > QUANTA_SYSCALL_MAX_BUFFER) return QUANTA_STATUS_LIMIT;
    if (current == 0 && has_namespace_capability(task, handle) && number == QUANTA_SYSCALL_CONSOLE_WRITE) {
        if (length > QUANTA_SYSCALL_MAX_BUFFER) return QUANTA_STATUS_LIMIT;
        if (!user_range_valid(task, message, length, 0)) return QUANTA_STATUS_INVALID;
        quanta_arch_console_write((const char *)(uintptr_t)message, length);
        return QUANTA_STATUS_OK;
    }
    if (current == 0 && has_namespace_capability(task, handle) && number == QUANTA_SYSCALL_CONSOLE_READ) {
        return quanta_arch_console_read_char();
    }
    if (current == 0 && has_namespace_capability(task, handle) && number == QUANTA_SYSCALL_CONSOLE_CLEAR) {
        quanta_arch_console_clear();
        return QUANTA_STATUS_OK;
    }
    if (current == 0 && has_namespace_capability(task, handle) && number == QUANTA_SYSCALL_FS_READ) {
        if (payload == 0U || length == 0U || length > QUANTA_SYSCALL_MAX_BUFFER) return QUANTA_STATUS_INVALID;
        if (!user_path_valid(task, message) ||
            !user_range_valid(task, payload, length, 1)) return QUANTA_STATUS_INVALID;
        read_result = mount->provider->read((const char *)(uintptr_t)message,
            (char *)(uintptr_t)payload, (uint32_t)length);
        if (read_result >= 0) return read_result;
        if (quanta_ramfs_read((const char *)(uintptr_t)message, &contents) != 0) return QUANTA_STATUS_INVALID;
        for (index = 0; index + 1U < length && contents[index]; ++index) ((char *)(uintptr_t)payload)[index] = contents[index];
        ((char *)(uintptr_t)payload)[index] = 0;
        return (long)index;
    }
    if (current == 0 && has_namespace_capability(task, handle) && number == QUANTA_SYSCALL_FS_OPEN) {
        struct quanta_file_stat stat;
        uint32_t slot;
        uint64_t *result = (uint64_t *)(uintptr_t)payload;
        const char *path = (const char *)(uintptr_t)message;
        if (result == NULL ||
            !user_path_valid(task, message) ||
            !user_range_valid(task, payload, sizeof(*result), 1) ||
            (length & ~(QUANTA_OPEN_READ | QUANTA_OPEN_WRITE | QUANTA_OPEN_CREATE |
                QUANTA_OPEN_TRUNCATE | QUANTA_OPEN_DIRECTORY)) != 0U) {
            return QUANTA_STATUS_INVALID;
        }
        if (mount->provider->stat(path, &stat) != 0) {
            if ((length & QUANTA_OPEN_CREATE) == 0U ||
                mount->filesystem_kind != QUANTA_FILESYSTEM_QFS2 ||
                (mount->flags & QUANTA_MOUNT_READ_ONLY) != 0U ||
                (length & QUANTA_OPEN_DIRECTORY) != 0U ||
                quanta_qfs2_create(path) != 0 ||
                mount->provider->stat(path, &stat) != 0) {
                return QUANTA_STATUS_INVALID;
            }
        }
        if ((length & QUANTA_OPEN_DIRECTORY) != 0U && stat.type != QUANTA_FILE_DIRECTORY) {
            return QUANTA_STATUS_INVALID;
        }
        for (slot = 0; slot < TASK_MAX_FILE_HANDLES; ++slot) {
            if (!task->files[slot].active) break;
        }
        if (slot == TASK_MAX_FILE_HANDLES) return QUANTA_STATUS_LIMIT;
        if (copy_path((const char *)(uintptr_t)message, task->files[slot].path) != 0) {
            return QUANTA_STATUS_INVALID;
        }
        task->files[slot].active = 1U;
        task->files[slot].flags = (uint32_t)length;
        task->files[slot].offset = 0U;
        *result = slot + 1U;
        return QUANTA_STATUS_OK;
    }
    if (current == 0 && has_namespace_capability(task, handle) && number == QUANTA_SYSCALL_FS_CLOSE) {
        struct quanta_task_file *file = file_for_handle(task, message);
        if (file == NULL) return QUANTA_STATUS_INVALID;
        file->active = 0U;
        file->flags = 0U;
        file->offset = 0U;
        file->path[0] = 0;
        return QUANTA_STATUS_OK;
    }
    if (current == 0 && has_namespace_capability(task, handle) && number == QUANTA_SYSCALL_FS_READ_HANDLE) {
        struct quanta_task_file *file = file_for_handle(task, message);
        char contents[QUANTA_SYSCALL_MAX_BUFFER];
        long result;
        uint64_t available;
        if (file == NULL || payload == 0U || length == 0U ||
            length > QUANTA_SYSCALL_MAX_BUFFER ||
            !user_range_valid(task, payload, length, 1)) return QUANTA_STATUS_INVALID;
        result = mount->provider->read(file->path, contents, sizeof(contents));
        if (result < 0 || file->offset >= (uint64_t)result) return 0;
        available = (uint64_t)result - file->offset;
        if (available > length) available = length;
        for (index = 0; index < available; ++index) {
            ((char *)(uintptr_t)payload)[index] = contents[file->offset + index];
        }
        file->offset += available;
        return (long)available;
    }
    if (current == 0 && has_namespace_capability(task, handle) && number == QUANTA_SYSCALL_FS_LIST) {
        if (payload == 0U || length == 0U || length > QUANTA_SYSCALL_MAX_BUFFER) return QUANTA_STATUS_INVALID;
        if (!user_path_valid(task, message) ||
            !user_range_valid(task, payload, length, 1)) return QUANTA_STATUS_INVALID;
        read_result = mount->provider->list((const char *)(uintptr_t)message,
            (char *)(uintptr_t)payload, (uint32_t)length);
        if (read_result >= 0) return read_result;
        listing = quanta_ramfs_list((const char *)(uintptr_t)message);
        if (listing == 0) return QUANTA_STATUS_INVALID;
        for (index = 0; index + 1U < length && listing[index]; ++index) ((char *)(uintptr_t)payload)[index] = listing[index];
        ((char *)(uintptr_t)payload)[index] = 0;
        return (long)index;
    }
    if (current == 0 && has_namespace_capability(task, handle) && number == QUANTA_SYSCALL_FS_STAT) {
        struct quanta_file_stat *stat = (struct quanta_file_stat *)(uintptr_t)payload;
        if (stat == 0 || length < sizeof(*stat) ||
            !user_path_valid(task, message) ||
            !user_range_valid(task, payload, sizeof(*stat), 1)) return QUANTA_STATUS_INVALID;
        return mount->provider->stat((const char *)(uintptr_t)message, stat) == 0
            ? QUANTA_STATUS_OK : QUANTA_STATUS_INVALID;
    }
    if (current == 0 && has_namespace_capability(task, handle) && number == QUANTA_SYSCALL_FS_MKDIR) {
        if ((mount->flags & QUANTA_MOUNT_READ_ONLY) != 0U ||
            !user_path_valid(task, message)) return QUANTA_STATUS_DENIED;
        if (mount->filesystem_kind != QUANTA_FILESYSTEM_QFS2) return QUANTA_STATUS_STATE;
        if (quanta_qfs2_mkdir((const char *)(uintptr_t)message) != 0) {
            quanta_arch_write_marker("QUANTA_MKDIR_FAILED\n");
            return QUANTA_STATUS_STATE;
        }
        quanta_arch_write_marker("QUANTA_MKDIR_READY\n");
        return QUANTA_STATUS_OK;
    }
    if (current == 0 && has_namespace_capability(task, handle) && number == QUANTA_SYSCALL_SHUTDOWN) {
        quanta_arch_write_marker("QUANTA_SHUTDOWN\n");
        quanta_arch_shutdown();
    }
    if (current == 0 && has_namespace_capability(task, handle) && number == QUANTA_SYSCALL_RTC_READ) {
        uint8_t clock[6];
        uint8_t *output = (uint8_t *)(uintptr_t)payload;
        if (output == NULL || length < 6U) {
            quanta_arch_write_marker("QUANTA_RTC_INVALID_ARGS\n");
            return QUANTA_STATUS_INVALID;
        }
        if (!user_range_valid(task, payload, 6U, 1)) {
            quanta_arch_write_marker("QUANTA_RTC_INVALID_RANGE\n");
            return QUANTA_STATUS_INVALID;
        }
        quanta_arch_write_marker("QUANTA_RTC_READY\n");
        quanta_arch_read_rtc(&clock[0], &clock[1], &clock[2], &clock[3], &clock[4], &clock[5]);
        for (index = 0; index < 6U; ++index) output[index] = clock[index];
        quanta_arch_write_marker("QUANTA_RTC_DONE\n");
        return QUANTA_STATUS_OK;
    }
    if (current == 0 && has_namespace_capability(task, handle) && number == QUANTA_SYSCALL_AUTH_VERIFY) {
        uint32_t *temporary = (uint32_t *)(uintptr_t)payload;
        char password[65];
        uint32_t flag = 0;
        if (length > 64U || temporary == 0 ||
            (length != 0U && !user_range_valid(task, message, length, 0)) ||
            !user_range_valid(task, payload, sizeof(*temporary), 1)) return QUANTA_STATUS_INVALID;
        for (index = 0; index < length; ++index)
            password[index] = ((const char *)(uintptr_t)message)[index];
        password[length] = 0;
        if (quanta_account_verify(password, length, &flag) != 0) {
            return QUANTA_STATUS_DENIED;
        }
        *temporary = flag;
        task->authenticated = 1U;
        return QUANTA_STATUS_OK;
    }
    if (current == 0 && has_namespace_capability(task, handle) && number == QUANTA_SYSCALL_AUTH_CHANGE) {
        char password[65];
        if (!task->authenticated || length == 0U || length > 64U ||
            !user_range_valid(task, message, length, 0)) return QUANTA_STATUS_DENIED;
        for (index = 0; index < length; ++index)
            password[index] = ((const char *)(uintptr_t)message)[index];
        password[length] = 0;
        return quanta_account_change(password, length) == 0
            ? QUANTA_STATUS_OK : QUANTA_STATUS_STATE;
    }
    if (current == 0 && has_namespace_capability(task, handle) && number == QUANTA_SYSCALL_SYSTEM_INFO) {
        struct quanta_system_info *info = (struct quanta_system_info *)(uintptr_t)payload;
        mount = quanta_vfs_boot_mount();
        if (length < sizeof(*info) || !user_range_valid(task, payload, sizeof(*info), 1)) return QUANTA_STATUS_INVALID;
        info->mount_size_bytes = mount->size_bytes;
        info->mount_read_only = (mount->flags & QUANTA_MOUNT_READ_ONLY) != 0U;
        info->filesystem_kind = mount->filesystem_kind;
        return QUANTA_STATUS_OK;
    }
    if (current == 0 && has_namespace_capability(task, handle) && number == QUANTA_SYSCALL_SESSION_LOGOUT) {
        task->authenticated = 0U;
        quanta_arch_write_marker("QUANTA_SESSION_LOGOUT\n");
        return QUANTA_STATUS_OK;
    }
    if (number == QUANTA_SYSCALL_IPC_CALL) {
        if (current != 0 || handle != task->endpoint) return QUANTA_STATUS_DENIED;
        task->return_ip = return_ip; task->return_flags = flags; task->state = TASK_WAIT_REPLY;
        run(1);
    }
    if (number == QUANTA_SYSCALL_IPC_RECEIVE) {
        if (current != 1 || handle != task->endpoint || tasks[0].state != TASK_WAIT_REPLY) return QUANTA_STATUS_STATE;
        quanta_arch_write_marker("QUANTA_IPC_SERVER_RECEIVED\n"); return QUANTA_STATUS_OK;
    }
    if (number == QUANTA_SYSCALL_IPC_REPLY) {
        if (current != 1 || handle != task->endpoint || tasks[0].state != TASK_WAIT_REPLY) return QUANTA_STATUS_STATE;
        tasks[0].ip = tasks[0].return_ip; tasks[0].state = TASK_DONE;
        quanta_arch_write_marker("QUANTA_IPC_ROUNDTRIP_READY\n");
        quanta_arch_write_marker("QUANTA_FAULT_TASK_STARTED\n"); run(2);
    }
    if (number == QUANTA_SYSCALL_TASK_EXIT) {
        task->state = TASK_DONE;
        if (current == 0) { quanta_arch_write_marker("QUANTA_FAULT_TASK_STARTED\n"); run(2); }
        for (;;) __asm__ volatile("hlt");
    }
    return number == QUANTA_SYSCALL_YIELD ? QUANTA_STATUS_OK : QUANTA_STATUS_INVALID;
}
long quanta_arch_syscall_dispatch(uint64_t number, uint64_t handle, uint64_t message,
    uint64_t payload, uint64_t length, uint64_t return_ip, uint64_t flags) {
    return quanta_task_syscall(number, handle, message, payload, length, return_ip, flags);
}
void quanta_task_user_fault(uint64_t vector) {
    tasks[current].state = TASK_DONE;
    if (vector == 13U) quanta_arch_write_marker("QUANTA_USER_GP\n");
    if (vector == 14U) quanta_arch_write_marker("QUANTA_USER_PAGE_FAULT\n");
    quanta_arch_write_marker("QUANTA_USER_FAULT_TERMINATED\n");
    if (current == 2) { quanta_arch_write_marker("QUANTA_SERVICES_CONTINUE\n"); for (;;) __asm__ volatile("hlt"); }
    run(2);
}
int quanta_task_grant(struct quanta_task *task, enum quanta_capability_kind kind,
    uint64_t resource, quanta_capability_handle *handle) {
    uint32_t slot;
    if (task == NULL || handle == NULL) return QUANTA_STATUS_INVALID;
    if (kind == 0U || task->capability_count >= QUANTA_MAX_CAPABILITIES) return QUANTA_STATUS_LIMIT;
    slot = task->capability_count;
    task->capabilities[slot].kind = kind;
    task->capabilities[slot].resource = resource;
    task->capabilities[slot].rights = QUANTA_CAP_RIGHT_SEND | QUANTA_CAP_RIGHT_RECEIVE;
    task->capabilities[slot].handle = (quanta_capability_handle)(slot + 1U);
    if (task->capabilities[slot].handle == QUANTA_CAPABILITY_INVALID) {
        task->capabilities[slot].handle = 1U;
    }
    *handle = task->capabilities[slot].handle;
    if (kind == QUANTA_CAP_ENDPOINT) task->endpoint = *handle;
    task->capability_count += 1U;
    return QUANTA_STATUS_OK;
}
int quanta_task_revoke(struct quanta_task *task, quanta_capability_handle handle) {
    uint32_t index;
    if (task == NULL || handle == QUANTA_CAPABILITY_INVALID) return QUANTA_STATUS_INVALID;
    for (index = 0; index < task->capability_count; ++index) {
        if (task->capabilities[index].handle == handle) {
            uint32_t remaining = task->capability_count - index - 1U;
            if (remaining > 0U) {
                for (uint32_t copy = 0; copy < remaining; ++copy) {
                    task->capabilities[index + copy] = task->capabilities[index + copy + 1U];
                }
            }
            task->capabilities[task->capability_count - 1U].kind = 0;
            task->capabilities[task->capability_count - 1U].resource = 0U;
            task->capabilities[task->capability_count - 1U].handle = QUANTA_CAPABILITY_INVALID;
            task->capabilities[task->capability_count - 1U].rights = 0U;
            task->capability_count -= 1U;
            if (task->endpoint == handle) task->endpoint = QUANTA_CAPABILITY_INVALID;
            return QUANTA_STATUS_OK;
        }
    }
    return QUANTA_STATUS_INVALID;
}
