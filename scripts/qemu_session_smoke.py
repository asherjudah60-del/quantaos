#!/usr/bin/env python3
"""Exercise login, path, mkdir, date, and command recovery over serial."""
from __future__ import annotations

import argparse
import errno
import json
import os
import selectors
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("image")
parser.add_argument("--cdrom", action="store_true")
args = parser.parse_args()
qemu = shutil.which("qemu-system-x86_64")
if qemu is None:
    sys.exit("qemu-system-x86_64 is required for qemu-session-smoke")

COPY_CHUNK_SIZE = 1024 * 1024


def copy_sparse_image(source: Path, destination: Path) -> None:
    size = source.stat().st_size
    unsupported = {errno.EINVAL, errno.ENOTSUP, errno.EOPNOTSUPP}
    with source.open("rb") as input_file, destination.open("wb") as output_file:
        output_file.truncate(size)
        if hasattr(os, "SEEK_DATA") and hasattr(os, "SEEK_HOLE"):
            offset = 0
            try:
                while offset < size:
                    try:
                        data = os.lseek(input_file.fileno(), offset, os.SEEK_DATA)
                    except OSError as error:
                        if error.errno == errno.ENXIO:
                            break
                        raise
                    hole = os.lseek(input_file.fileno(), data, os.SEEK_HOLE)
                    if hole <= data:
                        raise OSError(errno.EINVAL, "invalid sparse extent")
                    input_file.seek(data)
                    output_file.seek(data)
                    remaining = min(hole, size) - data
                    while remaining:
                        chunk = input_file.read(min(COPY_CHUNK_SIZE, remaining))
                        if not chunk:
                            raise OSError(errno.EIO, "short read while copying image")
                        output_file.write(chunk)
                        remaining -= len(chunk)
                    offset = hole
                return
            except OSError as error:
                if error.errno not in unsupported:
                    raise
                output_file.seek(0)
                output_file.truncate(0)
                output_file.truncate(size)
                input_file.seek(0)

        offset = 0
        while offset < size:
            chunk = input_file.read(min(COPY_CHUNK_SIZE, size - offset))
            if not chunk:
                raise OSError(errno.EIO, "short read while copying image")
            if any(chunk):
                output_file.seek(offset)
                output_file.write(chunk)
            offset += len(chunk)

script = "\n".join([
    "lsblk",
    "ls",
    "ls Documents",
    "cd " + ("live:" if args.cdrom else "prime:"),
    "ls",
    "cd " + ("live:home" if args.cdrom else "prime:home"),
    "pwd",
    "cd " + ("live:home>quanta" if args.cdrom else "prime:home>quanta"),
    "cd " + ("prime:home" if args.cdrom else "live:home"),
    "pwd",
        "mkdir qsession_smoke_dir",
        "cd qsession_smoke_dir",
    "ls",
    "cd ..",
    "pwd",
    "cd /home/quanta",
    "cd C:/home",
    "pwd",
    "date",
    "df " + ("live:" if args.cdrom else "prime:"),
    "cat",
    "ls",
    "notacommand",
    "shutdown",
    "",
])

with tempfile.TemporaryDirectory() as scratch:
    scratch_path = Path(scratch)
    image = scratch_path / "quantaos.img"
    qmp_path = scratch_path / "qmp.sock"
    copy_sparse_image(Path(args.image), image)
    media = ["-cdrom", str(image)] if args.cdrom else [
        "-drive", f"format=raw,file={image},if=ide,index=0,media=disk"
    ]
    command = [qemu, *media, "-serial", "stdio", "-display", "none",
        "-qmp", f"unix:{qmp_path},server=on,wait=off", "-monitor", "none", "-no-reboot"]
    output = bytearray()
    try:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, bufsize=0)
        selector = selectors.DefaultSelector()
        selector.register(process.stdout, selectors.EVENT_READ)
        qmp_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        deadline = time.monotonic() + 10.0
        while True:
            try:
                qmp_socket.connect(str(qmp_path))
                break
            except FileNotFoundError:
                if time.monotonic() >= deadline:
                    raise RuntimeError("QEMU did not create its QMP socket")
                time.sleep(0.02)
        qmp = qmp_socket.makefile("rwb", buffering=0)
        if "QMP" not in json.loads(qmp.readline()):
            raise RuntimeError("QEMU did not send a QMP greeting")

        def qmp_execute(operation: str, arguments: dict | None = None) -> None:
            request: dict[str, object] = {"execute": operation}
            if arguments is not None:
                request["arguments"] = arguments
            qmp.write((json.dumps(request) + "\r\n").encode())
            while True:
                response = json.loads(qmp.readline())
                if "event" in response:
                    continue
                if "error" in response:
                    raise RuntimeError(f"QMP {operation} failed: {response['error']}")
                return

        def send_key(key: str) -> None:
            qmp_execute("send-key", {"keys": [{"type": "qcode", "data": key}], "hold-time": 100})
            time.sleep(0.12)

        qmp_execute("qmp_capabilities")

        def wait_marker(marker: bytes, timeout: float = 60.0) -> None:
            end = time.monotonic() + timeout
            while marker not in output:
                if process.poll() is not None:
                    raise RuntimeError("QEMU exited before marker: " + output.decode(errors="replace"))
                if time.monotonic() >= end:
                    raise RuntimeError("QEMU session timed out before marker: " + marker.decode())
                if selector.select(0.1):
                    chunk = os.read(process.stdout.fileno(), 4096)
                    if chunk:
                        output.extend(chunk)

        wait_marker(b"QUANTA_LOGIN_SCREEN_READY")
        for key in "quanta":
            send_key(key)
        send_key("tab")
        for key in "quanta":
            send_key(key)
        send_key("ret")
        wait_marker(b"QUANTA_LOGIN_READY")
        wait_marker(b"QUANTA_DESKTOP_READY")
        qmp_execute("send-key", {"keys": [
            {"type": "qcode", "data": "alt"},
            {"type": "qcode", "data": "f1"}], "hold-time": 100})
        wait_marker(b"QUANTA_VIEW_TERMINAL")
        process.stdin.write(script.encode())
        process.stdin.flush()
        wait_marker(b"QUANTA_SHUTDOWN")
        process.wait(timeout=5)
    except (subprocess.TimeoutExpired, RuntimeError, OSError) as error:
        if "process" in locals() and process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
        sys.stderr.write(output.decode(errors="replace"))
        sys.exit(str(error))
    output = output.decode(errors="replace")

required = [
    "QUANTA_LOGIN_READY",
    "live:home>quanta> " if args.cdrom else "prime:home>quanta> ",
    "live  211456 bytes  live read-only" if args.cdrom else "disk0  10737418240 bytes  ready",
    "live:home>quanta> " if args.cdrom else "QUANTA_MKDIR_READY",
    "live:home" if args.cdrom else "prime:home",
    "mkdir: failed" if args.cdrom else "prime:home>quanta>qsession_smoke_dir",
    "cd: no such directory",
    "live:home>quanta\n" if args.cdrom else "prime:home>quanta\n",
    "prime:home>quanta> " if not args.cdrom else "cd: no such directory",
    *([] if args.cdrom else ["cd: no such directory"]),
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
