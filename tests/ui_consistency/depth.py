#!/usr/bin/env python3
"""A panel never covers its own text: citro2d tests depth, so a rectangle drawn deeper
than the shared text depth (ui_text_ex, 0.7) hides any text in the same function. Fades and
covers (0.95 and above) are meant to hide what is under them and are skipped."""
import re, sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
# Depths written as a number or as one of the named UI_*Z_* tokens of ui_style.h.
DEPTH = {m.group(1): float(m.group(2)) for m in re.finditer(
    r'#define (UI_\w*Z_\w+) ([0-9.]+)f\b', (root / 'include/breadbox/ui_style.h').read_text())}
TEXT = 0.7
files = sorted((root / 'app/ui/components').glob('*.inc')) + [root / 'app/ui/ui.c']
bad = []
for path in files:
    src = path.read_text()
    # function bodies: from a line starting a definition to the next one
    for body in re.split(r'\n(?=(?:PSX3D_COLD\S*\s+)?(?:static\s+)?(?:inline\s+)?[a-z_][a-z0-9_ *]*\([^;]*\)\s*\n?\{)', src):
        if not re.search(r'\bui_text(_ex|_wrap|_pixels)?\(', body):
            continue
        for m in re.finditer(r'C2D_DrawRectSolid\(([^;]*)\);', body):
            args = [a.strip() for a in m.group(1).split(',')]
            if len(args) < 3:
                continue
            if re.fullmatch(r'\d+\.\d+f', args[2]):
                z = float(args[2][:-1])
            elif args[2] in DEPTH:
                z = DEPTH[args[2]]
            else:
                continue
            if TEXT < z < 0.95:
                line = src[:src.index(m.group(0))].count('\n') + 1
                bad.append(f'{path.relative_to(root)}:{line}: panel at depth {z} covers text at {TEXT}')
for b in bad:
    print(b)
print('ui depth: %s' % ('FAIL' if bad else 'PASS'))
sys.exit(1 if bad else 0)
