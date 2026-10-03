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
assert 'equal(name, "shutdown")' in text, 'shutdown must remain a separate command'
assert 'equal(line, "exit") || equal(line, "shutdown")' not in text, 'exit must not power off'
assert 'split_command' in text, 'commands must parse a name and arguments'
assert '"prime"' in text and '"live"' in text and 'path_resolve' in text, 'named drive paths must resolve to the selected root'
assert 'C:/' not in text and '/dev/disk0' not in text, 'UNIX and drive-letter mount syntax must not be user-visible'
assert 'HOME=/home/' not in text and 'SHELL=/bin/' not in text and '"/dev/console' not in text, 'shell environment must use named-drive paths'
print('shell utility checks passed')
