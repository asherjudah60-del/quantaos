#!/usr/bin/env python3
"""Build a BIOS El Torito ISO whose boot image contains the complete kernel payload."""
from __future__ import annotations

import argparse
import math
import pathlib
import shutil
import subprocess

SECTOR = 512

parser = argparse.ArgumentParser()
parser.add_argument("--nasm", required=True)
parser.add_argument("--xorriso", required=True)
parser.add_argument("--stage1", type=pathlib.Path, required=True)
parser.add_argument("--stage2", type=pathlib.Path, required=True)
parser.add_argument("--boot-include", type=pathlib.Path, required=True)
parser.add_argument("--kernel", type=pathlib.Path, required=True)
parser.add_argument("--storage", type=pathlib.Path, required=True)
parser.add_argument("--efi", type=pathlib.Path, required=True)
parser.add_argument("--build", type=pathlib.Path, required=True)
parser.add_argument("--output", type=pathlib.Path, required=True)
args = parser.parse_args()

args.build.mkdir(parents=True, exist_ok=True)
layout = args.build / "layout.inc"
stage1 = args.build / "iso-stage1.bin"
stage2 = args.build / "iso-stage2.bin"
boot_image = args.build / "iso-boot.img"

kernel_bytes = args.kernel.read_bytes()
kernel_sectors = math.ceil(len(kernel_bytes) / SECTOR)
storage_bytes = args.storage.read_bytes()
storage_sectors = math.ceil(len(storage_bytes) / SECTOR)

def write_layout(stage2_sectors: int) -> None:
    layout.write_text(
        "%define ISO_BOOT 1\n"
        "%define STAGE2_LBA 1\n"
        f"%define STAGE2_SECTORS {stage2_sectors}\n"
        "%define KERNEL_LBA 0\n"
        f"%define KERNEL_SECTORS {kernel_sectors}\n"
        f"%define KERNEL_BYTES {len(kernel_bytes)}\n"
        f"%define STORAGE_SECTORS {storage_sectors}\n")

def assemble(source: pathlib.Path, output: pathlib.Path) -> None:
    subprocess.run([
        args.nasm, "-f", "bin", "-I", str(args.build) + "/", "-I",
        str(args.boot_include) + "/", "-I", str(args.stage2.parent / "include") + "/",
        str(source), "-o", str(output)
    ], check=True)

write_layout(1)
assemble(args.stage2, stage2)
stage2_sectors = math.ceil(stage2.stat().st_size / SECTOR)
write_layout(stage2_sectors)
assemble(args.stage2, stage2)
stage2_sectors = math.ceil(stage2.stat().st_size / SECTOR)
write_layout(stage2_sectors)
assemble(args.stage1, stage1)
if stage1.stat().st_size != SECTOR or stage1.read_bytes()[-2:] != b"\x55\xaa":
    raise SystemExit("ISO stage 1 is not a bootable 512-byte image")

def pad(data: bytes) -> bytes:
    return data + bytes((-len(data)) % SECTOR)

boot_image.write_bytes(pad(stage1.read_bytes()) + bytes(SECTOR) +
                      pad(stage2.read_bytes()) + pad(kernel_bytes) + pad(storage_bytes))
boot_sectors = boot_image.stat().st_size // SECTOR
iso_dir = args.build / "iso-root"
if iso_dir.exists():
    shutil.rmtree(iso_dir)
iso_dir.mkdir()
shutil.copy2(boot_image, iso_dir / "boot.img")
fallback_dir = iso_dir / "EFI" / "BOOT"
fallback_dir.mkdir(parents=True)
shutil.copy2(args.efi, fallback_dir / "BOOTX64.EFI")
efi_image = args.build / "efi.img"
efi_image.write_bytes(bytes(16 * 1024 * 1024))
subprocess.run(["mkfs.fat", "-F", "16", "-n", "QUANTAESP", str(efi_image)], check=True, stdout=subprocess.DEVNULL)
subprocess.run(["mmd", "-i", str(efi_image), "::EFI", "::EFI/BOOT"], check=True,
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
subprocess.run(["mcopy", "-i", str(efi_image), str(args.efi), "::EFI/BOOT/BOOTX64.EFI"],
               check=True, stdout=subprocess.DEVNULL)
shutil.copy2(efi_image, iso_dir / "efi.img")
args.output.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([
    args.xorriso, "-as", "mkisofs", "-iso-level", "3", "-V", "QUANTAOS",
    "-appended_part_as_gpt", "-append_partition", "3", "0x83", str(args.storage),
    "-b", "boot.img", "-no-emul-boot", "-boot-load-size", str(boot_sectors),
    "-efi-boot-part", str(efi_image), "--efi-boot", "efi.img",
    "-o", str(args.output), str(iso_dir)
], check=True, stdout=subprocess.DEVNULL)
print(f"created {args.output}: El Torito boot image={boot_sectors} sectors")
