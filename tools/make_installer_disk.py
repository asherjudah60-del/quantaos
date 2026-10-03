#!/usr/bin/env python3
"""Create a sparse, whole-disk QuantaOS installation image.

The image is deliberately an installation *target*, not the installer USB.  It
is also used as the byte-for-byte layout contract for the future in-OS writer.
"""
from __future__ import annotations

import argparse
import hashlib
import math
import pathlib
import shutil
import struct
import subprocess
import sys
import uuid
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from qfs2 import SECTOR, parse_volume, build_volume

MIB = 1024 * 1024
DISK_SIZE = 8 * 1024 * 1024 * 1024
BIOS_LBA = 34
BIOS_SECTORS = 2 * MIB // SECTOR
ESP_LBA = BIOS_LBA + BIOS_SECTORS
ESP_SECTORS = 64 * MIB // SECTOR
ROOT_LBA = ESP_LBA + ESP_SECTORS
GPT_ENTRIES_LBA = 2
GPT_ENTRIES_SECTORS = 32
ESP_GUID = uuid.UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b")
BIOS_GUID = uuid.UUID("21686148-6449-6e6f-744e-656564454649")
QFS_GUID = uuid.UUID("0fc63daf-8483-4772-8e79-3d69d8477de4")

parser = argparse.ArgumentParser()
parser.add_argument("--nasm", required=True)
parser.add_argument("--stage1", type=pathlib.Path, required=True)
parser.add_argument("--stage2", type=pathlib.Path, required=True)
parser.add_argument("--boot-include", type=pathlib.Path, required=True)
parser.add_argument("--kernel", type=pathlib.Path, required=True)
parser.add_argument("--efi", type=pathlib.Path, required=True)
parser.add_argument("--storage", type=pathlib.Path, required=True)
parser.add_argument("--build", type=pathlib.Path, required=True)
parser.add_argument("--output", type=pathlib.Path, required=True)
args = parser.parse_args()


def crc(data: bytes) -> int:
    return zlib.crc32(data) & 0xffffffff


def guid(value: uuid.UUID) -> bytes:
    return value.bytes_le


def partition(kind: uuid.UUID, name: str, first: int, last: int) -> bytes:
    label = name.encode("utf-16-le")
    identity = uuid.uuid5(uuid.NAMESPACE_URL, f"quantaos:{name}")
    return struct.pack("<16s16sQQQ72s", guid(kind), guid(identity), first, last,
                       0, label + bytes(72 - len(label)))


def header(current: int, backup: int, first: int, last: int, entries_lba: int,
           entries_crc: int) -> bytes:
    value = bytearray(SECTOR)
    struct.pack_into("<8sIIIIQQQQ16sQIII", value, 0, b"EFI PART", 0x10000, 92,
                     0, 0, current, backup, first, last, bytes(16), entries_lba,
                     128, 128, entries_crc)
    struct.pack_into("<I", value, 16, crc(value[:92]))
    return bytes(value)


def assemble(source: pathlib.Path, output: pathlib.Path, root_lba: int) -> None:
    subprocess.run([args.nasm, "-f", "bin", "-I", str(args.build) + "/", "-I",
                    str(args.boot_include) + "/", "-I", str(args.stage2.parent / "include") + "/",
                    f"-DROOT_LBA={root_lba}", str(source), "-o", str(output)], check=True)


args.build.mkdir(parents=True, exist_ok=True)
layout = args.build / "layout.inc"
stage2 = args.build / "installer-stage2.bin"
stage1 = args.build / "installer-stage1.bin"
kernel = args.kernel.read_bytes()
layout.write_text(f"%define STAGE2_LBA {BIOS_LBA}\n%define STAGE2_SECTORS 1\n"
                  f"%define KERNEL_LBA {BIOS_LBA + 1}\n%define KERNEL_SECTORS {math.ceil(len(kernel) / SECTOR)}\n"
                  f"%define KERNEL_BYTES {len(kernel)}\n")
for _ in range(3):
    assemble(args.stage2, stage2, ROOT_LBA)
    sectors = math.ceil(stage2.stat().st_size / SECTOR)
    layout.write_text(f"%define STAGE2_LBA {BIOS_LBA}\n%define STAGE2_SECTORS {sectors}\n"
                      f"%define KERNEL_LBA {BIOS_LBA + sectors}\n%define KERNEL_SECTORS {math.ceil(len(kernel) / SECTOR)}\n"
                      f"%define KERNEL_BYTES {len(kernel)}\n")
assemble(args.stage1, stage1, ROOT_LBA)
if stage1.stat().st_size != SECTOR or stage1.read_bytes()[-2:] != b"\x55\xaa":
    raise SystemExit("installer stage 1 is not bootable")
if BIOS_LBA + sectors + math.ceil(len(kernel) / SECTOR) > ESP_LBA:
    raise SystemExit("kernel exceeds BIOS boot partition")

esp = args.build / "installer-esp.img"
esp.write_bytes(bytes(ESP_SECTORS * SECTOR))
subprocess.run(["mkfs.fat", "-F", "32", "-n", "QUANTAESP", str(esp)], check=True,
               stdout=subprocess.DEVNULL)
subprocess.run(["mmd", "-i", str(esp), "::EFI", "::EFI/BOOT"], check=True,
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
subprocess.run(["mcopy", "-i", str(esp), str(args.efi), "::EFI/BOOT/BOOTX64.EFI"], check=True,
               stdout=subprocess.DEVNULL)

source = args.storage.read_bytes()
files = list(parse_volume(source).items())
disk_sectors = DISK_SIZE // SECTOR
root = build_volume(files, total_sectors=disk_sectors - ROOT_LBA, start_lba=ROOT_LBA)
last_lba = disk_sectors - 1
backup_entries_lba = last_lba - GPT_ENTRIES_SECTORS
entries = b"".join((
    partition(BIOS_GUID, "BIOS boot", BIOS_LBA, ESP_LBA - 1),
    partition(ESP_GUID, "EFI System", ESP_LBA, ROOT_LBA - 1),
    partition(QFS_GUID, "Quanta root", ROOT_LBA, backup_entries_lba - 1),
)) + bytes(128 * 128 - 3 * 128)
entries_crc = crc(entries)
mbr = bytearray(stage1.read_bytes())
mbr[446:462] = bytes((0, 0, 2, 0)) + b"\xee\xff\xff\xff" + struct.pack("<II", 1, 0xffffffff)

args.output.parent.mkdir(parents=True, exist_ok=True)
with args.output.open("wb") as output:
    output.seek(DISK_SIZE - 1)
    output.write(b"\0")
    output.seek(0); output.write(mbr)
    output.seek(SECTOR); output.write(header(1, last_lba, 34, backup_entries_lba - 1,
                                               GPT_ENTRIES_LBA, entries_crc))
    output.seek(GPT_ENTRIES_LBA * SECTOR); output.write(entries)
    output.seek(BIOS_LBA * SECTOR)
    stage2_bytes = stage2.read_bytes()
    output.write(stage2_bytes + bytes((-len(stage2_bytes)) % SECTOR))
    output.write(kernel + bytes((-len(kernel)) % SECTOR))
    output.seek(ESP_LBA * SECTOR); output.write(esp.read_bytes())
    output.seek(ROOT_LBA * SECTOR); output.write(root)
    output.seek(256 * SECTOR); output.write(source[256 * SECTOR:257 * SECTOR])
    output.seek(backup_entries_lba * SECTOR); output.write(entries)
    output.seek(last_lba * SECTOR); output.write(header(last_lba, 1, 34,
                                                         backup_entries_lba - 1,
                                                         backup_entries_lba, entries_crc))
print(f"created {args.output}: root LBA={ROOT_LBA}, SHA256={hashlib.sha256(root).hexdigest()}")
