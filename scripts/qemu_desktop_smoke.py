#!/usr/bin/env python3
"""Exercise VBE rendering and Alt+F1/F2 view switching through QMP."""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import selectors
import shutil
import socket
import subprocess
import sys
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument("image", type=pathlib.Path)
parser.add_argument("--screenshot", type=pathlib.Path)
args = parser.parse_args()
qemu = shutil.which("qemu-system-x86_64")
if qemu is None:
    sys.exit("qemu-system-x86_64 is required for qemu-desktop-smoke")

serial_buffer = bytearray()


def wait_serial(process: subprocess.Popen[bytes], selector: selectors.BaseSelector,
    marker: bytes, timeout: float = 10.0, start_index: int = 0) -> None:
    deadline = time.monotonic() + timeout
    while serial_buffer.find(marker, start_index) < 0:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RuntimeError(f"QEMU serial marker not observed: {marker.decode(errors='replace')}\n" +
                serial_buffer.decode(errors="replace"))
        if process.poll() is not None:
            raise RuntimeError("QEMU exited before serial marker: " +
                serial_buffer.decode(errors="replace"))
        if not selector.select(remaining):
            continue
        chunk = os.read(process.stdout.fileno(), 4096)
        if chunk:
            serial_buffer.extend(chunk)


def qmp_command(stream, command: str, arguments: dict | None = None) -> dict:
    request: dict[str, object] = {"execute": command}
    if arguments is not None:
        request["arguments"] = arguments
    stream.write((json.dumps(request) + "\r\n").encode())
    while True:
        response = json.loads(stream.readline())
        if "event" in response:
            continue
        if "error" in response:
            raise RuntimeError(f"QMP {command} failed: {response['error']}")
        return response


def send_key(stream, *keys: str) -> None:
    qmp_command(stream, "send-key", {
        "keys": [{"type": "qcode", "data": key} for key in keys],
        "hold-time": 100,
    })
    time.sleep(0.12)


def send_text(stream, text: str) -> None:
    for character in text:
        if character == "\n":
            send_key(stream, "ret")
        else:
            send_key(stream, character)


def send_mouse(stream, axis_x: int, axis_y: int, button_down: bool | None = None) -> None:
    events: list[dict[str, object]] = []
    if axis_x:
        events.append({"type": "rel", "data": {"axis": "x", "value": axis_x}})
    if axis_y:
        events.append({"type": "rel", "data": {"axis": "y", "value": axis_y}})
    if button_down is not None:
        events.append({"type": "btn", "data": {"button": "left", "down": button_down}})
    qmp_command(stream, "input-send-event", {"events": events})


def screenshot(stream, destination: pathlib.Path) -> bytes:
    qmp_command(stream, "screendump", {"filename": str(destination)})
    image = destination.read_bytes()
    header_end = image.find(b"\n255\n")
    if not image.startswith(b"P6\n") or header_end < 0:
        raise RuntimeError("QEMU screendump is not a raw RGB PPM")
    return image[header_end + len(b"\n255\n"):]


with tempfile.TemporaryDirectory(prefix="quanta-desktop-") as temporary:
    scratch = pathlib.Path(temporary)
    qmp_path = scratch / "qmp.sock"
    serial_selector = selectors.DefaultSelector()
    command = [
        qemu,
        "-drive", f"format=raw,file={args.image},if=ide,index=0,media=disk,snapshot=on",
        "-vga", "std",
        "-display", "gtk",
        "-serial", "stdio",
        "-qmp", f"unix:{qmp_path},server=on,wait=off",
        "-monitor", "none",
        "-no-reboot",
    ]
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, bufsize=0)
    serial_selector.register(process.stdout, selectors.EVENT_READ)
    try:
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
        qmp_stream = qmp_socket.makefile("rwb", buffering=0)
        greeting = json.loads(qmp_stream.readline())
        if "QMP" not in greeting:
            raise RuntimeError("QEMU did not send a QMP greeting")
        qmp_command(qmp_stream, "qmp_capabilities")

        wait_serial(process, serial_selector, b"QUANTA_LOGIN_SCREEN_READY")
        if args.screenshot is not None:
            login = screenshot(qmp_stream, scratch / "login.ppm")
            args.screenshot.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(scratch / "login.ppm",
                args.screenshot.with_name(args.screenshot.stem + "-login.ppm"))
        send_text(qmp_stream, "quanta")
        send_key(qmp_stream, "tab")
        send_text(qmp_stream, "quanta")
        send_key(qmp_stream, "ret")
        wait_serial(process, serial_selector, b"QUANTA_LOGIN_READY")
        wait_serial(process, serial_selector, b"QUANTA_FRAMEBUFFER_READY")
        wait_serial(process, serial_selector, b"QUANTA_DESKTOP_READY")
        wait_serial(process, serial_selector, b"QUANTA_BGA_DOUBLE_BUFFER_READY")
        wait_serial(process, serial_selector, b"QUANTA_BGA_PAGE_FLIP")
        marker_start = len(serial_buffer)
        send_key(qmp_stream, "alt", "f2")
        wait_serial(process, serial_selector, b"QUANTA_VIEW_WORKSPACE", start_index=marker_start)
        time.sleep(0.1)
        workspace = screenshot(qmp_stream, scratch / "workspace.ppm")
        if args.screenshot is not None:
            args.screenshot.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(scratch / "workspace.ppm", args.screenshot)

        marker_start = len(serial_buffer)
        send_key(qmp_stream, "down")
        send_key(qmp_stream, "ret")
        wait_serial(process, serial_selector, b"QUANTA_APP_FILES_FOCUSED",
            start_index=marker_start)
        time.sleep(0.1)
        files = screenshot(qmp_stream, scratch / "files-window.ppm")
        if files == workspace:
            raise RuntimeError("Files launcher did not open its Home window")
        if args.screenshot is not None:
            shutil.copyfile(scratch / "files-window.ppm",
                args.screenshot.with_name(args.screenshot.stem + "-files.ppm"))

        marker_start = len(serial_buffer)
        send_key(qmp_stream, "alt", "f1")
        wait_serial(process, serial_selector, b"QUANTA_VIEW_TERMINAL", start_index=marker_start)
        wait_serial(process, serial_selector, b"QUANTA_APP_TERMINAL_FOCUSED",
            start_index=marker_start)
        wait_serial(process, serial_selector, b"prime:home>quanta> ")
        time.sleep(0.1)
        terminal = screenshot(qmp_stream, scratch / "terminal-window.ppm")
        if terminal == workspace:
            shutil.copyfile(scratch / "workspace.ppm", "/tmp/quanta-bga-workspace.ppm")
            shutil.copyfile(scratch / "terminal-window.ppm", "/tmp/quanta-bga-terminal.ppm")
            raise RuntimeError("Alt+F1 did not open the terminal window")

        screenshot(qmp_stream, scratch / "pointer.ppm")
        if args.screenshot is not None:
            shutil.copyfile(scratch / "terminal-window.ppm",
                args.screenshot.with_name(args.screenshot.stem + "-terminal.ppm"))
            shutil.copyfile(scratch / "pointer.ppm",
                args.screenshot.with_name(args.screenshot.stem + "-pointer.ppm"))

        marker_start = len(serial_buffer)
        send_key(qmp_stream, "alt", "f2")
        wait_serial(process, serial_selector, b"QUANTA_VIEW_WORKSPACE", start_index=marker_start)
        marker_start = len(serial_buffer)
        send_key(qmp_stream, "alt", "f1")
        wait_serial(process, serial_selector, b"QUANTA_VIEW_TERMINAL", start_index=marker_start)
        time.sleep(0.1)

        marker_start = len(serial_buffer)
        send_text(qmp_stream, "pwd\n")
        wait_serial(process, serial_selector, b"prime:home>quanta\n", start_index=marker_start)
        marker_start = len(serial_buffer)
        send_text(qmp_stream, "shutdown\n")
        wait_serial(process, serial_selector, b"QUANTA_SHUTDOWN", start_index=marker_start)
        print("QEMU desktop smoke passed: VBE desktop, graphical login, Files focus, windowed terminal, persistent shell")
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)