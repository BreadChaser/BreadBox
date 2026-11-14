#!/usr/bin/env python3
"""Extend (never shrink) original frozen pins from the verified public reference's own objects."""
import json
from pathlib import Path
import subprocess
import sys
from pin_contract import FRONTEND_OBJECTS, NM, frontend_names
import reference

ROOT = Path(__file__).resolve().parents[2]
meta = json.loads((ROOT / 'tools/layout/baseline.json').read_text())['public_reference']
elf = Path(sys.argv[1]).resolve()
reference.verify(elf, meta)
source = elf.parents[2]
build = elf.parent / 'build-breadbox'
linker = (elf.parent / 'core-first.ld').read_text()


def functions(path):
    rows = subprocess.check_output([NM, str(path)], text=True).splitlines()
    return {p[2] for row in rows if len(p := row.split()) == 3 and p[1] in 'tT'}


surviving = functions(elf)
p = ROOT / 'tools/layout/pinned-names.json'
data = json.loads(p.read_text())
for obj in sorted(FRONTEND_OBJECTS):
    old = set(data['objects'].get(obj, []))
    data['objects'][obj] = sorted(old | (frontend_names(build, obj, linker) & surviving))
data['comment'] = 'Original build-2650 frontend requirements, extended with surviving functions of the qualified public reference using its own objects. Never record from a candidate.'
p.write_text(json.dumps(data, indent=1) + '\n')
p = ROOT / 'tools/layout/system-pinned-names.json'
data = json.loads(p.read_text())
for pins in sorted((source / 'systems').glob('*/frame-pins.txt')):
    system = pins.parent.name
    objects = [line.strip() + '.o' for line in pins.read_text().splitlines() if line.strip() and not line.startswith('#')]
    objects += [system + '_core_blob.o']
    for obj in objects:
        path = source / 'build/systems' / system / obj if obj.endswith('_core_blob.o') else build / obj
        if not path.is_file():
            continue
        old = set(data['systems'].setdefault(system, {}).get(obj, []))
        data['systems'][system][obj] = sorted(old | (functions(path) & surviving))
data['comment'] = 'Original build-2650 system requirements, extended with surviving functions of the qualified public reference using its own objects. Never record from a candidate.'
p.write_text(json.dumps(data, indent=1) + '\n')
print('extended frozen names from verified public reference; original names retained')
