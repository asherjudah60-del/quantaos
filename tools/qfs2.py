#!/usr/bin/env python3
"""QFS v2 metadata contract for the writable filesystem."""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

SECTOR = 512
MAGIC = b"QFSv2\0\0\0"
VERSION = 2
ROOT_INODE = 1
TYPE_FILE = 1
TYPE_DIRECTORY = 2
MODE_FILE = 0o644
MODE_DIRECTORY = 0o755
SUPER = struct.Struct("<8sHHIIIIIIIII")
INODE = struct.Struct("<QIIIIIIQQII")
DENTRY = struct.Struct("<QQBB48s")

SUPERBLOCK_LBA = 258
DEFAULT_VOLUME_SECTORS = (10 * 1024 * 1024 * 1024 // SECTOR) - SUPERBLOCK_LBA

@dataclass(frozen=True)
class Inode:
    inode: int
    kind: int
    mode: int
    uid: int = 1000
    gid: int = 1000
    size: int = 0
    first_sector: int = 0
    sectors: int = 0
    generation: int = 1

    def encode(self) -> bytes:
        return INODE.pack(self.inode, self.kind, self.mode, self.uid, self.gid,
                          self.size, self.first_sector, self.sectors,
                          self.generation, 0, 0)

@dataclass(frozen=True)
class DirectoryEntry:
    parent: int
    inode: int
    name: str
    kind: int

    def encode(self) -> bytes:
        encoded = self.name.encode("ascii")
        if not encoded or len(encoded) > 48 or "/" in self.name or self.name in (".", ".."):
            raise ValueError("invalid directory entry")
        return DENTRY.pack(self.parent, self.inode, self.kind, len(encoded),
                           encoded + bytes(48 - len(encoded)))

def _checksum(data: bytes) -> int:
    return zlib.crc32(data) & 0xffffffff

def build_metadata(inodes: list[Inode], entries: list[DirectoryEntry],
                   total_sectors: int, bitmap_lba: int = 259,
                   inode_reserve_sectors: int = 0, entry_reserve_sectors: int = 0) -> bytes:
    if not any(item.inode == ROOT_INODE and item.kind == TYPE_DIRECTORY for item in inodes):
        raise ValueError("QFS root inode is required")
    inode_lba = bitmap_lba + 1
    inode_sectors = max((len(inodes) * INODE.size + SECTOR - 1) // SECTOR,
                        inode_reserve_sectors)
    entry_lba = inode_lba + inode_sectors
    entry_sectors = max((len(entries) * DENTRY.size + SECTOR - 1) // SECTOR,
                        entry_reserve_sectors)
    bitmap_sectors = 1
    header = SUPER.pack(MAGIC, VERSION, SECTOR, total_sectors, bitmap_lba,
                        inode_lba, inode_sectors, entry_lba, entry_sectors,
                        ROOT_INODE, 0, 0)
    checksum = _checksum(header[:40])
    header = SUPER.pack(MAGIC, VERSION, SECTOR, total_sectors, bitmap_lba,
                        inode_lba, inode_sectors, entry_lba, entry_sectors,
                        ROOT_INODE, checksum, 0)
    inode_data = b"".join(item.encode() for item in inodes)
    entry_data = b"".join(item.encode() for item in entries)
    if len(inode_data) > inode_sectors * SECTOR or len(entry_data) > entry_sectors * SECTOR:
        raise ValueError("QFS v2 metadata reserve is too small")
    return (header + bytes(SECTOR - len(header)) + bytes(bitmap_sectors * SECTOR)
            + inode_data + bytes(inode_sectors * SECTOR - len(inode_data))
            + entry_data + bytes(entry_sectors * SECTOR - len(entry_data)))

def validate_metadata(data: bytes) -> None:
    if len(data) < SECTOR:
        raise ValueError("QFS v2 superblock is truncated")
    fields = SUPER.unpack_from(data)
    magic, version, sector_size, total, bitmap_lba, inode_lba, inode_sectors, entry_lba, entry_sectors, root, checksum, _ = fields
    if magic != MAGIC or version != VERSION or sector_size != SECTOR or root != ROOT_INODE:
        raise ValueError("invalid QFS v2 superblock")
    if checksum != _checksum(data[:40]):
        raise ValueError("invalid QFS v2 checksum")
    if total == 0 or bitmap_lba < 259 or inode_lba <= bitmap_lba or entry_lba < inode_lba:
        raise ValueError("invalid QFS v2 metadata regions")
    end = (entry_lba + entry_sectors) * SECTOR
    if end > total * SECTOR or inode_sectors == 0:
        raise ValueError("QFS v2 metadata exceeds filesystem")


def _parents(files: list[tuple[str, bytes]]) -> list[str]:
    result = {"/"}
    for path, _ in files:
        if not path.startswith("/") or path == "/" or "//" in path:
            raise ValueError("invalid QFS v2 path")
        pieces = path.strip("/").split("/")
        for index in range(1, len(pieces)):
            result.add("/" + "/".join(pieces[:index]))
    return sorted(result, key=lambda item: (item.count("/"), item))


def build_volume(files: list[tuple[str, bytes]], *, total_sectors: int = DEFAULT_VOLUME_SECTORS,
                 start_lba: int = SUPERBLOCK_LBA) -> bytes:
    """Format the allocated prefix of a sparse QFS v2 volume.

    LBA fields are disk-relative so the same bytes can be placed directly after
    the boot/account reservation.  `total_sectors` describes the whole sparse
    volume; the returned value intentionally contains only initialized sectors.
    """
    files = sorted(files)
    directories = _parents(files)
    inode_for = {path: index + 1 for index, path in enumerate(directories)}
    next_inode = len(inode_for) + 1
    file_inodes: dict[str, int] = {}
    for path, _ in files:
        if path in inode_for:
            raise ValueError("QFS v2 path is both file and directory")
        file_inodes[path] = next_inode
        next_inode += 1
    entries: list[DirectoryEntry] = []
    for path in directories[1:]:
        parent, name = path.rsplit("/", 1)
        entries.append(DirectoryEntry(inode_for[parent or "/"], inode_for[path], name, TYPE_DIRECTORY))
    for path, _ in files:
        parent, name = path.rsplit("/", 1)
        entries.append(DirectoryEntry(inode_for[parent or "/"], file_inodes[path], name, TYPE_FILE))
    inode_sectors = max(16, ((next_inode - 1) * INODE.size + SECTOR - 1) // SECTOR)
    entry_lba = start_lba + 2 + inode_sectors
    entry_sectors = max(32, (len(entries) * DENTRY.size + SECTOR - 1) // SECTOR)
    data_lba = entry_lba + entry_sectors
    inodes = [Inode(inode_for[path], TYPE_DIRECTORY, MODE_DIRECTORY) for path in directories]
    payload = bytearray()
    current_lba = data_lba
    for path, data in files:
        sectors = max(1, (len(data) + SECTOR - 1) // SECTOR)
        inodes.append(Inode(file_inodes[path], TYPE_FILE,
                            0o755 if path.startswith("/bin/") else MODE_FILE,
                            size=len(data), first_sector=current_lba, sectors=sectors))
        payload.extend(data)
        payload.extend(bytes(sectors * SECTOR - len(data)))
        current_lba += sectors
    if current_lba - start_lba > total_sectors:
        raise ValueError("QFS v2 contents exceed volume")
    metadata = build_metadata(inodes, entries, total_sectors=start_lba + total_sectors,
                              bitmap_lba=start_lba + 1,
                              inode_reserve_sectors=inode_sectors,
                              entry_reserve_sectors=entry_sectors)
    # Mark all format metadata and initial payload sectors allocated.  The
    # bitmap is disk-relative and reserves bit zero for LBA zero.
    bitmap = bytearray(SECTOR)
    for lba in range(start_lba, current_lba):
        bitmap[lba // 8] |= 1 << (lba % 8)
    metadata = metadata[:SECTOR] + bytes(bitmap) + metadata[SECTOR * 2:]
    return metadata + payload


def parse_volume(image: bytes, start_lba: int = SUPERBLOCK_LBA) -> dict[str, bytes]:
    """Read the initialized QFS v2 objects from a disk image for hosted tests."""
    base = start_lba * SECTOR
    if len(image) < base + SECTOR:
        raise ValueError("QFS v2 superblock is truncated")
    validate_metadata(image[base:])
    fields = SUPER.unpack_from(image, base)
    _, _, _, _, _, inode_lba, inode_sectors, entry_lba, entry_sectors, _, _, _ = fields
    inode_data = image[inode_lba * SECTOR:(inode_lba + inode_sectors) * SECTOR]
    entries_data = image[entry_lba * SECTOR:(entry_lba + entry_sectors) * SECTOR]
    inodes = {INODE.unpack_from(inode_data, offset)[0]: INODE.unpack_from(inode_data, offset)
              for offset in range(0, len(inode_data) - INODE.size + 1, INODE.size)
              if INODE.unpack_from(inode_data, offset)[0] != 0}
    paths = {ROOT_INODE: "/"}
    pending = [DENTRY.unpack_from(entries_data, offset)
               for offset in range(0, len(entries_data) - DENTRY.size + 1, DENTRY.size)
               if DENTRY.unpack_from(entries_data, offset)[3] != 0]
    while pending:
        before = len(pending)
        remaining = []
        for entry in pending:
            if entry[0] not in paths:
                remaining.append(entry)
                continue
            paths[entry[1]] = paths[entry[0]].rstrip("/") + "/" + entry[4][:entry[3]].decode("ascii")
        pending = remaining
        if len(pending) == before:
            raise ValueError("orphan QFS v2 directory entry")
    result: dict[str, bytes] = {}
    for inode, path in paths.items():
        values = inodes.get(inode)
        if values and values[1] == TYPE_FILE:
            _, _, _, _, _, size, first, _, _, _, _ = values
            result[path] = image[first * SECTOR:first * SECTOR + size]
    return result


def _split_path(path: str) -> tuple[str, str]:
    if not path.startswith("/") or path == "/":
        raise ValueError("invalid mkdir path")
    trimmed = path.rstrip("/")
    parent, name = trimmed.rsplit("/", 1)
    if not name:
        raise ValueError("invalid mkdir path")
    return parent or "/", name


def _inode_table(image: bytearray, start_lba: int = SUPERBLOCK_LBA):
    validate_metadata(image[start_lba * SECTOR:])
    fields = SUPER.unpack_from(image, start_lba * SECTOR)
    _, _, _, _, _, inode_lba, inode_sectors, entry_lba, entry_sectors, _, _, _ = fields
    inode_off = inode_lba * SECTOR
    entry_off = entry_lba * SECTOR
    inode_data = image[inode_off:inode_off + inode_sectors * SECTOR]
    entry_data = image[entry_off:entry_off + entry_sectors * SECTOR]
    return fields, inode_off, inode_data, entry_off, entry_data


def _paths_from_image(image: bytes, start_lba: int = SUPERBLOCK_LBA) -> dict[int, str]:
    fields, _, inode_data, _, entry_data = _inode_table(bytearray(image), start_lba)
    del fields
    inodes = {INODE.unpack_from(inode_data, offset)[0]: INODE.unpack_from(inode_data, offset)
              for offset in range(0, len(inode_data) - INODE.size + 1, INODE.size)
              if INODE.unpack_from(inode_data, offset)[0] != 0}
    paths = {ROOT_INODE: "/"}
    pending = [DENTRY.unpack_from(entry_data, offset)
               for offset in range(0, len(entry_data) - DENTRY.size + 1, DENTRY.size)
               if DENTRY.unpack_from(entry_data, offset)[3] != 0]
    while pending:
        before = len(pending)
        remaining = []
        for entry in pending:
            if entry[0] not in paths:
                remaining.append(entry)
                continue
            paths[entry[1]] = paths[entry[0]].rstrip("/") + "/" + entry[4][:entry[3]].decode("ascii")
        pending = remaining
        if len(pending) == before:
            raise ValueError("orphan QFS v2 directory entry")
    return paths, inodes


def list_dir(image: bytes, path: str, start_lba: int = SUPERBLOCK_LBA) -> list[str]:
    paths, inodes = _paths_from_image(image, start_lba)
    inode_for = {value: key for key, value in paths.items()}
    if path != "/" and path.endswith("/"):
        path = path.rstrip("/")
    number = inode_for.get(path)
    if number is None or inodes[number][1] != TYPE_DIRECTORY:
        raise FileNotFoundError(path)
    names = []
    for child, full in paths.items():
        if child == number:
            continue
        parent = full.rsplit("/", 1)[0] or "/"
        if parent == path:
            names.append(full.rsplit("/", 1)[1])
    return names


def create_node(image: bytearray, path: str, kind: int = TYPE_DIRECTORY,
                start_lba: int = SUPERBLOCK_LBA) -> None:
    parent, name = _split_path(path)
    paths, inodes = _paths_from_image(bytes(image), start_lba)
    inode_for = {value: key for key, value in paths.items()}
    if parent not in inode_for or inodes[inode_for[parent]][1] != TYPE_DIRECTORY:
        raise FileNotFoundError(parent)
    if path.rstrip("/") in inode_for:
        raise FileExistsError(path)
    fields, inode_off, inode_data, entry_off, entry_data = _inode_table(image, start_lba)
    inode_count = len(inode_data) // INODE.size
    new_inode = 0
    for number in range(2, inode_count + 1):
        offset = (number - 1) * INODE.size
        current = INODE.unpack_from(inode_data, offset)[0]
        if current == 0:
            new_inode = number
            break
    if new_inode == 0:
        raise OSError("no free inode")
    slot = None
    for index in range(len(entry_data) // DENTRY.size):
        offset = index * DENTRY.size
        if DENTRY.unpack_from(entry_data, offset)[3] == 0:
            slot = offset
            break
    if slot is None:
        raise OSError("no free directory entry")
    encoded_inode = Inode(new_inode, kind, MODE_DIRECTORY if kind == TYPE_DIRECTORY else MODE_FILE).encode()
    encoded_entry = DirectoryEntry(inode_for[parent], new_inode, name, kind).encode()
    inode_pos = inode_off + (new_inode - 1) * INODE.size
    image[inode_pos:inode_pos + INODE.size] = encoded_inode
    image[entry_off + slot:entry_off + slot + DENTRY.size] = encoded_entry
    del fields


def mkdir(image: bytearray, path: str, start_lba: int = SUPERBLOCK_LBA) -> None:
    create_node(image, path, TYPE_DIRECTORY, start_lba)
