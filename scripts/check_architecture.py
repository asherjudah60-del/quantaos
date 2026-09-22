#!/usr/bin/env python3
"""Mechanical separation-of-concerns checks for QuantaOs."""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".c", ".h", ".asm", ".S", ".s"}
ASSEMBLY_ALLOWED = ("bootloader/", "kernel/arch/x86_64/", "userspace/")
FORBIDDEN_USER_TOKENS = ("__asm__", "asm(", "inb", "outb", "read_cr", "write_cr", "hlt", "cli", "lgdt", "lidt", "mov cr")
INCLUDE = re.compile(r'^\s*#\s*include\s*[<\"]([^>\"]+)[>\"]', re.M)

ZONE_PREFIXES = {
    "bootloader/": "bootloader",
    "kernel/arch/x86_64/": "arch",
    "kernel/core/": "core",
    "kernel/mm/": "mm",
    "kernel/vfs/": "vfs",
    "userspace/": "userspace",
    "tools/": "tools",
}
ALLOWED_EDGES = {
    "bootloader": {"bootloader", "abi"},
    "arch": {"arch", "mm", "abi"},
    "core": {"core", "mm", "vfs", "arch", "abi"},
    "mm": {"mm", "arch", "abi"},
    "vfs": {"vfs", "abi"},
    "userspace": {"userspace", "abi"},
    "tools": {"tools"},
}

def zone_for(path: str) -> str | None:
    for prefix, zone in ZONE_PREFIXES.items():
        if path.startswith(prefix):
            return zone
    return None

def include_zone(included: str) -> str | None:
    normalized = included.replace("\\", "/")
    private_headers = {
        "quanta/arch.h": "arch",
        "quanta/memory.h": "mm",
        "quanta/task.h": "core",
        "quanta/ipc_router.h": "core",
        "quanta/vfs.h": "vfs",
    }
    if normalized in private_headers:
        return private_headers[normalized]
    if normalized.startswith("abi/") or normalized.startswith("quanta/"):
        return "abi"
    return zone_for(normalized)

def fail(path: pathlib.Path, message: str) -> None:
    print(f"architecture error: {path.relative_to(ROOT)}: {message}")
    raise SystemExit(1)

def main(root: pathlib.Path) -> None:
    global ROOT
    ROOT = root.resolve()
    for path in ROOT.rglob("*"):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        relative = path.relative_to(ROOT).as_posix()
        if path.suffix in {".asm", ".S", ".s"} and not relative.startswith(ASSEMBLY_ALLOWED):
            fail(path, "assembly is restricted to bootloader or kernel/arch/x86_64")
        text = path.read_text(encoding="utf-8")
        owner = zone_for(relative)
        if owner:
            for included in INCLUDE.findall(text):
                dependency = include_zone(included)
                if dependency and dependency not in ALLOWED_EDGES[owner]:
                    fail(path, f"{owner} may not include {dependency} ({included})")
        if relative.startswith(("userspace/", "tools/")):
            if any(token in text for token in FORBIDDEN_USER_TOKENS):
                fail(path, "userspace/tools may not perform privileged hardware operations")
            for included in INCLUDE.findall(text):
                if included.startswith("kernel/") or included.startswith("bootloader/"):
                    fail(path, "userspace/tools may not include privileged implementation headers")
        if relative.startswith("bootloader/"):
            for included in INCLUDE.findall(text):
                if included.startswith("kernel/"):
                    fail(path, "bootloader may not include kernel implementation headers")
        if relative.startswith("kernel/mm/") and any(token in text for token in ("quanta_ipc_", "quanta_task_")):
            fail(path, "memory manager may not implement IPC or task policy")
        if relative.startswith("kernel/vfs/") and "quanta_task_" in text:
            fail(path, "VFS may not implement task policy")
    print("architecture checks passed")

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, default=ROOT)
    main(parser.parse_args().root)
