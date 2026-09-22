#ifndef QUANTA_KERNEL_VFS_H
#define QUANTA_KERNEL_VFS_H

#include <quanta/object.h>
#include <quanta/syscall.h>

typedef int (*quanta_block_read_fn)(uint32_t sector, void *buffer);
typedef int (*quanta_block_write_fn)(uint32_t sector, const void *buffer);

struct quanta_block_device {
    const char *name;
    uint32_t sector_size;
    uint64_t sector_count;
    quanta_block_read_fn read;
    quanta_block_write_fn write;
};

typedef int (*quanta_fs_read_fn)(const char *path, char *output, uint32_t capacity);
typedef int (*quanta_fs_list_fn)(const char *path, char *output, uint32_t capacity);
typedef int (*quanta_fs_stat_fn)(const char *path, struct quanta_file_stat *stat);

struct quanta_fs_provider {
    const char *name;
    quanta_fs_read_fn read;
    quanta_fs_list_fn list;
    quanta_fs_stat_fn stat;
};

struct quanta_mount {
    const char *path;
    const struct quanta_block_device *device;
    const struct quanta_fs_provider *provider;
    uint64_t size_bytes;
    uint32_t filesystem_kind;
    uint32_t flags;
};

#define QUANTA_MOUNT_READ_ONLY (1U << 0)
#define QUANTA_MOUNT_BOOT (1U << 1)

const struct quanta_block_device *quanta_storage_boot_device(void);
int quanta_block_device_read(const struct quanta_block_device *device,
    uint64_t sector, void *buffer);
int quanta_block_device_write(const struct quanta_block_device *device,
    uint64_t sector, const void *buffer);
const struct quanta_fs_provider *quanta_qfs_v1_provider(void);
uint64_t quanta_qfs_v1_size_bytes(void);
const struct quanta_fs_provider *quanta_qfs_v2_provider(void);
uint64_t quanta_qfs_v2_size_bytes(void);
uint32_t quanta_vfs_probe_boot(void);
const struct quanta_mount *quanta_vfs_boot_mount(void);
int quanta_namespace_resolve(quanta_object_id parent, const char *name,
    quanta_object_id *object);
struct quanta_ramfs_entry {
    const char *path;
    const char *contents;
};
int quanta_ramfs_read(const char *path, const char **contents);
const char *quanta_ramfs_list(const char *path);
int quanta_qfs_read(const char *path, char *output, uint32_t capacity);
int quanta_qfs_list(const char *path, char *output, uint32_t capacity);
int quanta_qfs_stat(const char *path, struct quanta_file_stat *stat);
int quanta_qfs2_stat(const char *path, struct quanta_file_stat *stat);
int quanta_qfs2_mkdir(const char *path);
#endif
