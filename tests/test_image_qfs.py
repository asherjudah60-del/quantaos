#!/usr/bin/env python3
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from qfs2 import list_dir, parse_volume
from disk_layout import (DISK_SIZE_BYTES, MBR_PARTITION_LBA, QFS2_LBA,
    SECTOR_SIZE, disk_sector_count)

image = ROOT / "build" / "quantaos.img"
SECTOR = SECTOR_SIZE
with image.open("rb") as source:
    mbr = source.read(SECTOR)
assert mbr[510:512] == b"\x55\xaa"
assert mbr[446] == 0x80
assert mbr[450] == 0x83
assert int.from_bytes(mbr[454:458], "little") == MBR_PARTITION_LBA
assert int.from_bytes(mbr[458:462], "little") == disk_sector_count() - MBR_PARTITION_LBA
assert image.stat().st_size == DISK_SIZE_BYTES
# The image is deliberately sparse and 10 GiB; only its initialized prefix is
# relevant to filesystem-format validation.
with image.open("rb") as source:
    prefix = source.read((QFS2_LBA + 4096) * SECTOR)
files = parse_volume(prefix)
assert files["/etc/motd"] == b"Welcome to QuantaOs.\n"
assert files["/bin/echo"][:4] == b"\x7fELF"
assert files["/home/quanta/Documents/readme"]
assert set(list_dir(prefix, "/home/quanta")) == {
    "Desktop", "Documents", "Downloads", "Music", "Pictures", "Trash", "Videos"
}
print("image QFS checks passed")
