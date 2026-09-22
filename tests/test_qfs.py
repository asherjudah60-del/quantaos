#!/usr/bin/env python3
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from qfs import build, parse

image = bytes(258 * 512) + build([("/bin/echo", b"ELF"), ("/etc/motd", b"hello\n")])
files = parse(image)
assert files["/bin/echo"] == b"ELF"
assert files["/etc/motd"] == b"hello\n"

corrupt = bytearray(image)
corrupt[258 * 512 + 8] ^= 1
try:
    parse(bytes(corrupt))
except ValueError:
    pass
else:
    raise AssertionError("corrupt QFS superblock was accepted")
print("QFS checks passed")