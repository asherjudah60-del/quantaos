#!/usr/bin/env python3
"""Regression checks for the shell utility surface."""
from __future__ import annotations

import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
SHELL = ROOT / 'userspace' / 'session.c'

text = SHELL.read_text(encoding='utf-8')

assert 'passwd' in text, 'passwd command is required for the session flow'
assert 'drivers' in text, 'driver status command is required for the session flow'
assert 'printf' in text, 'printf command is required for the session flow'
assert 'lsblk' in text, 'lsblk command is required for storage inspection'
assert 'help' in text, 'help command is required for the session flow'
assert 'logged_in_user' in text and 'cwd' in text, 'prompt must include session identity and working directory'
assert 'QUANTA_SYSCALL_SESSION_LOGOUT' in text, 'exit must use the logout syscall'
assert 'equal(line, "shutdown")' in text, 'shutdown must remain a separate command'
assert 'equal(line, "exit") || equal(line, "shutdown")' not in text, 'exit must not power off'
print('shell utility checks passed')
