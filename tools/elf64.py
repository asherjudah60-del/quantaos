"""Minimal ELF64 validation shared by image construction and host tests."""
from __future__ import annotations

import struct

KERNEL_PHYSICAL_START = 0x00200000
KERNEL_PHYSICAL_END = 0x00300000

def validate_kernel_elf_bytes(data: bytes) -> None:
    if len(data) < 64 or data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("kernel must be a little-endian ELF64 image")
    machine, = struct.unpack_from("<H", data, 18)
    phoff, = struct.unpack_from("<Q", data, 32)
    phentsize, phnum = struct.unpack_from("<HH", data, 54)
    if machine != 0x3e or phentsize != 56 or phoff + phentsize * phnum > len(data):
        raise ValueError("kernel ELF program-header table is invalid")
    for index in range(phnum):
        offset = phoff + index * phentsize
        kind, = struct.unpack_from("<I", data, offset)
        if kind != 1:
            continue
        file_offset, _virtual, physical, filesz, memsz = struct.unpack_from("<QQQQQ", data, offset + 8)
        if filesz > memsz or file_offset + filesz > len(data):
            raise ValueError("kernel ELF load segment is out of bounds")
        if physical < KERNEL_PHYSICAL_START or physical + memsz > KERNEL_PHYSICAL_END:
            raise ValueError("kernel ELF exceeds the bootstrap 1 MiB high-half mapping")

def validate_user_elf_bytes(data: bytes) -> None:
    if len(data) < 64 or data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("user program must be a little-endian ELF64 image")
    machine, = struct.unpack_from("<H", data, 18)
    entry, = struct.unpack_from("<Q", data, 24)
    phoff, = struct.unpack_from("<Q", data, 32)
    phentsize, phnum = struct.unpack_from("<HH", data, 54)
    if machine != 0x3e or phentsize != 56 or phnum == 0 or phoff + phentsize * phnum > len(data):
        raise ValueError("user ELF program-header table is invalid")
    loadable = False
    for index in range(phnum):
        offset = phoff + index * phentsize
        kind, flags = struct.unpack_from("<II", data, offset)
        if kind != 1:
            continue
        file_offset, virtual, _physical, filesz, memsz = struct.unpack_from("<QQQQQ", data, offset + 8)
        if filesz > memsz or file_offset + filesz > len(data) or virtual < 0x400000:
            raise ValueError("user ELF load segment is invalid")
        if entry >= virtual and entry < virtual + memsz:
            loadable = True
    if not loadable:
        raise ValueError("user ELF entry point is not loadable")
