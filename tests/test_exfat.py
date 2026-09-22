#!/usr/bin/env python3
import pathlib
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
import sys
sys.path.insert(0, str(ROOT / "tools"))
from exfat import format_image, validate_boot_region

with tempfile.TemporaryDirectory() as directory:
    image = pathlib.Path(directory) / "volume.exfat"
    format_image(image, 16 * 1024 * 1024, "QUANTA")
    validate_boot_region(image)
    corrupt = bytearray(image.read_bytes()[:512])
    corrupt[3] = ord("X")
    image.write_bytes(bytes(corrupt) + image.read_bytes()[512:])
    try:
        validate_boot_region(image)
    except ValueError:
        pass
    else:
        raise AssertionError("corrupt exFAT boot region was accepted")
print("exFAT formatting checks passed")