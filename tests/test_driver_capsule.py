#!/usr/bin/env python3
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from driver_capsule import validate_capsule
from driver_registry import DriverRegistry

binary = ROOT / "build/userspace/bin/echo.elf"
if not binary.exists():
    raise SystemExit("build/userspace/bin/echo.elf is required; run make user-binaries")

with tempfile.TemporaryDirectory() as directory:
    capsule = pathlib.Path(directory) / "console"
    capsule.mkdir()
    (capsule / "manifest").write_text(
        "name=console\nversion=1.0\nkind=userspace\ncapabilities=ipc,storage\n",
        encoding="ascii")
    shutil.copy2(binary, capsule / "driver.elf")
    manifest = validate_capsule(capsule)
    assert manifest.name == "console"
    assert manifest.capabilities == ("ipc", "storage")

    registry = DriverRegistry(pathlib.Path(directory) / "drivers.json")
    registry.add(capsule)
    registry.set_state("console", "running")
    try:
        registry.remove("console")
    except ValueError:
        pass
    else:
        raise AssertionError("running driver was removed")
    registry.set_state("console", "stopped")
    registry.remove("console")

    (capsule / "extra").write_text("unexpected", encoding="ascii")
    try:
        validate_capsule(capsule)
    except ValueError:
        pass
    else:
        raise AssertionError("capsule with unexpected files was accepted")

print("driver capsule checks passed")
