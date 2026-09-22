#include <stddef.h>
#include <quanta/boot_info.h>
#include <quanta/ipc.h>
#include <quanta/syscall.h>

_Static_assert(sizeof(struct quanta_boot_memory_region) == 24,
    "boot memory region ABI changed");
_Static_assert(offsetof(struct quanta_boot_info, magic) == 0,
    "boot-info magic offset changed");
_Static_assert(offsetof(struct quanta_boot_info, memory_map_physical) == 16,
    "boot-info memory-map offset changed");
_Static_assert(sizeof(struct quanta_ipc_message) == 56,
    "IPC envelope ABI changed");
_Static_assert(offsetof(struct quanta_ipc_message, capabilities) == 24,
    "IPC capability offset changed");
_Static_assert(QUANTA_IPC_MAX_PAYLOAD == 64, "IPC payload ABI changed");
_Static_assert(QUANTA_SYSCALL_TASK_EXIT == 4, "syscall ABI changed");
_Static_assert(QUANTA_SYSCALL_FS_OPEN == 15, "file syscall ABI changed");
_Static_assert(sizeof(struct quanta_file_stat) == 48, "file stat ABI changed");
_Static_assert(sizeof(struct quanta_dir_entry) == 62, "directory entry ABI changed");
_Static_assert(QUANTA_PATH_MAX == 96, "path ABI changed");

int main(void) { return 0; }
