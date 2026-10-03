#!/usr/bin/env python3
"""Boot the BIOS image in headless Bochs and require BGA frame presentation."""
from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument("image", type=pathlib.Path)
args = parser.parse_args()
bochs = shutil.which("bochs") or shutil.which("bochs-bin")
if bochs is None:
    raise SystemExit("Bochs 2.8+ is required; install Bochs and rerun make bochs-desktop-smoke")

bios_candidates = (
    pathlib.Path("/usr/share/bochs/BIOS-bochs-latest"),
    pathlib.Path("/usr/share/seabios/bios-256k.bin"),
)
vga_candidates = (
    pathlib.Path("/usr/share/vgabios/vgabios.bin"),
    pathlib.Path("/usr/share/seabios/vgabios-bochs-display.bin"),
)
bios = next((path for path in bios_candidates if path.exists()), None)
vga = next((path for path in vga_candidates if path.exists()), None)
if bios is None or vga is None:
    raise SystemExit(
        "Bochs firmware ROMs are missing. Install Bochs BIOS and VGA BIOS ROM data "
        "(for Debian/Ubuntu, install the bochsbios package) and rerun make bochs-desktop-smoke."
    )

required = (
    b"QUANTA_BGA_DOUBLE_BUFFER_READY",
    b"QUANTA_BGA_PAGE_FLIP",
    b"QUANTA_FRAMEBUFFER_READY",
)
with tempfile.TemporaryDirectory(prefix="quanta-bochs-") as temporary:
    scratch = pathlib.Path(temporary)
    serial = scratch / "serial.log"
    log = scratch / "bochs.log"
    config = scratch / "bochsrc"
    config.write_text("\n".join((
        "config_interface: textconfig",
        "display_library: sdl2",
        f"romimage: file={bios}, address=0xc0000",
        f"vgaromimage: file={vga}",
        "megs: 64",
        "ata0: enabled=1, ioaddr1=0x1f0, ioaddr2=0x3f0, irq=14",
        f'ata0-master: type=disk, mode=flat, path="{args.image.resolve()}"',
        "boot: disk",
        f'com1: enabled=1, mode=file, dev="{serial}"',
        f'log: "{log}"',
        "panic: action=report",
        "error: action=report",
        "info: action=ignore",
        "debug: action=ignore",
        "mouse: enabled=1",
        "",
    )))
    environment = os.environ.copy()
    environment["SDL_VIDEODRIVER"] = "dummy"
    process = subprocess.Popen([bochs, "-q", "-f", str(config)],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        env=environment)
    if process.stdin is not None:
        process.stdin.write(b"c\n")
        process.stdin.flush()
    deadline = time.monotonic() + 35.0
    try:
        while time.monotonic() < deadline:
            if process.poll() is not None:
                details = process.stdout.read().decode(errors="replace") if process.stdout else ""
                output = serial.read_bytes() if serial.exists() else b""
                raise RuntimeError("Bochs exited before BGA presentation:\n" +
                    details + output.decode(errors="replace"))
            output = serial.read_bytes() if serial.exists() else b""
            if all(marker in output for marker in required):
                print("Bochs desktop smoke passed: BGA double buffer, page flip, framebuffer ready")
                break
            time.sleep(0.05)
        else:
            output = serial.read_bytes() if serial.exists() else b""
            details = log.read_text(errors="replace") if log.exists() else ""
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            console = process.stdout.read().decode(errors="replace") if process.stdout else ""
            raise RuntimeError("Bochs timed out before BGA frame presentation:\n" +
                output.decode(errors="replace") + "\n" + details[-4000:] + "\n" + console[-4000:])
    except RuntimeError as error:
        raise SystemExit(str(error)) from error
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
