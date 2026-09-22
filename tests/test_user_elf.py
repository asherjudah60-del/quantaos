#!/usr/bin/env python3
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from elf64 import validate_user_elf_bytes

subprocess.run(["make", "user-binaries"], cwd=ROOT, check=True)
for binary in sorted((ROOT / "build" / "userspace" / "bin").glob("*.elf")):
	validate_user_elf_bytes(binary.read_bytes())
print("user ELF checks passed")