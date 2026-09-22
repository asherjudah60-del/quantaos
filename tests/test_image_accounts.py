#!/usr/bin/env python3
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from account import AccountRecord, RECORD_SIZE

ACCOUNT_LBA = 256
image = ROOT / "build" / "quantaos.img"
if image.exists():
    data = image.read_bytes()
    first = data[ACCOUNT_LBA * 512:ACCOUNT_LBA * 512 + RECORD_SIZE]
    second = data[(ACCOUNT_LBA + 1) * 512:(ACCOUNT_LBA + 2) * 512]
    assert AccountRecord.decode(first) is not None
    assert len(second) == RECORD_SIZE
print("image account layout checks passed")