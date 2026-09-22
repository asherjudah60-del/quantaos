#!/usr/bin/env python3
"""Create the fixed-sector BIOS image and its NASM layout contract."""
from __future__ import annotations

import argparse
import math
import os
import pathlib
import subprocess
import sys
import hashlib
from elf64 import validate_kernel_elf_bytes

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from account import AccountRecord, make_record
from qfs2 import SUPERBLOCK_LBA, build_volume

SECTOR = 512
DISK_SIZE = 10 * 1024 * 1024 * 1024
ACCOUNT_LBA = 256
ACCOUNT_SLOTS = 2

parser = argparse.ArgumentParser()
parser.add_argument("--nasm", required=True)
parser.add_argument("--stage1", type=pathlib.Path, required=True)
parser.add_argument("--stage2", type=pathlib.Path, required=True)
parser.add_argument("--boot-include", type=pathlib.Path, required=True)
parser.add_argument("--kernel", type=pathlib.Path, required=True)
parser.add_argument("--build", type=pathlib.Path, required=True)
parser.add_argument("--output", type=pathlib.Path, required=True)
parser.add_argument("--initial-password", default=os.environ.get("INITIAL_PASSWORD", "quanta"))
args = parser.parse_args()

try:
    validate_kernel_elf_bytes(args.kernel.read_bytes())
except ValueError as error:
    raise SystemExit(str(error)) from error

args.build.mkdir(parents=True, exist_ok=True)
layout = args.build / "layout.inc"
stage2 = args.build / "stage2.bin"
kernel_sectors = math.ceil(args.kernel.stat().st_size / SECTOR)
kernel_bytes = args.kernel.stat().st_size

def write_layout(stage2_sectors: int) -> None:
    layout.write_text(
        f"%define STAGE2_SECTORS {stage2_sectors}\n"
        f"%define STAGE2_LBA 1\n"
        f"%define KERNEL_LBA {1 + stage2_sectors}\n"
        f"%define KERNEL_SECTORS {kernel_sectors}\n"
        f"%define KERNEL_BYTES {kernel_bytes}\n")

write_layout(1)
for _ in range(3):
    subprocess.run([args.nasm, "-f", "bin", "-I", str(args.build) + "/", "-I",
        str(args.boot_include) + "/", "-I", str(args.stage2.parent / "include") + "/",
        str(args.stage2), "-o", str(stage2)], check=True)
    sectors = math.ceil(stage2.stat().st_size / SECTOR)
    previous = layout.read_text()
    write_layout(sectors)
    if layout.read_text() == previous:
        break
else:
    raise SystemExit("stage-2 layout did not converge")
if stage2.stat().st_size > 0x18000:
    raise SystemExit("stage 2 overlaps the kernel scratch buffer")

stage1 = args.build / "stage1.bin"
subprocess.run([args.nasm, "-f", "bin", "-I", str(args.build) + "/", str(args.stage1),
    "-o", str(stage1)], check=True)
if stage1.stat().st_size != SECTOR or stage1.read_bytes()[-2:] != b"\x55\xaa":
    raise SystemExit("stage 1 is not a bootable 512-byte MBR")

def pad(data: bytes) -> bytes:
    return data + b"\0" * ((-len(data)) % SECTOR)

prefix = stage1.read_bytes() + pad(stage2.read_bytes()) + pad(args.kernel.read_bytes())
if len(prefix) > ACCOUNT_LBA * SECTOR:
    raise SystemExit("kernel overlaps reserved account sectors")
account = make_record(args.initial_password, hashlib.sha256(b"quanta-account-salt").digest()[:16])
image = prefix + bytes(ACCOUNT_LBA * SECTOR - len(prefix))
image += account.encode() + bytes(SECTOR)
qfs_files = [
    ("/etc/motd", b"Welcome to QuantaOs.\n"),
    ("/etc/os-release", b"NAME=QuantaOs\nARCH=x86_64\n"),
    ("/home/quanta/readme", b"This is your single-user QuantaOs session.\n"),
]
for utility in sorted(pathlib.Path("build/userspace/bin").glob("*.elf")):
    qfs_files.append(("/bin/" + utility.stem, utility.read_bytes()))
if not any(path == "/bin/echo" for path, _ in qfs_files):
    raise SystemExit("at least the echo utility is required for QFS packaging")
directories = [
    "/boot/.keep", "/dev/.keep", "/lib/.keep", "/mnt/.keep", "/opt/.keep",
    "/proc/.keep", "/root/.keep", "/sbin/.keep", "/tmp/.keep", "/usr/bin/.keep",
    "/usr/include/.keep", "/usr/lib/.keep", "/var/log/.keep",
]
qfs_files.extend((path, b"") for path in directories)
image += bytes(SUPERBLOCK_LBA * SECTOR - len(image)) + build_volume(
    qfs_files, total_sectors=DISK_SIZE // SECTOR - SUPERBLOCK_LBA,
    start_lba=SUPERBLOCK_LBA)
args.output.parent.mkdir(parents=True, exist_ok=True)
with args.output.open("wb") as output:
    output.write(image)
    output.truncate(DISK_SIZE)
print(f"created {args.output}: stage2={sectors} sectors kernel={kernel_sectors} sectors")
