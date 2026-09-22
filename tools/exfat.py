#!/usr/bin/env python3
"""Host-side exFAT image formatting and boot-region validation."""
from __future__ import annotations

import pathlib
import shutil
import subprocess

SECTOR = 512
BOOT_REGION_SIZE = 12 * SECTOR
OEM_NAME = b"EXFAT   "
SIGNATURE = b"\x55\xaa"


def _read_boot_region(image: pathlib.Path) -> bytes:
    with image.open("rb") as source:
        data = source.read(BOOT_REGION_SIZE)
    if len(data) != BOOT_REGION_SIZE:
        raise ValueError("exFAT image is smaller than its boot region")
    return data


def validate_boot_region(image: pathlib.Path) -> None:
    """Validate the fields needed before handing an image to an exFAT provider."""
    boot = _read_boot_region(image)
    if boot[3:11] != OEM_NAME or boot[510:512] != SIGNATURE:
        raise ValueError("invalid exFAT boot signature")
    if boot[11:64] != bytes(53):
        raise ValueError("exFAT must have a zeroed reserved boot region")
    partition_offset = int.from_bytes(boot[64:72], "little")
    volume_length = int.from_bytes(boot[72:80], "little")
    fat_offset = int.from_bytes(boot[80:84], "little")
    fat_length = int.from_bytes(boot[84:88], "little")
    heap_offset = int.from_bytes(boot[88:92], "little")
    cluster_count = int.from_bytes(boot[92:96], "little")
    root_cluster = int.from_bytes(boot[96:100], "little")
    bytes_per_sector_shift = boot[108]
    sectors_per_cluster_shift = boot[109]
    if partition_offset != 0:
        raise ValueError("standalone exFAT image must start at sector zero")
    if bytes_per_sector_shift != 9 or not 0 <= sectors_per_cluster_shift <= 25:
        raise ValueError("unsupported exFAT sector or cluster size")
    if volume_length == 0 or fat_length == 0 or cluster_count == 0 or root_cluster < 2:
        raise ValueError("invalid exFAT geometry")
    cluster_heap_end = heap_offset + (cluster_count << sectors_per_cluster_shift)
    if fat_offset < 24 or heap_offset < fat_offset + fat_length or cluster_heap_end > volume_length:
        raise ValueError("exFAT regions overlap or exceed the volume")
    if volume_length * SECTOR > image.stat().st_size:
        raise ValueError("exFAT volume exceeds image")


def format_image(image: pathlib.Path, size: int, label: str = "QUANTA") -> None:
    """Create and validate a sparse standalone exFAT image."""
    if size < 1 * 1024 * 1024 or size % SECTOR:
        raise ValueError("exFAT image size must be at least 1 MiB and sector aligned")
    if not 1 <= len(label) <= 15 or not label.isascii():
        raise ValueError("exFAT labels must contain 1-15 ASCII characters")
    formatter = shutil.which("mkfs.exfat")
    checker = shutil.which("fsck.exfat")
    if formatter is None or checker is None:
        raise RuntimeError("mkfs.exfat and fsck.exfat are required")
    image.parent.mkdir(parents=True, exist_ok=True)
    with image.open("wb") as output:
        output.truncate(size)
    subprocess.run([formatter, "-L", label, str(image)], check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    validate_boot_region(image)
    subprocess.run([checker, "-n", str(image)], check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)