#!/usr/bin/env python3
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
tool = ROOT / "tools" / "install_image.py"
with tempfile.TemporaryDirectory() as directory:
    folder = pathlib.Path(directory)
    image = folder / "image.img"
    image.write_bytes(b"x" * 1024)
    target = folder / "target.img"
    bad = subprocess.run([sys.executable, str(tool), "--image", str(image), "--target", str(target),
                          "--confirm", "no"], text=True, capture_output=True)
    assert bad.returncode != 0
    assert not target.exists()

    image = folder / "sparse-installer.img"
    with image.open("wb") as output:
        output.seek(8 * 1024 * 1024 * 1024 - 1)
        output.write(b"\0")
        output.seek(0)
        output.write(b"QUANTA-BOOT")
        output.seek(1024 * 1024)
        output.write(b"QUANTA-ROOT")
    result = subprocess.run([sys.executable, str(tool), "--image", str(image),
                             "--target", str(target), "--confirm", "ERASE-QUANTAOS"],
                            text=True, capture_output=True)
    assert result.returncode == 0, result.stderr
    assert target.stat().st_size == image.stat().st_size
    with target.open("rb") as output:
        assert output.read(11) == b"QUANTA-BOOT"
        output.seek(1024 * 1024)
        assert output.read(11) == b"QUANTA-ROOT"
print("installer writer safety checks passed")
