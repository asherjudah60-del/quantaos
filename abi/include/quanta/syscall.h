#ifndef QUANTA_SYSCALL_H
#define QUANTA_SYSCALL_H

#include <stdint.h>

/* RAX is the operation; RDI, RSI, RDX and R10 are its arguments. */
enum quanta_syscall_number {
    QUANTA_SYSCALL_YIELD = 0,
    QUANTA_SYSCALL_IPC_CALL = 1,
    QUANTA_SYSCALL_IPC_RECEIVE = 2,
    QUANTA_SYSCALL_IPC_REPLY = 3,
    QUANTA_SYSCALL_TASK_EXIT = 4,
    QUANTA_SYSCALL_CONSOLE_WRITE = 5,
    QUANTA_SYSCALL_CONSOLE_READ = 6,
    QUANTA_SYSCALL_FS_LIST = 7,
    QUANTA_SYSCALL_FS_READ = 8,
    QUANTA_SYSCALL_SYSTEM_INFO = 9,
    QUANTA_SYSCALL_CONSOLE_CLEAR = 10,
    QUANTA_SYSCALL_SHUTDOWN = 11,
    QUANTA_SYSCALL_RTC_READ = 12,
    QUANTA_SYSCALL_SESSION_LOGOUT = 13,
    QUANTA_SYSCALL_AUTH_VERIFY = 14,
    QUANTA_SYSCALL_FS_OPEN = 15,
    QUANTA_SYSCALL_FS_CLOSE = 16,
    QUANTA_SYSCALL_FS_READ_HANDLE = 17,
    QUANTA_SYSCALL_FS_WRITE_HANDLE = 18,
    QUANTA_SYSCALL_FS_SEEK = 19,
    QUANTA_SYSCALL_FS_STAT = 20,
    QUANTA_SYSCALL_FS_READDIR = 21,
    QUANTA_SYSCALL_FS_MKDIR = 22,
    QUANTA_SYSCALL_FS_UNLINK = 23,
    QUANTA_SYSCALL_FS_RMDIR = 24,
    QUANTA_SYSCALL_FS_CHMOD = 25,
    QUANTA_SYSCALL_FS_SYNC = 26,
    QUANTA_SYSCALL_AUTH_CHANGE = 27,
};

enum quanta_status {
    QUANTA_STATUS_OK = 0,
    QUANTA_STATUS_INVALID = -1,
    QUANTA_STATUS_DENIED = -2,
    QUANTA_STATUS_LIMIT = -3,
    QUANTA_STATUS_STATE = -4,
};

#define QUANTA_IPC_MAX_PAYLOAD 64U
#define QUANTA_SYSCALL_MAX_BUFFER 256U
#define QUANTA_ACCOUNT_LBA 256U
#define QUANTA_ACCOUNT_SLOT_COUNT 2U
#define QUANTA_ACCOUNT_RECORD_SIZE 512U
#define QUANTA_FILE_HANDLE_INVALID 0U
#define QUANTA_PATH_MAX 96U
#define QUANTA_NAME_MAX 48U

enum quanta_open_flags {
    QUANTA_OPEN_READ = 1U << 0,
    QUANTA_OPEN_WRITE = 1U << 1,
    QUANTA_OPEN_CREATE = 1U << 2,
    QUANTA_OPEN_TRUNCATE = 1U << 3,
    QUANTA_OPEN_DIRECTORY = 1U << 4,
};

enum quanta_file_type {
    QUANTA_FILE_REGULAR = 1,
    QUANTA_FILE_DIRECTORY = 2,
    QUANTA_FILE_DEVICE = 3,
};

struct quanta_file_stat {
    uint64_t inode;
    uint64_t size;
    uint64_t blocks;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t type;
    uint64_t generation;
} __attribute__((packed));

struct quanta_dir_entry {
    uint64_t inode;
    uint32_t type;
    uint16_t name_length;
    char name[QUANTA_NAME_MAX];
} __attribute__((packed));
struct quanta_system_info {
    uint64_t mount_size_bytes;
    uint32_t mount_read_only;
    uint32_t filesystem_kind;
} __attribute__((packed));
#define QUANTA_FILESYSTEM_UNKNOWN 0U
#define QUANTA_FILESYSTEM_QFS1 1U
#define QUANTA_FILESYSTEM_QFS2 2U
#define QUANTA_FILESYSTEM_FAT 3U
#define QUANTA_FILESYSTEM_VFAT 4U
#define QUANTA_MAX_TASKS 3U
#define QUANTA_MAX_CAPABILITIES 8U
#define QUANTA_MAX_ENDPOINTS 1U

#endif
