#!/usr/bin/env python3
"""Guard the fixed BIOS layout used by every host-side image builder."""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from disk_layout import (ACCOUNT_LBA, DISK_SIZE_BYTES, MBR_PARTITION_LBA,
    QFS2_LBA, SECTOR_SIZE, disk_sector_count, validate_boot_prefix)

assert SECTOR_SIZE == 512
assert MBR_PARTITION_LBA == 1
assert ACCOUNT_LBA == 512
assert QFS2_LBA == 514
assert disk_sector_count() == DISK_SIZE_BYTES // SECTOR_SIZE
assert disk_sector_count() - MBR_PARTITION_LBA <= 0xffffffff
validate_boot_prefix(ACCOUNT_LBA * SECTOR_SIZE)
try:
    validate_boot_prefix(ACCOUNT_LBA * SECTOR_SIZE + 1)
except ValueError:
    pass
else:
    raise AssertionError("overlapping boot prefix was accepted")

print("disk layout checks passed")
