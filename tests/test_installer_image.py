#!/usr/bin/env python3
"""Inspect the sparse installed-disk artifact without reading its empty tail."""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from qfs2 import parse_volume

SECTOR = 512
BIOS_LBA = 34
ESP_LBA = BIOS_LBA + 2 * 1024 * 1024 // SECTOR
ROOT_LBA = ESP_LBA + 64 * 1024 * 1024 // SECTOR

image = ROOT / "build" / "quantaos-installer.img"
assert image.stat().st_size == 8 * 1024 * 1024 * 1024
with image.open("rb") as source:
    source.seek(510)
    assert source.read(2) == b"\x55\xaa"
    source.seek(446)
    assert source.read(5)[4] == 0xee
    source.seek(SECTOR)
    assert source.read(8) == b"EFI PART"
    source.seek(BIOS_LBA * SECTOR)
    assert source.read(2) != b"\0\0"
    source.seek(ESP_LBA * SECTOR + 82)
    assert source.read(3) == b"FAT"
    source.seek(ROOT_LBA * SECTOR)
    prefix = source.read(8192 * SECTOR)
files = parse_volume(bytes(ROOT_LBA * SECTOR) + prefix, start_lba=ROOT_LBA)
assert files["/etc/os-release"].startswith(b"NAME=QuantaOs")
print("installer disk layout checks passed")
