#include <quanta/vfs.h>

#define STORAGE_SECTOR_SIZE 512U

extern int quanta_arch_disk_read(uint32_t sector, void *buffer);
extern int quanta_arch_disk_write(uint32_t sector, const void *buffer);

static int boot_read(uint32_t sector, void *buffer) {
    return quanta_arch_disk_read(sector, buffer);
}

static int boot_write(uint32_t sector, const void *buffer) {
    return quanta_arch_disk_write(sector, buffer);
}

static const struct quanta_block_device boot_device = {
    "disk0", STORAGE_SECTOR_SIZE, 0, boot_read, boot_write
};

const struct quanta_block_device *quanta_storage_boot_device(void) {
    return &boot_device;
}

int quanta_block_device_read(const struct quanta_block_device *device,
    uint64_t sector, void *buffer) {
    if (device == 0 || device->read == 0 || buffer == 0 || sector > 0xffffffffU) return -1;
    if (device->sector_count != 0 && sector >= device->sector_count) return -1;
    return device->read((uint32_t)sector, buffer);
}

int quanta_block_device_write(const struct quanta_block_device *device,
    uint64_t sector, const void *buffer) {
    if (device == 0 || device->write == 0 || buffer == 0 || sector > 0xffffffffU) return -1;
    if (device->sector_count != 0 && sector >= device->sector_count) return -1;
    return device->write((uint32_t)sector, buffer);
}