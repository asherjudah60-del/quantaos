#!/usr/bin/env python3
"""Verify that a linked ELF exposes only names declared by its owner."""
from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("elf", type=pathlib.Path)
parser.add_argument("allowlist", type=pathlib.Path)
args = parser.parse_args()
nm = shutil.which("llvm-nm") or shutil.which("nm")
if nm is None:
    sys.exit("no nm tool found")
allowed = {line.strip() for line in args.allowlist.read_text().splitlines() if line.strip()}
result = subprocess.run([nm, "--defined-only", "--extern-only", "--format=posix", str(args.elf)],
    check=True, text=True, capture_output=True)
exports = {line.split()[0] for line in result.stdout.splitlines() if line}
unexpected = sorted(exports - allowed)
if unexpected:
    sys.exit("unexpected exported symbols: " + ", ".join(unexpected))
print("export check passed")
