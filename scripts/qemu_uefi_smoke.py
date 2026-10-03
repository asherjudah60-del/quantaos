#!/usr/bin/env python3
"""Boot the live ISO through OVMF and require kernel/login markers."""
from __future__ import annotations

import pathlib
import json
import os
import selectors
import shutil
import socket
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
    qmp_path = pathlib.Path(directory) / "qmp.sock"
    process = subprocess.Popen([
        qemu, "-machine", "q35", "-drive", f"if=pflash,format=raw,readonly=on,file={code}",
        "-drive", f"if=pflash,format=raw,file={variables}", "-cdrom", str(iso),
        "-boot", "order=d,menu=on",
        "-display", "none", "-monitor", "none", "-serial", "stdio",
        "-qmp", f"unix:{qmp_path},server=on,wait=off", "-no-reboot"
    ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    output = bytearray()

    def qmp_command(stream, operation: str, arguments: dict | None = None) -> None:
        request: dict[str, object] = {"execute": operation}
        if arguments is not None:
            request["arguments"] = arguments
        stream.write((json.dumps(request) + "\r\n").encode())
        while True:
            response = json.loads(stream.readline())
            if "event" in response:
                continue
            if "error" in response:
                raise RuntimeError(f"QMP {operation} failed: {response['error']}")
            return

    def send_key(stream, key: str) -> None:
        qmp_command(stream, "send-key", {"keys": [{"type": "qcode", "data": key}], "hold-time": 100})
        time.sleep(0.12)

    def wait_marker(marker: bytes, timeout: float = 30.0) -> None:
        deadline = time.monotonic() + timeout
        while marker not in output:
            if process.poll() is not None:
                raise RuntimeError("QEMU exited before marker: " + output.decode(errors="replace"))
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError("QEMU timed out before marker: " + marker.decode() +
                    "\n" + output.decode(errors="replace"))
            if selector.select(remaining):
                chunk = os.read(process.stdout.fileno(), 4096)
                if chunk:
                    output.extend(chunk)

    try:
        qmp_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        qmp_deadline = time.monotonic() + 10.0
        while True:
            try:
                qmp_socket.connect(str(qmp_path))
                break
            except FileNotFoundError:
                if time.monotonic() >= qmp_deadline:
                    raise RuntimeError("QEMU did not create its QMP socket")
                time.sleep(0.02)
        qmp = qmp_socket.makefile("rwb", buffering=0)
        if "QMP" not in json.loads(qmp.readline()):
            raise RuntimeError("QEMU did not send a QMP greeting")
        qmp_command(qmp, "qmp_capabilities")
        wait_marker(b"QUANTA_UEFI_GOP_READY")
        wait_marker(b"QUANTA_FRAMEBUFFER_READY")
        wait_marker(b"QUANTA_KERNEL_READY")
        wait_marker(b"QUANTA_USER_SESSION_STARTING")
        wait_marker(b"QUANTA_LOGIN_SCREEN_READY")
        for key in "quanta":
            send_key(qmp, key)
        send_key(qmp, "tab")
        for key in "quanta":
            send_key(qmp, key)
        send_key(qmp, "ret")
        wait_marker(b"QUANTA_LOGIN_READY")
        wait_marker(b"QUANTA_DESKTOP_READY")
        for key in ("down", "ret"):
            send_key(qmp, key)
        wait_marker(b"QUANTA_APP_FILES_FOCUSED")
        print("UEFI QEMU smoke passed: GOP framebuffer, login, desktop ready")
    except (RuntimeError, OSError) as error:
        print(output.decode(errors="replace"))
        raise SystemExit(str(error)) from error
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
