#!/usr/bin/env python3
"""Boot an image headlessly and require a declared serial progress marker."""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import time

parser = argparse.ArgumentParser()
parser.add_argument("image")
parser.add_argument("--marker", action="append", required=True)
parser.add_argument("--cdrom", action="store_true")
args = parser.parse_args()
qemu = shutil.which("qemu-system-x86_64")
if qemu is None:
    sys.exit("qemu-system-x86_64 is required for qemu-smoke")
try:
    media = ["-cdrom", args.image] if args.cdrom else ["-drive", f"format=raw,file={args.image}"]
    command = [qemu, *media, "-boot", "order=d", "-display", "none", "-monitor", "none",
        "-serial", "stdio", "-no-reboot"]
    if args.cdrom:
        process = subprocess.Popen(command, text=True, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        time.sleep(1)
        output, _ = process.communicate(input="quanta\nquanta\n", timeout=5)
    else:
        run = subprocess.run(command, text=True, input="quanta\nquanta\n",
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=5)
        output = run.stdout
except subprocess.TimeoutExpired as error:
    output = error.stdout or ""
    if isinstance(output, bytes):
        output = output.decode(errors="replace")
position = 0
for marker in args.marker:
    found = output.find(marker, position)
    if found < 0:
        sys.stderr.write(output)
        sys.exit(f"QEMU smoke marker not observed in order: {marker}")
    position = found + len(marker)
if "QUANTA_BOOT_ERROR_" in output:
    sys.stderr.write(output)
    sys.exit("QEMU emitted a bootloader error marker")
print("QEMU smoke passed: " + ", ".join(args.marker))
