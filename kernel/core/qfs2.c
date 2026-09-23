#include <stdint.h>
#include <quanta/arch.h>
#include <quanta/vfs.h>

#define QFS2_LBA 258U
#define QFS2_SECTOR 512U
#define QFS2_ROOT_INODE 1U
#define QFS2_FILE 1U
#define QFS2_DIRECTORY 2U

struct qfs2_superblock {
    char magic[8]; uint16_t version; uint16_t sector_size;
    uint32_t total_sectors, bitmap_lba, inode_lba, inode_sectors;
    uint32_t entry_lba, entry_sectors, root_inode, checksum, reserved;
} __attribute__((packed));
struct qfs2_inode {
    uint64_t inode; uint32_t kind, mode, uid, gid, size, first_sector;
    uint64_t sectors;
    uint64_t generation; uint32_t reserved, flags;
} __attribute__((packed));
struct qfs2_dentry {
    uint64_t parent, inode; uint8_t kind, name_length; char name[48];
} __attribute__((packed));

static uint8_t sector[QFS2_SECTOR];
static void copy_bytes(void *destination, const void *source, uint32_t length) {
    uint8_t *out = destination; const uint8_t *in = source;
    while (length--) *out++ = *in++;
}
static uint32_t crc32(const uint8_t *data, uint32_t length) {
    uint32_t crc = 0xffffffffU, index, bit;
    for (index = 0; index < length; ++index) {
        crc ^= data[index];
        for (bit = 0; bit < 8U; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
static int read_sector(uint32_t number) {
    return quanta_block_device_read(quanta_storage_boot_device(), number, sector);
}
static int write_sector(uint32_t number) {
    return quanta_block_device_write(quanta_storage_boot_device(), number, sector);
}
static int load_super(struct qfs2_superblock *super) {
    if (read_sector(QFS2_LBA) != 0) return -1;
    copy_bytes(super, sector, sizeof(*super));
    if (super->magic[0] != 'Q' || super->magic[1] != 'F' || super->magic[2] != 'S' ||
        super->magic[3] != 'v' || super->magic[4] != '2' || super->version != 2U ||
        super->sector_size != QFS2_SECTOR || super->root_inode != QFS2_ROOT_INODE ||
        super->total_sectors == 0U || super->bitmap_lba < QFS2_LBA + 1U ||
        super->inode_lba <= super->bitmap_lba || super->inode_sectors == 0U ||
        super->entry_lba < super->inode_lba ||
        super->entry_lba + super->entry_sectors > super->total_sectors ||
        super->checksum != crc32(sector, 40U)) return -1;
    return 0;
}
static int read_metadata(const struct qfs2_superblock *super, uint64_t offset,
    void *output, uint32_t length) {
    uint8_t *out = output;
    uint64_t limit = (uint64_t)super->total_sectors * QFS2_SECTOR;
    uint32_t sector_number, byte, count;
    if (offset > limit || length > limit - offset) return -1;
    while (length) {
        sector_number = (uint32_t)(offset / QFS2_SECTOR);
        byte = (uint32_t)(offset % QFS2_SECTOR);
        if (read_sector(sector_number) != 0) return -1;
        count = QFS2_SECTOR - byte;
        if (count > length) count = length;
        copy_bytes(out, sector + byte, count);
        out += count; offset += count; length -= count;
    }
    return 0;
}
static int write_metadata(const struct qfs2_superblock *super, uint64_t offset,
    const void *input, uint32_t length) {
    const uint8_t *in = input;
    uint64_t limit = (uint64_t)super->total_sectors * QFS2_SECTOR;
    uint32_t sector_number, byte, count;
    if (offset > limit || length > limit - offset) return -1;
    while (length) {
        sector_number = (uint32_t)(offset / QFS2_SECTOR);
        byte = (uint32_t)(offset % QFS2_SECTOR);
        if (read_sector(sector_number) != 0) return -1;
        count = QFS2_SECTOR - byte;
        if (count > length) count = length;
        copy_bytes(sector + byte, in, count);
        if (write_sector(sector_number) != 0) return -1;
        in += count; offset += count; length -= count;
    }
    return 0;
}
static int read_inode(const struct qfs2_superblock *super, uint64_t number,
    struct qfs2_inode *inode) {
    uint64_t offset;
    if (number == 0U || number > ((uint64_t)super->inode_sectors * QFS2_SECTOR) /
        sizeof(*inode)) return -1;
    offset = (uint64_t)super->inode_lba * QFS2_SECTOR + (number - 1U) * sizeof(*inode);
    return read_metadata(super, offset, inode, sizeof(*inode));
}
static int write_inode(const struct qfs2_superblock *super, uint64_t number,
    const struct qfs2_inode *inode) {
    uint64_t offset;
    if (number == 0U || number > ((uint64_t)super->inode_sectors * QFS2_SECTOR) /
        sizeof(*inode)) return -1;
    offset = (uint64_t)super->inode_lba * QFS2_SECTOR + (number - 1U) * sizeof(*inode);
    return write_metadata(super, offset, inode, sizeof(*inode));
}
static int name_equal(const char *left, uint32_t left_length, const char *right) {
    uint32_t index = 0;
    while (right[index] && index < left_length && left[index] == right[index]) ++index;
    return right[index] == 0 && index == left_length;
}
static int find_child(const struct qfs2_superblock *super, uint64_t parent,
    const char *name, uint64_t *result) {
    struct qfs2_dentry entry;
    uint32_t count = (super->entry_sectors * QFS2_SECTOR) / sizeof(entry), index;
    uint32_t offset = super->entry_lba * QFS2_SECTOR;
    for (index = 0; index < count; ++index) {
        if (read_metadata(super, (uint64_t)offset + (uint64_t)index * sizeof(entry),
            &entry, sizeof(entry)) != 0) return -1;
        if (entry.parent == parent && entry.name_length > 0U &&
            entry.name_length <= sizeof(entry.name) &&
            name_equal(entry.name, entry.name_length, name)) {
            *result = entry.inode; return 0;
        }
    }
    return -1;
}
static int find_path(const struct qfs2_superblock *super, const char *path,
    uint64_t *result) {
    char component[49]; uint64_t current = QFS2_ROOT_INODE, child;
    uint32_t index = 0, length;
    if (path == 0 || path[0] != '/') return -1;
    while (path[index] == '/') ++index;
    if (path[index] == 0) { *result = current; return 0; }
    while (path[index]) {
        length = 0;
        while (path[index] && path[index] != '/') {
            if (length + 1U >= sizeof(component)) return -1;
            component[length++] = path[index++];
        }
        component[length] = 0;
        while (path[index] == '/') ++index;
        if (find_child(super, current, component, &child) != 0) return -1;
        current = child;
    }
    *result = current;
    return 0;
}
int quanta_qfs2_read(const char *path, char *output, uint32_t capacity) {
    struct qfs2_superblock super; struct qfs2_inode inode; uint64_t number;
    uint32_t copied = 0, count;
    if (output == 0 || capacity == 0U || load_super(&super) != 0 ||
        find_path(&super, path, &number) != 0 || read_inode(&super, number, &inode) != 0 ||
        inode.kind != QFS2_FILE || inode.size >= capacity) return -1;
    if (inode.size == 0U) { output[0] = 0; return 0; }
    if (inode.sectors == 0U || inode.first_sector < super.entry_lba ||
        inode.first_sector + inode.sectors > super.total_sectors) return -1;
    while (copied < inode.size) {
        if (read_sector(inode.first_sector + copied / QFS2_SECTOR) != 0) return -1;
        count = inode.size - copied;
        if (count > QFS2_SECTOR) count = QFS2_SECTOR;
        copy_bytes(output + copied, sector, count); copied += count;
    }
    output[copied] = 0;
    return (int)copied;
}
int quanta_qfs2_list(const char *path, char *output, uint32_t capacity) {
    struct qfs2_superblock super; struct qfs2_inode inode; struct qfs2_dentry entry;
    uint64_t number; uint32_t count, index, used = 0, offset;
    if (output == 0 || capacity < 2U || load_super(&super) != 0 ||
        find_path(&super, path, &number) != 0 || read_inode(&super, number, &inode) != 0 ||
        inode.kind != QFS2_DIRECTORY) return -1;
    count = (super.entry_sectors * QFS2_SECTOR) / sizeof(entry);
    offset = super.entry_lba * QFS2_SECTOR;
    for (index = 0; index < count; ++index) {
        if (read_metadata(&super, offset + index * sizeof(entry), &entry, sizeof(entry)) != 0) return -1;
        if (entry.parent != number || entry.name_length == 0U || entry.name_length > sizeof(entry.name) ||
            used + entry.name_length + 2U >= capacity) continue;
        copy_bytes(output + used, entry.name, entry.name_length); used += entry.name_length;
        output[used++] = ' '; output[used] = 0;
    }
    if (used == 0U) {
        output[0] = 0;
        return 0;
    }
    output[used - 1U] = '\n'; output[used] = 0;
    return (int)used;
}
int quanta_qfs2_stat(const char *path, struct quanta_file_stat *stat) {
    struct qfs2_superblock super; struct qfs2_inode inode; uint64_t number;
    if (path == 0 || stat == 0 || load_super(&super) != 0 ||
        find_path(&super, path, &number) != 0 || read_inode(&super, number, &inode) != 0) return -1;
    stat->inode = inode.inode; stat->size = inode.size; stat->blocks = inode.sectors;
    stat->mode = inode.mode; stat->uid = inode.uid; stat->gid = inode.gid;
    stat->type = inode.kind == QFS2_DIRECTORY ? QUANTA_FILE_DIRECTORY : QUANTA_FILE_REGULAR;
    stat->generation = inode.generation;
    return 0;
}

static int split_path(const char *path, char *parent, char *name) {
    uint32_t length = 0, cut = 0, index;
    if (path == 0 || path[0] != '/') return -1;
    while (path[length]) { if (path[length] == '/') cut = length; ++length; }
    while (length > 1U && path[length - 1U] == '/') --length;
    while (cut > 0U && cut >= length) {
        --cut;
        while (cut > 0U && path[cut] != '/') --cut;
    }
    if (length <= 1U || path[cut + 1U] == 0 ||
        length - cut - 1U >= 49U || cut >= 96U) return -1;
    if (cut == 0U) {
        parent[0] = '/';
        parent[1] = 0;
    } else {
        for (index = 0; index < cut; ++index) parent[index] = path[index];
        parent[cut] = 0;
    }
    for (index = 0; index + cut + 1U < length; ++index) name[index] = path[cut + 1U + index];
    name[length - cut - 1U] = 0;
    return 0;
}

static int create_node(const char *path, uint32_t kind) {
    struct qfs2_superblock super; struct qfs2_inode inode; struct qfs2_dentry entry;
    char parent[96], name[49]; uint64_t parent_inode, new_inode = 0, check;
    uint32_t index, count, name_length = 0; uint64_t offset;
    if (load_super(&super) != 0) { quanta_arch_write_marker("QUANTA_MKDIR_SUPER_FAILED\n"); return -1; }
    if (split_path(path, parent, name) != 0) { quanta_arch_write_marker("QUANTA_MKDIR_PATH_FAILED\n"); return -1; }
    if (find_path(&super, parent, &parent_inode) != 0 ||
        read_inode(&super, parent_inode, &inode) != 0 || inode.kind != QFS2_DIRECTORY) {
        quanta_arch_write_marker("QUANTA_MKDIR_PARENT_FAILED\n"); return -1;
    }
    if (find_child(&super, parent_inode, name, &check) == 0) {
        quanta_arch_write_marker("QUANTA_MKDIR_PARENT_FAILED\n"); return -1;
    }
    count = (uint32_t)(((uint64_t)super.inode_sectors * QFS2_SECTOR) / sizeof(inode));
    for (index = 2; index <= count; ++index) {
        if (read_inode(&super, index, &inode) != 0) {
            quanta_arch_write_marker("QUANTA_MKDIR_INODE_READ_FAILED\n"); return -1;
        }
        if (inode.inode == 0U) { new_inode = index; break; }
    }
    if (new_inode == 0U) { quanta_arch_write_marker("QUANTA_MKDIR_NO_INODE\n"); return -1; }
    count = (uint32_t)(((uint64_t)super.entry_sectors * QFS2_SECTOR) / sizeof(entry));
    offset = (uint64_t)super.entry_lba * QFS2_SECTOR;
    for (index = 0; index < count; ++index) {
        if (read_metadata(&super, offset + (uint64_t)index * sizeof(entry), &entry,
            sizeof(entry)) != 0) {
            quanta_arch_write_marker("QUANTA_MKDIR_ENTRY_READ_FAILED\n"); return -1;
        }
        if (entry.name_length == 0U) break;
    }
    if (index == count) { quanta_arch_write_marker("QUANTA_MKDIR_NO_ENTRY\n"); return -1; }
    while (name[name_length]) ++name_length;
    inode.inode = new_inode;
    inode.kind = kind;
    inode.mode = kind == QFS2_DIRECTORY ? 0755U : 0644U;
    inode.uid = 1000U; inode.gid = 1000U; inode.size = 0U; inode.first_sector = 0U;
    inode.sectors = 0U; inode.generation = 1U; inode.reserved = 0U; inode.flags = 0U;
    if (write_inode(&super, new_inode, &inode) != 0) {
        quanta_arch_write_marker("QUANTA_MKDIR_INODE_WRITE_FAILED\n"); return -1;
    }
    entry.parent = parent_inode; entry.inode = new_inode; entry.kind = (uint8_t)kind;
    for (count = 0; count < sizeof(entry.name); ++count) entry.name[count] = 0;
    copy_bytes(entry.name, name, name_length);
    entry.name_length = (uint8_t)name_length;
    if (write_metadata(&super, offset + (uint64_t)index * sizeof(entry), &entry,
        sizeof(entry)) != 0) {
        inode.inode = 0U; inode.kind = 0U; inode.mode = 0U; inode.uid = 0U;
        inode.gid = 0U; inode.size = 0U; inode.first_sector = 0U; inode.sectors = 0U;
        inode.generation = 0U; inode.reserved = 0U; inode.flags = 0U;
        write_inode(&super, new_inode, &inode);
        return -1;
    }
    if (read_inode(&super, new_inode, &inode) != 0 || inode.inode != new_inode ||
        inode.kind != kind || find_child(&super, parent_inode, name, &check) != 0 ||
        check != new_inode) {
        quanta_arch_write_marker("QUANTA_MKDIR_VERIFY_FAILED\n");
        return -1;
    }
    return 0;
}
int quanta_qfs2_mkdir(const char *path) { return create_node(path, QFS2_DIRECTORY); }
int quanta_qfs2_create(const char *path) { return create_node(path, QFS2_FILE); }
static const struct quanta_fs_provider qfs2_provider = {
    "qfs2", quanta_qfs2_read, quanta_qfs2_list, quanta_qfs2_stat
};
const struct quanta_fs_provider *quanta_qfs_v2_provider(void) { return &qfs2_provider; }
uint64_t quanta_qfs_v2_size_bytes(void) {
    struct qfs2_superblock super;
    return load_super(&super) == 0 ? (uint64_t)super.total_sectors * QFS2_SECTOR : 0;
}
