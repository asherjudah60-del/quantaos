#!/usr/bin/env python3
"""Create the compact, immutable QFS v2 storage payload used by the live ISO."""
from __future__ import annotations

import argparse
import hashlib
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from qfs2 import SUPERBLOCK_LBA, build_volume
from account import make_record

SECTOR = 512
parser = argparse.ArgumentParser()
parser.add_argument("--utilities", type=pathlib.Path, required=True)
parser.add_argument("--output", type=pathlib.Path, required=True)
args = parser.parse_args()

files = [
    ("/etc/motd", b"Welcome to QuantaOs live session.\n"),
    ("/etc/os-release", b"NAME=QuantaOs\nARCH=x86_64\nLIVE=1\n"),
    ("/home/quanta/readme", b"This is a read-only QuantaOs live session.\n"),
]
for utility in sorted(args.utilities.glob("*.elf")):
    files.append(("/bin/" + utility.stem, utility.read_bytes()))
for path in ("/boot/.keep", "/dev/.keep", "/lib/.keep", "/mnt/.keep", "/opt/.keep",
             "/proc/.keep", "/root/.keep", "/sbin/.keep", "/tmp/.keep", "/usr/bin/.keep",
             "/usr/include/.keep", "/usr/lib/.keep", "/var/log/.keep"):
    files.append((path, b""))
account = make_record("quanta", hashlib.sha256(b"quanta-account-salt").digest()[:16]).encode()
prefix = bytearray(SUPERBLOCK_LBA * SECTOR)
prefix[256 * SECTOR:257 * SECTOR] = account
payload = bytes(prefix) + build_volume(files, total_sectors=4096)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_bytes(payload)
print(f"created {args.output}: {len(payload) // SECTOR} initialized sectors")
