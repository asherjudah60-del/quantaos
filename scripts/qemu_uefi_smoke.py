#!/usr/bin/env python3
"""Boot the live ISO through OVMF and require kernel/login markers."""
from __future__ import annotations

import pathlib
import shutil
import subprocess
import tempfile
import time

root = pathlib.Path(__file__).resolve().parents[1]
iso = root / "build" / "quantaos.iso"
qemu = shutil.which("qemu-system-x86_64")
code = pathlib.Path("/usr/share/OVMF/OVMF_CODE_4M.fd")
vars_template = pathlib.Path("/usr/share/OVMF/OVMF_VARS_4M.fd")
if qemu is None or not code.exists() or not vars_template.exists():
    raise SystemExit("QEMU and OVMF firmware are required for UEFI smoke")
with tempfile.TemporaryDirectory() as directory:
    variables = pathlib.Path(directory) / "OVMF_VARS.fd"
    shutil.copy2(vars_template, variables)
    process = subprocess.Popen([
        qemu, "-machine", "q35", "-drive", f"if=pflash,format=raw,readonly=on,file={code}",
        "-drive", f"if=pflash,format=raw,file={variables}", "-cdrom", str(iso),
        "-boot", "order=d,menu=on",
        "-display", "none", "-monitor", "none", "-serial", "stdio", "-no-reboot"
    ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(2)
    try:
        output, _ = process.communicate(input="quanta\nquanta\n", timeout=8)
    except subprocess.TimeoutExpired as error:
        process.kill()
        output, _ = process.communicate()
        print(output)
        raise SystemExit("UEFI QEMU timed out") from error
for marker in ("QUANTA_KERNEL_READY", "QUANTA_LOGIN_READY"):
    if marker not in output:
        print(output)
        raise SystemExit(f"UEFI smoke marker not observed: {marker}")
print("UEFI QEMU smoke passed: QUANTA_KERNEL_READY, QUANTA_LOGIN_READY")
