#!/usr/bin/env python3
"""Source-text ratchet: tests should run the code, not read its source text.

Counts the Python tests under tests/ that read a production source file (app, systems, platform,
include) and then slice or search the text (.index(, .count(, .find(, a [a:b] slice). The count
is stored in text-ratchet.count and may only go down: replace such a test with one that builds
and calls the real code, then run with --update.

  python3 tools/architecture/test-text-ratchet.py            check
  python3 tools/architecture/test-text-ratchet.py --list     print the tests that are counted
  python3 tools/architecture/test-text-ratchet.py --update   store a lower count (refuses to raise it)

It is a text heuristic on purpose (no parsing); the tools/architecture guards are not tests/.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gatelib

# One edit here when the top-level folders move (src/).
TEST_DIR = 'tests'
SOURCE_DIRS = ('app', 'systems', 'platform', 'include')
SKIP = ('tests/arch_gates/', 'tests/trunk_gate/')    # these build fixture trees, not production reads
COUNT_FILE = 'text-ratchet.count'

READS = re.compile(r'read_text\(|\bopen\(|\.read\(\)|read_bytes\(')
NAMES_SOURCE = re.compile(r'[\'"/](?:%s)[\'"/]' % '|'.join(SOURCE_DIRS))
SEARCHES = re.compile(r'\.(?:index|rindex|count|find|rfind)\(|\[[A-Za-z_][\w.()+\- ]*:[^\]\n]*\]|\[[^\]\n:]*:[A-Za-z_][\w.()+\- ]*\]')


def counted(root):
    root = Path(root)
    found = []
    for p in sorted((root / TEST_DIR).rglob('*.py')):
        rel = p.relative_to(root).as_posix()
        if rel.startswith(SKIP):
            continue
        text = p.read_text(errors='replace')
        if READS.search(text) and NAMES_SOURCE.search(text) and SEARCHES.search(text):
            found.append(rel)
    return found


def main():
    a = gatelib.args(__doc__.split('\n')[0]).parse_args()
    found = counted(a.root)
    if a.list:
        print('\n'.join(found))
    return gatelib.ratchet(a.root, COUNT_FILE, len(found), a.update, 'text-ratchet', () if a.list else found)


if __name__ == '__main__':
    sys.exit(main())
