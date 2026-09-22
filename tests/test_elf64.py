#!/usr/bin/env python3
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from elf64 import validate_kernel_elf_bytes

def valid_image() -> bytearray:
    image = bytearray(256)
    image[0:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<H", image, 18, 0x3e)
    struct.pack_into("<Q", image, 32, 64)
    struct.pack_into("<HH", image, 54, 56, 1)
    struct.pack_into("<I", image, 64, 1)
    struct.pack_into("<QQQQQ", image, 72, 128, 0xffffffff80100000, 0x00200000, 4, 8)
    return image

class Elf64Tests(unittest.TestCase):
    def test_accepts_bootstrap_kernel_segment(self) -> None:
        validate_kernel_elf_bytes(valid_image())

    def test_rejects_bad_magic(self) -> None:
        image = valid_image()
        image[:4] = b"nope"
        with self.assertRaises(ValueError):
            validate_kernel_elf_bytes(image)

    def test_rejects_segment_outside_bootstrap_mapping(self) -> None:
        image = valid_image()
        struct.pack_into("<Q", image, 88, 0x00300000)
        with self.assertRaises(ValueError):
            validate_kernel_elf_bytes(image)

if __name__ == "__main__":
    unittest.main()
