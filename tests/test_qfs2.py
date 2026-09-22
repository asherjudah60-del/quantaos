#!/usr/bin/env python3
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from qfs2 import (DENTRY, INODE, ROOT_INODE, TYPE_DIRECTORY, TYPE_FILE,
                  DirectoryEntry, Inode, build_metadata, validate_metadata)

inodes = [
    Inode(ROOT_INODE, TYPE_DIRECTORY, 0o755),
    Inode(2, TYPE_DIRECTORY, 0o755),
    Inode(3, TYPE_FILE, 0o644, size=6, first_sector=300, sectors=1),
]
entries = [
    DirectoryEntry(ROOT_INODE, 2, "home", TYPE_DIRECTORY),
    DirectoryEntry(2, 3, "readme", TYPE_FILE),
]
metadata = build_metadata(inodes, entries, total_sectors=1024)
validate_metadata(metadata)
assert len(Inode(1, TYPE_DIRECTORY, 0o755).encode()) == INODE.size
assert len(entries[0].encode()) == DENTRY.size
corrupt = bytearray(metadata)
corrupt[40] ^= 1
try:
    validate_metadata(bytes(corrupt))
except ValueError:
    pass
else:
    raise AssertionError("corrupt QFS v2 metadata was accepted")
try:
    DirectoryEntry(1, 2, "bad/name", TYPE_FILE).encode()
except ValueError:
    pass
else:
    raise AssertionError("invalid directory entry was accepted")
print("QFS v2 metadata checks passed")
