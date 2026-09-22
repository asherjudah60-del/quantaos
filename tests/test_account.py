#!/usr/bin/env python3
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from account import (FLAG_TEMPORARY, RECORD_SIZE, AccountRecord, choose_record,
                     make_record, password_matches, update_record)

salt = bytes(range(16))
initial = make_record("quanta", salt, generation=4, temporary=True)
assert len(initial.encode()) == RECORD_SIZE
assert password_matches(initial, "quanta")
assert not password_matches(initial, "wrong")
assert initial.flags & FLAG_TEMPORARY

updated = update_record(initial, "new-password", bytes(reversed(range(16))))
assert updated.generation == 5
assert updated.flags == 0
assert password_matches(updated, "new-password")
assert choose_record([b"bad", initial.encode(), updated.encode()]) == updated
assert choose_record([b"bad"]) is None

corrupt = bytearray(updated.encode())
corrupt[20] ^= 0x40
assert AccountRecord.decode(bytes(corrupt)) is None
print("account record checks passed")
