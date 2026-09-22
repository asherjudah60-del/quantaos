#!/usr/bin/env python3
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from qfs2 import SECTOR, SUPERBLOCK_LBA, parse_volume

image = ROOT / "build" / "quantaos.img"
# The image is deliberately sparse and 10 GiB; only its initialized prefix is
# relevant to filesystem-format validation.
with image.open("rb") as source:
    prefix = source.read((SUPERBLOCK_LBA + 4096) * SECTOR)
files = parse_volume(prefix)
assert files["/etc/motd"] == b"Welcome to QuantaOs.\n"
assert files["/bin/echo"][:4] == b"\x7fELF"
print("image QFS checks passed")
