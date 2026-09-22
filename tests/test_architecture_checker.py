#!/usr/bin/env python3
"""Regression tests for architectural boundary enforcement."""
from __future__ import annotations

import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CHECKER = ROOT / "scripts/check_architecture.py"

class ArchitectureCheckerTests(unittest.TestCase):
    def check(self, relative: str, content: str) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            path = root / relative
            path.parent.mkdir(parents=True)
            path.write_text(content)
            return subprocess.run(["python3", str(CHECKER), "--root", str(root)],
                cwd=ROOT, text=True, capture_output=True, check=False)

    def test_userspace_hardware_token_is_rejected(self) -> None:
        result = self.check("userspace/probe.c", "void f(void) { __asm__(\"hlt\"); }\n")
        self.assertNotEqual(result.returncode, 0)

    def test_vfs_to_memory_include_is_rejected(self) -> None:
        result = self.check("kernel/vfs/probe.c",
            "#include <kernel/mm/include/quanta/memory.h>\n")
        self.assertNotEqual(result.returncode, 0)

if __name__ == "__main__":
    unittest.main()
