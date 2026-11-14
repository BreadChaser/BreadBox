#!/usr/bin/env python3
"""Invalidate only objects that embed reproducible compiler timestamps."""
import os
from pathlib import Path
import re

# Keep this inventory synchronized with the source-macro regression in build_contract.py.
TIMESTAMP_OBJECTS = {
    'core/frontend/main.c': 'core/frontend/main.o',
    'systems/ps1/clip.c': 'build/3ds/build-breadbox/clip.o',
}


def prepare(root, epoch):
    if not re.fullmatch(r'[0-9]+', epoch):
        raise ValueError('SOURCE_DATE_EPOCH must be a nonnegative integer')
    stamp = root / 'build/systems/.source-date-epoch'
    if stamp.is_file() and stamp.read_text() == epoch + '\n':
        return
    # Normal make dependencies rebuild the affected archives and final links.
    # Unrelated PS1, GBA, NES and frontend objects remain reusable.
    for obj in TIMESTAMP_OBJECTS.values():
        (root / obj).unlink(missing_ok=True)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(epoch + '\n')


def dry_run(flags):
    # GNU make puts single-letter flags first, without a dash ("n -j3"); a first
    # word with a dash is an option such as -Oline, never the letter group.
    first = flags.split()[0] if flags.split() else ''
    return not first.startswith('-') and any(flag in first for flag in 'nqt')


if __name__ == '__main__' and not dry_run(os.environ.get('MAKEFLAGS', '')):
    prepare(Path(__file__).resolve().parents[2], os.environ['SOURCE_DATE_EPOCH'])
