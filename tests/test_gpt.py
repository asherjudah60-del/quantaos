#!/usr/bin/env python3
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from gpt import EXFAT_GUID, QFS_GUID, make_layout, validate_layout

image, partitions = make_layout(2 * 1024 * 1024, QFS_GUID)
validate_layout(image, partitions)
assert [item.name for item in partitions] == ["BIOS boot", "EFI System", "Quanta data"]
assert partitions[-1].type_guid == QFS_GUID

image, partitions = make_layout(2 * 1024 * 1024, EXFAT_GUID)
validate_layout(image, partitions)
assert partitions[-1].type_guid == EXFAT_GUID

corrupt = bytearray(image)
corrupt[512 + 24] ^= 1
try:
    validate_layout(bytes(corrupt), partitions)
except ValueError:
    pass
else:
    raise AssertionError("corrupt GPT header was accepted")
print("GPT layout checks passed")
