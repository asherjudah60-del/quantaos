#ifndef QUANTA_DISK_LAYOUT_H
#define QUANTA_DISK_LAYOUT_H

/* Keep in sync with tools/disk_layout.py. These are on-disk ABI values. */
#define QUANTA_DISK_SECTOR_SIZE 512U
#define QUANTA_DISK_ACCOUNT_LBA 512U
#define QUANTA_DISK_ACCOUNT_SLOT_COUNT 2U
#define QUANTA_DISK_QFS2_LBA 514U
#define QUANTA_DISK_MBR_PARTITION_LBA 1U

#endif
