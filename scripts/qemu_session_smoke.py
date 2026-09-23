#!/usr/bin/env python3
"""Exercise login, path, mkdir, date, and command recovery over serial."""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("image")
args = parser.parse_args()
qemu = shutil.which("qemu-system-x86_64")
if qemu is None:
    sys.exit("qemu-system-x86_64 is required for qemu-session-smoke")

script = "\n".join([
    "quanta",
    "quanta",
    "ls",
    "cd /",
    "ls",
    "cd C:/home",
    "pwd",
    "cd /home/quanta",
    "mkdir john",
    "cd john",
    "ls",
    "cd ..",
    "date",
    "df /",
    "cat",
    "ls",
    "shutdown",
    "",
])

with tempfile.TemporaryDirectory() as scratch:
    image = Path(scratch) / "quantaos.img"
    shutil.copyfile(args.image, image)
    command = [
        qemu, "-drive", f"format=raw,file={image},if=ide,index=0,media=disk",
        "-serial", "stdio", "-display", "none", "-monitor", "none", "-no-reboot",
    ]
    try:
        run = subprocess.run(command, text=True, input=script, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=20)
        output = run.stdout
    except subprocess.TimeoutExpired as error:
        output = error.stdout or ""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")

required = [
    "QUANTA_LOGIN_READY",
    "QUANTA_MKDIR_READY",
    "C:/home",
    "C:/home/quanta/john",
    "UTC",
    "cat: usage: cat FILE...",
    "command not found",
    "readme",
    "QUANTA_SHUTDOWN",
]
for marker in required:
    if marker not in output:
        sys.stderr.write(output)
        sys.exit(f"QEMU session smoke missing: {marker}")
if "QUANTA_USER_FAULT_TERMINATED" in output:
    sys.stderr.write(output)
    sys.exit("QEMU session smoke observed a user fault")
print("QEMU session smoke passed")
