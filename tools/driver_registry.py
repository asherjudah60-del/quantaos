#!/usr/bin/env python3
"""Small host-side registry for validated userspace driver capsules."""
from __future__ import annotations

import json
import pathlib

from driver_capsule import DriverManifest, validate_capsule


class DriverRegistry:
    def __init__(self, path: pathlib.Path):
        self.path = path
        self.records: dict[str, dict[str, object]] = {}
        if path.exists():
            self.records = json.loads(path.read_text(encoding="utf-8"))

    def _save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(self.records, sort_keys=True) + "\n", encoding="utf-8")

    def add(self, capsule: pathlib.Path) -> DriverManifest:
        manifest = validate_capsule(capsule)
        if manifest.name in self.records:
            raise ValueError("driver is already registered")
        self.records[manifest.name] = {
            "version": manifest.version,
            "capabilities": list(manifest.capabilities),
            "capsule": str(capsule),
            "state": "stopped",
        }
        self._save()
        return manifest

    def remove(self, name: str) -> None:
        record = self.records.get(name)
        if record is None:
            raise ValueError("driver is not registered")
        if record.get("state") == "running":
            raise ValueError("running driver must be stopped first")
        del self.records[name]
        self._save()

    def set_state(self, name: str, state: str) -> None:
        if name not in self.records:
            raise ValueError("driver is not registered")
        if state not in {"stopped", "running", "failed"}:
            raise ValueError("invalid driver state")
        self.records[name]["state"] = state
        self._save()
