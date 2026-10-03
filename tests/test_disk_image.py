#!/usr/bin/env python3
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
image = ROOT / "build" / "quantaos.img"
expected = 10 * 1024 * 1024 * 1024
assert image.stat().st_size == expected
assert image.stat().st_blocks * 512 < expected // 100
print("10 GiB disk image checks passed")