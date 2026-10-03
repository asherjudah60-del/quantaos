#include <quanta/vfs.h>

#define STORAGE_SECTOR_SIZE 512U

extern int quanta_arch_disk_read(uint32_t sector, void *buffer);
extern int quanta_arch_disk_write(uint32_t sector, const void *buffer);
extern int quanta_arch_ata_read(uint32_t device_index, uint32_t sector, void *buffer);
extern int quanta_arch_ata_write(uint32_t device_index, uint32_t sector, const void *buffer);
extern uint64_t quanta_arch_ata_sector_count(uint32_t device_index);
extern uint64_t quanta_arch_storage_physical(void);
extern uint64_t quanta_arch_storage_sectors(void);
extern int quanta_arch_memory_storage_read(uint32_t sector, void *buffer);

static int live_read(uint32_t sector, void *buffer) {
    return quanta_arch_memory_storage_read(sector, buffer);
}

static int disk0_read(uint32_t sector, void *buffer) { return quanta_arch_ata_read(0U, sector, buffer); }
static int disk0_write(uint32_t sector, const void *buffer) { return quanta_arch_ata_write(0U, sector, buffer); }
static int disk1_read(uint32_t sector, void *buffer) { return quanta_arch_ata_read(1U, sector, buffer); }
static int disk1_write(uint32_t sector, const void *buffer) { return quanta_arch_ata_write(1U, sector, buffer); }
static int disk2_read(uint32_t sector, void *buffer) { return quanta_arch_ata_read(2U, sector, buffer); }
static int disk2_write(uint32_t sector, const void *buffer) { return quanta_arch_ata_write(2U, sector, buffer); }
static int disk3_read(uint32_t sector, void *buffer) { return quanta_arch_ata_read(3U, sector, buffer); }
static int disk3_write(uint32_t sector, const void *buffer) { return quanta_arch_ata_write(3U, sector, buffer); }

static struct quanta_block_device disk_devices[4] = {
    { "disk0", STORAGE_SECTOR_SIZE, 0, disk0_read, disk0_write },
    { "disk1", STORAGE_SECTOR_SIZE, 0, disk1_read, disk1_write },
    { "disk2", STORAGE_SECTOR_SIZE, 0, disk2_read, disk2_write },
    { "disk3", STORAGE_SECTOR_SIZE, 0, disk3_read, disk3_write }
};
static struct quanta_block_device live_device = {
    "live", STORAGE_SECTOR_SIZE, 0, live_read, 0
};

static uint32_t detected_disk_count(void) {
    uint32_t count = 0;
    for (uint32_t index = 0; index < 4U; ++index) {
        uint64_t sectors = quanta_arch_ata_sector_count(index);
        disk_devices[index].sector_count = sectors;
        if (sectors != 0U) ++count;
    }
    return count;
}

const struct quanta_block_device *quanta_storage_boot_device(void) {
    if (quanta_arch_storage_physical() != 0U && quanta_arch_storage_sectors() != 0U)
        return &live_device;
    return quanta_arch_ata_sector_count(0U) != 0U ? &disk_devices[0] : 0;
}

uint32_t quanta_storage_device_count(void) {
    live_device.sector_count = quanta_arch_storage_sectors();
    return detected_disk_count() + (quanta_arch_storage_physical() != 0U &&
        quanta_arch_storage_sectors() != 0U ? 1U : 0U);
}

const struct quanta_block_device *quanta_storage_device_at(uint32_t index) {
    uint32_t output = 0;
    for (uint32_t disk = 0; disk < 4U; ++disk) {
        disk_devices[disk].sector_count = quanta_arch_ata_sector_count(disk);
        if (disk_devices[disk].sector_count == 0U) continue;
        if (output == index) return &disk_devices[disk];
        ++output;
    }
    if (output == index && quanta_arch_storage_physical() != 0U &&
        quanta_arch_storage_sectors() != 0U) {
        live_device.sector_count = quanta_arch_storage_sectors();
        return &live_device;
    }
    return 0;
}

const struct quanta_block_device *quanta_storage_find_device(const char *name) {
    if (name == 0) return 0;
    for (uint32_t index = 0; index < quanta_storage_device_count(); ++index) {
        const struct quanta_block_device *device = quanta_storage_device_at(index);
        uint32_t character = 0;
        while (name[character] && device->name[character] == name[character]) ++character;
        if (name[character] == 0 && device->name[character] == 0) return device;
    }
    return 0;
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