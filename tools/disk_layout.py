"""Single source of truth for QuantaOS's BIOS disk layout."""

SECTOR_SIZE = 512
DISK_SIZE_BYTES = 10 * 1024 * 1024 * 1024
ACCOUNT_LBA = 512
ACCOUNT_SLOT_COUNT = 2
QFS2_LBA = 514
MBR_PARTITION_LBA = 1


def disk_sector_count(size_bytes: int = DISK_SIZE_BYTES) -> int:
    if size_bytes <= 0 or size_bytes % SECTOR_SIZE:
        raise ValueError("disk size must be a positive whole number of sectors")
    return size_bytes // SECTOR_SIZE


def validate_boot_prefix(prefix_bytes: int) -> None:
    if prefix_bytes > ACCOUNT_LBA * SECTOR_SIZE:
        raise ValueError("boot prefix overlaps reserved account sectors")
