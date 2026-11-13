#!/usr/bin/env python3
"""Print accepted-changes.json stubs for functions whose code changed on purpose.
accept-change.py --gate frontend|system BASE.elf NEW.elf NAME...
Fills in sizes and fingerprints; reason and evidence are left empty on purpose and must be
written by the reviewer (the gate rejects an entry without them)."""
import json, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import hotcode
a = sys.argv[1:]
if len(a) < 5 or a[0] != '--gate' or a[1] not in ('frontend', 'system'):
    sys.exit(__doc__)
gate, base, new, names = a[1], a[2], a[3], a[4:]
img = hotcode.Images(base, new)
bd, nd = hotcode.defs(base), hotcode.defs(new)
out = {}
for name in names:
    if name not in bd or name not in nd:
        sys.exit(f'{name}: not a function in both images')
    (ba, bs), (na, ns) = bd[name][0], nd[name][0]
    out[name] = {'gate': gate, 'reason': '', 'evidence': '',
                 'base': {'size': bs, 'fingerprint': img.fingerprint('base', ba, bs)},
                 'new': {'size': ns, 'fingerprint': img.fingerprint('new', na, ns)}}
    if ba & 4095 != na & 4095:
        out[name]['new_page_offset'] = na & 4095
        out[name]['page_reason'] = ''
        if out[name]['base'] == out[name]['new']:
            out[name]['change'] = 'page-only'
print(json.dumps(out, indent=1))
