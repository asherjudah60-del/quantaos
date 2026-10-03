#!/usr/bin/env python3
"""Safely write a prepared QuantaOS disk image to one explicit target.

This is the host-side reference transaction for the on-device installer.  It
never guesses a target, refuses the source itself, checks capacity before the
first write, and copies sparse source extents without expanding them in RAM.
"""
from __future__ import annotations

import argparse
import os
import pathlib
import stat

CONFIRMATION = "ERASE-QUANTAOS"
CHUNK = 1024 * 1024

parser = argparse.ArgumentParser()
parser.add_argument("--image", type=pathlib.Path, required=True)
parser.add_argument("--target", type=pathlib.Path, required=True)
parser.add_argument("--confirm", required=True, metavar=CONFIRMATION)
args = parser.parse_args()

if args.confirm != CONFIRMATION:
    raise SystemExit(f"refusing write: pass --confirm {CONFIRMATION}")
source = args.image.resolve(strict=True)
target = args.target.resolve(strict=False)
if source == target:
    raise SystemExit("refusing write: image and target are the same file")
source_size = source.stat().st_size
if source_size < 8 * 1024 * 1024 * 1024:
    raise SystemExit("refusing write: image is smaller than the installer contract")

try:
    target_stat = target.stat()
except FileNotFoundError:
    target_stat = None
if target_stat is not None and stat.S_ISREG(target_stat.st_mode) and target_stat.st_size not in (0, source_size):
    raise SystemExit("refusing write: regular-file target has an unexpected size")
if target_stat is not None and stat.S_ISBLK(target_stat.st_mode):
    size = os.lseek(os.open(target, os.O_RDONLY), 0, os.SEEK_END)
    if size < source_size:
        raise SystemExit("refusing write: block target is too small")

with source.open("rb") as input_file, target.open("w+b" if target_stat is None else "r+b") as output_file:
    output_file.truncate(source_size)
    offset = 0
    while offset < source_size:
        try:
            data_offset = os.lseek(input_file.fileno(), offset, os.SEEK_DATA)
        except OSError:
            break
        if data_offset >= source_size:
            break
        try:
            hole_offset = os.lseek(input_file.fileno(), data_offset, os.SEEK_HOLE)
        except OSError:
            hole_offset = source_size
        input_file.seek(data_offset)
        output_file.seek(data_offset)
        remaining = min(hole_offset, source_size) - data_offset
        while remaining:
            data = input_file.read(min(CHUNK, remaining))
            if not data:
                raise SystemExit("source changed during installation")
            output_file.write(data)
            remaining -= len(data)
        offset = hole_offset
    output_file.flush()
    os.fsync(output_file.fileno())
print(f"installed {source} to {target}; reboot from the target disk")
