#include <quanta/vfs.h>

enum {
    FILESYSTEM_UNKNOWN,
    FILESYSTEM_QFS1,
    FILESYSTEM_QFS2,
    FILESYSTEM_FAT,
    FILESYSTEM_VFAT
};

static uint16_t little16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static struct quanta_mount boot_mount = {
    "/", 0, 0, 0, FILESYSTEM_UNKNOWN, QUANTA_MOUNT_BOOT
};

uint32_t quanta_vfs_probe_boot(void) {
    uint8_t sector[512];
    uint32_t kind = FILESYSTEM_UNKNOWN;
    uint32_t qfs_read;
    for (qfs_read = 0; qfs_read < sizeof(sector); ++qfs_read) sector[qfs_read] = 0;
    if (quanta_block_device_read(quanta_storage_boot_device(), 258, sector) == 0) {
        if (sector[0] == 'Q' && sector[1] == 'F' && sector[2] == 'S' &&
            sector[3] == 'v' && sector[4] == '1') kind = FILESYSTEM_QFS1;
        else if (sector[0] == 'Q' && sector[1] == 'F' && sector[2] == 'S' &&
            sector[3] == 'v' && sector[4] == '2') kind = FILESYSTEM_QFS2;
    }
    if (kind == FILESYSTEM_UNKNOWN &&
        quanta_block_device_read(quanta_storage_boot_device(), 0, sector) == 0 &&
        sector[510] == 0x55 && sector[511] == 0xaa) {
        uint16_t bytes_per_sector = little16(sector + 11);
        uint16_t reserved = little16(sector + 14);
        uint8_t fats = sector[16];
        uint16_t root_entries = little16(sector + 17);
        uint16_t sectors_per_fat = little16(sector + 22);
        if (bytes_per_sector == 512U && reserved != 0U && fats != 0U &&
            root_entries != 0U && sectors_per_fat != 0U) kind = FILESYSTEM_FAT;
        if (sector[54] == 'F' && sector[55] == 'A' && sector[56] == 'T') kind = FILESYSTEM_FAT;
        if (sector[82] == 'F' && sector[83] == 'A' && sector[84] == 'T') kind = FILESYSTEM_VFAT;
    }
    return kind;
}

const struct quanta_mount *quanta_vfs_boot_mount(void) {
    if (boot_mount.device == 0) {
        boot_mount.device = quanta_storage_boot_device();
        boot_mount.filesystem_kind = quanta_vfs_probe_boot();
        if (boot_mount.filesystem_kind == FILESYSTEM_QFS1) {
            boot_mount.provider = quanta_qfs_v1_provider();
            boot_mount.size_bytes = quanta_qfs_v1_size_bytes();
            boot_mount.flags |= QUANTA_MOUNT_READ_ONLY;
        } else if (boot_mount.filesystem_kind == FILESYSTEM_QFS2) {
            boot_mount.provider = quanta_qfs_v2_provider();
            boot_mount.size_bytes = quanta_qfs_v2_size_bytes();
        }
    }
    return &boot_mount;
}
