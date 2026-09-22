#!/usr/bin/env python3
"""QFS v1: deterministic read-only files for the QuantaOs image."""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

SECTOR_SIZE = 512
SUPERBLOCK_LBA = 258
MAGIC = b"QFSv1\0\0\0"
VERSION = 1
SUPERBLOCK_SIZE = 64
RECORD_SIZE = 64
PATH_SIZE = 48
FLAG_FILE = 1
SUPER = struct.Struct("<8sHHIIIIII")
RECORD = struct.Struct("<48sIIII")

@dataclass(frozen=True)
class QfsFile:
    path: str
    data: bytes
    lba: int = 0

    def record(self) -> bytes:
        encoded = self.path.encode("ascii")
        if not encoded or len(encoded) >= PATH_SIZE or not self.path.startswith("/"):
            raise ValueError("invalid QFS path")
        sectors = (len(self.data) + SECTOR_SIZE - 1) // SECTOR_SIZE
        return RECORD.pack(encoded + bytes(PATH_SIZE - len(encoded)), self.lba,
                           len(self.data), sectors, FLAG_FILE)

def build(files: list[tuple[str, bytes]], start_lba: int = SUPERBLOCK_LBA) -> bytes:
    objects = [QfsFile(path, data) for path, data in sorted(files)]
    table_lba = start_lba + 1
    data_lba = table_lba + ((len(objects) * RECORD_SIZE + SECTOR_SIZE - 1) // SECTOR_SIZE)
    records = []
    payload = bytearray()
    current = data_lba
    for item in objects:
        record = QfsFile(item.path, item.data, current)
        records.append(record.record())
        payload.extend(item.data)
        payload.extend(bytes((-len(item.data)) % SECTOR_SIZE))
        current += (len(item.data) + SECTOR_SIZE - 1) // SECTOR_SIZE
    table = b"".join(records)
    table += bytes((-len(table)) % SECTOR_SIZE)
    total_sectors = 1 + len(table) // SECTOR_SIZE + len(payload) // SECTOR_SIZE
    header = SUPER.pack(MAGIC, VERSION, SECTOR_SIZE, len(objects), table_lba,
                        data_lba, total_sectors, 0, 0)
    checksum = zlib.crc32(header[:28]) & 0xffffffff
    header = SUPER.pack(MAGIC, VERSION, SECTOR_SIZE, len(objects), table_lba,
                        data_lba, total_sectors, checksum, 0)
    return header + bytes(SUPERBLOCK_SIZE - len(header)) + bytes(SECTOR_SIZE - SUPERBLOCK_SIZE) + table + payload

def parse(image: bytes, start_lba: int = SUPERBLOCK_LBA) -> dict[str, bytes]:
    offset = start_lba * SECTOR_SIZE
    if len(image) < offset + SECTOR_SIZE:
        raise ValueError("QFS superblock is truncated")
    magic, version, sector_size, count, table_lba, data_lba, total, checksum, _ = SUPER.unpack_from(image, offset)
    if magic != MAGIC or version != VERSION or sector_size != SECTOR_SIZE:
        raise ValueError("invalid QFS superblock")
    expected = zlib.crc32(image[offset:offset + 28]) & 0xffffffff
    if checksum != expected or total == 0 or offset + total * SECTOR_SIZE > len(image):
        raise ValueError("invalid QFS checksum or size")
    result: dict[str, bytes] = {}
    table_offset = table_lba * SECTOR_SIZE
    for index in range(count):
        record_offset = table_offset + index * RECORD_SIZE
        path, lba, size, sectors, flags = RECORD.unpack_from(image, record_offset)
        if flags != FLAG_FILE or not path:
            raise ValueError("invalid QFS record")
        name = path.split(b"\0", 1)[0].decode("ascii")
        end = lba * SECTOR_SIZE + size
        if lba < data_lba or sectors == 0 or end > offset + total * SECTOR_SIZE:
            raise ValueError("QFS file extent is invalid")
        if name in result:
            raise ValueError("duplicate QFS path")
        result[name] = image[lba * SECTOR_SIZE:end]
    return result
