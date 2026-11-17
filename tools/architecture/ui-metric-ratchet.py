#!/usr/bin/env python3
"""UI_METRIC ratchet: screens use named style tokens, not numbered metrics.

Counts every `UI_METRIC_<number>` outside include/breadbox/ui_style.h (the file that defines them).
The count is stored in ui-metric.count and may only go down.

  python3 tools/architecture/ui-metric-ratchet.py            check
  python3 tools/architecture/ui-metric-ratchet.py --list     count per file
  python3 tools/architecture/ui-metric-ratchet.py --update   store a lower count (refuses to raise it)
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gatelib

# One edit here when the top-level folders move (src/).
SCAN_DIRS = ('app', 'systems', 'platform', 'include')
DEFINED_IN = 'include/breadbox/ui_style.h'
EXTENSIONS = ('.c', '.cc', '.cpp', '.h', '.inc')
COUNT_FILE = 'ui-metric.count'
USE = re.compile(r'UI_METRIC_[0-9]+')


def uses(root):
    root = Path(root)
    per_file = {}
    for d in SCAN_DIRS:
        for p in sorted((root / d).rglob('*')) if (root / d).is_dir() else ():
            rel = p.relative_to(root).as_posix()
            if p.is_file() and p.suffix in EXTENSIONS and rel != DEFINED_IN:
                n = len(USE.findall(p.read_text(errors='replace')))
                if n:
                    per_file[rel] = n
    return per_file


def main():
    a = gatelib.args(__doc__.split('\n')[0]).parse_args()
    per_file = uses(a.root)
    lines = [f'{n:5d}  {rel}' for rel, n in sorted(per_file.items(), key=lambda kv: -kv[1])]
    if a.list:
        print('\n'.join(lines))
    return gatelib.ratchet(a.root, COUNT_FILE, sum(per_file.values()), a.update, 'ui-metric-ratchet',
                           lines[:15] if not a.list else ())


if __name__ == '__main__':
    sys.exit(main())
