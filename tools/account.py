#!/usr/bin/env python3
"""Account record encoding and password verification for QACCOUNT slots."""
from __future__ import annotations

import hashlib
import struct
import zlib
from dataclasses import dataclass

MAGIC = b"QACCOUNT"
VERSION = 1
RECORD_SIZE = 512
SALT_SIZE = 16
VERIFIER_SIZE = 32
FLAG_TEMPORARY = 1
_HEADER = struct.Struct("<8sHHQ16s32sI")

@dataclass(frozen=True)
class AccountRecord:
    generation: int
    salt: bytes
    verifier: bytes
    flags: int = FLAG_TEMPORARY

    def encode(self) -> bytes:
        if len(self.salt) != SALT_SIZE or len(self.verifier) != VERIFIER_SIZE:
            raise ValueError("invalid account field length")
        header = _HEADER.pack(MAGIC, VERSION, self.flags, self.generation,
                              self.salt, self.verifier, 0)
        checksum = zlib.crc32(header[:-4]) & 0xffffffff
        record = header[:-4] + struct.pack("<I", checksum)
        return record + bytes(RECORD_SIZE - len(record))

    @staticmethod
    def decode(data: bytes) -> "AccountRecord | None":
        if len(data) != RECORD_SIZE:
            return None
        magic, version, flags, generation, salt, verifier, checksum = _HEADER.unpack(data[:_HEADER.size])
        if magic != MAGIC or version != VERSION:
            return None
        if checksum != (zlib.crc32(data[:_HEADER.size - 4]) & 0xffffffff):
            return None
        return AccountRecord(generation, salt, verifier, flags)

def verifier(password: str, salt: bytes) -> bytes:
    return hashlib.sha256(salt + password.encode("utf-8")).digest()

def make_record(password: str, salt: bytes, generation: int = 1,
                temporary: bool = True) -> AccountRecord:
    flags = FLAG_TEMPORARY if temporary else 0
    return AccountRecord(generation, salt, verifier(password, salt), flags)

def choose_record(records: list[bytes]) -> AccountRecord | None:
    valid = [record for raw in records if (record := AccountRecord.decode(raw))]
    return max(valid, key=lambda record: record.generation, default=None)

def password_matches(record: AccountRecord, password: str) -> bool:
    return verifier(password, record.salt) == record.verifier

def update_record(current: AccountRecord, password: str, salt: bytes) -> AccountRecord:
    return make_record(password, salt, current.generation + 1, temporary=False)
