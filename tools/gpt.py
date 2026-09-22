#!/usr/bin/env python3
"""GPT layout construction and validation for QuantaOs disk tooling."""
from __future__ import annotations

import struct
import uuid
import zlib
from dataclasses import dataclass

SECTOR = 512
GPT_HEADER_LBA = 1
GPT_ENTRY_LBA = 2
GPT_ENTRY_COUNT = 128
GPT_ENTRY_SIZE = 128
GPT_ENTRY_SECTORS = GPT_ENTRY_COUNT * GPT_ENTRY_SIZE // SECTOR
GPT_SIGNATURE = b"EFI PART"

BIOS_GUID = uuid.UUID("21686148-6449-6e6f-744e-656564454649")
ESP_GUID = uuid.UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b")
QFS_GUID = uuid.UUID("0fc63daf-8483-4772-8e79-3d69d8477de4")
EXFAT_GUID = uuid.UUID("7c3457ef-0000-11aa-aa11-00306543ecac")


@dataclass(frozen=True)
class Partition:
    name: str
    type_guid: uuid.UUID
    first_lba: int
    last_lba: int
    unique_guid: uuid.UUID

    @property
    def sectors(self) -> int:
        return self.last_lba - self.first_lba + 1


def _guid_bytes(value: uuid.UUID) -> bytes:
    return value.bytes_le


def _header(current: int, backup: int, first: int, last: int,
            entries_lba: int, entries_crc: int) -> bytes:
    raw = bytearray(SECTOR)
    struct.pack_into("<8sIIIIQQQQ16sQIII", raw, 0, GPT_SIGNATURE, 0x00010000,
                     92, 0, 0, current, backup, first, last, bytes(16),
                     entries_lba, GPT_ENTRY_COUNT, GPT_ENTRY_SIZE, entries_crc)
    header_crc = zlib.crc32(raw[:92]) & 0xffffffff
    struct.pack_into("<I", raw, 16, header_crc)
    return bytes(raw)


def _entry(partition: Partition) -> bytes:
    name = partition.name.encode("utf-16-le")
    if len(name) > 72:
        raise ValueError("GPT partition name is too long")
    return struct.pack("<16s16sQQQ72s", _guid_bytes(partition.type_guid),
                       _guid_bytes(partition.unique_guid), partition.first_lba,
                       partition.last_lba, 0, name + bytes(72 - len(name)))


def make_layout(disk_sectors: int, data_type: uuid.UUID = QFS_GUID) -> tuple[bytes, list[Partition]]:
    """Return initialized LBA 0..last GPT sectors and its protected partitions."""
    if disk_sectors < 131072:
        raise ValueError("GPT test/runtime disk must contain at least 64 MiB")
    backup_header_lba = disk_sectors - 1
    backup_entries_lba = backup_header_lba - GPT_ENTRY_SECTORS
    first_usable = GPT_ENTRY_LBA + GPT_ENTRY_SECTORS
    last_usable = backup_entries_lba - 1
    bios = Partition("BIOS boot", BIOS_GUID, first_usable, first_usable + 2047,
                     uuid.uuid4())
    esp = Partition("EFI System", ESP_GUID, bios.last_lba + 1,
                    bios.last_lba + 65536, uuid.uuid4())
    data = Partition("Quanta data", data_type, esp.last_lba + 1, last_usable,
                     uuid.uuid4())
    if data.first_lba >= data.last_lba:
        raise ValueError("disk has no usable data partition")
    partitions = [bios, esp, data]
    entries = b"".join(_entry(item) for item in partitions)
    entries += bytes(GPT_ENTRY_COUNT * GPT_ENTRY_SIZE - len(entries))
    entries_crc = zlib.crc32(entries) & 0xffffffff
    primary = _header(GPT_HEADER_LBA, backup_header_lba, first_usable,
                      last_usable, GPT_ENTRY_LBA, entries_crc)
    backup = _header(backup_header_lba, GPT_HEADER_LBA, first_usable,
                     last_usable, backup_entries_lba, entries_crc)
    image = bytearray((backup_header_lba + 1) * SECTOR)
    image[510:512] = b"\x55\xaa"
    image[446:462] = bytes([0]) + bytes(3) + b"\0\0\0\0" + bytes(4) + bytes(4)
    image[450] = 0xee
    struct.pack_into("<II", image, 454, 1, min(disk_sectors - 1, 0xffffffff))
    image[SECTOR:2 * SECTOR] = primary
    image[GPT_ENTRY_LBA * SECTOR:(GPT_ENTRY_LBA + GPT_ENTRY_SECTORS) * SECTOR] = entries
    image[backup_entries_lba * SECTOR:backup_header_lba * SECTOR] = entries
    image[backup_header_lba * SECTOR:(backup_header_lba + 1) * SECTOR] = backup
    return bytes(image), partitions


def validate_layout(image: bytes, partitions: list[Partition]) -> None:
    if len(image) % SECTOR or len(image) < 2 * SECTOR:
        raise ValueError("GPT image is not sector aligned")
    disk_sectors = len(image) // SECTOR
    if image[510:512] != b"\x55\xaa" or image[450] != 0xee:
        raise ValueError("protective MBR is missing")
    for header_lba, entries_lba, expected_current in (
        (1, GPT_ENTRY_LBA, 1),
        (disk_sectors - 1, disk_sectors - 1 - GPT_ENTRY_SECTORS, disk_sectors - 1),
    ):
        header = image[header_lba * SECTOR:(header_lba + 1) * SECTOR]
        if header[:8] != GPT_SIGNATURE or struct.unpack_from("<Q", header, 24)[0] != expected_current:
            raise ValueError("invalid GPT header")
        header_size, saved_crc = struct.unpack_from("<II", header, 12)
        check = bytearray(header[:header_size])
        struct.pack_into("<I", check, 16, 0)
        if zlib.crc32(check) & 0xffffffff != saved_crc:
            raise ValueError("invalid GPT header checksum")
        actual_entries = image[entries_lba * SECTOR:(entries_lba + GPT_ENTRY_SECTORS) * SECTOR]
        if zlib.crc32(actual_entries) & 0xffffffff != struct.unpack_from("<I", header, 88)[0]:
            raise ValueError("invalid GPT entry checksum")
    for previous, current in zip(partitions, partitions[1:]):
        if previous.last_lba >= current.first_lba:
            raise ValueError("GPT partitions overlap")
    if partitions[0].first_lba < 34 or partitions[-1].last_lba >= disk_sectors - 33:
        raise ValueError("GPT partition lies outside usable disk bounds")
