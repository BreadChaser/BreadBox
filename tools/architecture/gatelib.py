"""Shared helpers of the architecture gates: the repo root and counts kept in small plain files.

A stored count may only go down. `--update` rewrites the file with the current value and refuses
to raise it; the first run (no file yet) creates it.
"""
import argparse
import sys
from pathlib import Path

DATA = 'tools/architecture'   # where the stored counts and exception lists live (under --root)


def args(description):
    ap = argparse.ArgumentParser(description=description)
    ap.add_argument('--root', default=str(Path(__file__).resolve().parents[2]),
                    help='repo root to check (the host tests point it at a fixture)')
    ap.add_argument('--update', action='store_true',
                    help='store the current value; refused when it is higher than the stored one')
    ap.add_argument('--list', action='store_true', help='print every item that is counted')
    return ap


def data_path(root, name):
    return Path(root) / DATA / name


def read_count(path):
    return int(path.read_text().split()[0]) if path.exists() else None


def ratchet(root, name, current, update, label, details=()):
    """Compare `current` with the number stored in DATA/name. Returns the process exit code."""
    path = data_path(root, name)
    stored = read_count(path)
    if update:
        if stored is not None and current > stored:
            print(f'{label}: {current} is higher than the stored {stored}; --update only lowers it', file=sys.stderr)
            return 1
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f'{current}\n')
        print(f'{label}: stored {current} in {DATA}/{name}')
        return 0
    if stored is None:
        print(f'{label}: {DATA}/{name} is missing; run with --update to create it', file=sys.stderr)
        return 1
    if current > stored:
        for line in details:
            print('  ' + line)
        print(f'{label}: FAIL {current} is above the stored {stored}', file=sys.stderr)
        return 1
    note = f' (stored {stored}; run with --update to lock in the lower count)' if current < stored else ''
    print(f'{label}: {current}, limit {stored}: ok{note}')
    return 0
