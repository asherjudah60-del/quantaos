#!/usr/bin/env python3
"""Validate QuantaOs userspace driver capsules."""
from __future__ import annotations

import pathlib
from dataclasses import dataclass

from elf64 import validate_user_elf_bytes

MANIFEST_NAME = "manifest"
BINARY_NAME = "driver.elf"
ALLOWED_CAPABILITIES = frozenset({"mmio", "irq", "dma", "ipc", "storage"})
REQUIRED_FIELDS = frozenset({"name", "version", "kind", "capabilities"})


@dataclass(frozen=True)
class DriverManifest:
    name: str
    version: str
    kind: str
    capabilities: tuple[str, ...]


def _parse_manifest(data: str) -> DriverManifest:
    values: dict[str, str] = {}
    for line in data.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError("driver manifest contains a malformed line")
        key, value = (part.strip() for part in line.split("=", 1))
        if key in values or not key:
            raise ValueError("driver manifest contains a duplicate or empty key")
        values[key] = value
    if not REQUIRED_FIELDS.issubset(values):
        raise ValueError("driver manifest is missing required fields")
    if values["kind"] != "userspace":
        raise ValueError("only userspace driver capsules are supported")
    name = values["name"]
    version = values["version"]
    capabilities = tuple(item.strip() for item in values["capabilities"].split(",") if item.strip())
    if not name or len(name) > 48 or not name.isascii() or "/" in name:
        raise ValueError("invalid driver name")
    if not version or len(version) > 32 or not version.isascii():
        raise ValueError("invalid driver version")
    if not capabilities or any(item not in ALLOWED_CAPABILITIES for item in capabilities):
        raise ValueError("driver requests an unsupported capability")
    if len(set(capabilities)) != len(capabilities):
        raise ValueError("driver requests a capability more than once")
    return DriverManifest(name, version, values["kind"], capabilities)


def validate_capsule(capsule: pathlib.Path) -> DriverManifest:
    """Validate an immutable capsule directory and its static user ELF."""
    if not capsule.is_dir():
        raise ValueError("driver capsule is not a directory")
    manifest_path = capsule / MANIFEST_NAME
    binary_path = capsule / BINARY_NAME
    if not manifest_path.is_file() or not binary_path.is_file():
        raise ValueError("driver capsule requires manifest and driver.elf")
    allowed = {MANIFEST_NAME, BINARY_NAME}
    actual = {item.name for item in capsule.iterdir()}
    if actual != allowed:
        raise ValueError("driver capsule contains unexpected files")
    manifest = _parse_manifest(manifest_path.read_text(encoding="ascii"))
    validate_user_elf_bytes(binary_path.read_bytes())
    return manifest
