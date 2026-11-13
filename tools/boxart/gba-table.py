#!/usr/bin/env python3
"""Generate GBA serial fallback names from libretro-database's No-Intro DAT.
Input: metadat/no-intro/Nintendo - Game Boy Advance.dat (CC BY-SA 4.0).
See docs/gba-covers.md for attribution and reproduction.
"""
import re,sys
from pathlib import Path
rows=set()
for block in Path(sys.argv[1]).read_text().split('\ngame (')[1:]:
    name=re.search(r'\n\s*name "([^"]+)"',block)
    code=re.search(r'\n\s*serial "([A-Z0-9]{4})"',block)
    if name and code:rows.add((code[1],name[1]))
Path(sys.argv[2]).write_text('# No-Intro / libretro-database contributors; CC BY-SA 4.0; adapted serial/name table.\n# Source and changes: docs/gba-covers.md\n'+''.join(f'{code}\t{name}\n' for code,name in sorted(rows)))
