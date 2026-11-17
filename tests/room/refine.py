#!/usr/bin/env python3
"""The shipped room is the refined mesh (V3): every grid line tools/room/lamplight/refine.json adds
(where the Cycles light changes faster than the 30 cm cells follow) has vertices in assets/romfs/room/room.bbr, on
its surface's plane. A default build (tools/room/build.sh, no env) reads refine.json; a room built without it has
only the 30 cm (rug: 40 cm) cell lines and fails here. Plain Python: no Blender, no NumPy."""
import json
import pathlib
import struct
import sys

root = pathlib.Path(__file__).resolve().parents[2]
room = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'assets/romfs/room/room.bbr'
refine = json.loads((root / 'tools/room/lamplight/refine.json').read_text())
FRONT = 1.58                         # bbr3.FRONT: the bookcase's front plane is BBR z = 0
CELLS = (0.3, 0.4)                   # scene.py's own cell lines (shell 30 cm, rug 40 cm)

d = room.read_bytes()
magic, ver, nstyle, ntex, npart, nanc, stride, nv, ni, _style = struct.unpack_from('<4sHHHHHHII8s', d, 0)
assert magic == b'BBR1' and ver == 3 and stride == 16, f'{room}: not a BBR v3 room file'
o = 32 + ntex * 24 + npart * 48 + nanc * 56 + nstyle * 24
world = []                           # every vertex in Blender's world (metres): x right, y into the room, z up
for i in range(nv):
    x, y, z = struct.unpack_from('<3h', d, o + i * 16)
    world.append((x / 1000, z / 1000 + FRONT, y / 1000))

checked, missing = 0, []
for (part, n, off), lines in refine:
    on = [p for p in world if abs(sum(a * b for a, b in zip(n, p)) - off) < 0.0015]
    assert on, f'{part} {n} {off}: no room vertex on this surface'
    for axis, coords in lines.items():
        a = int(axis)
        have = sorted(p[a] for p in on)
        for c in coords:
            if any(abs(c / k - round(c / k)) < 1e-3 for k in CELLS):
                continue                 # a line the plain cells have too: proves nothing
            checked += 1
            if not any(abs(h - c) < 0.0015 for h in have):
                missing.append(f'{part} n={n} axis {a} at {c:.4f} m')
assert checked >= 100, f'only {checked} refined lines to check: refine.json is not the V3 data'
assert not missing, f'{len(missing)} of {checked} refined grid lines are not in {room.name} (built without refine.json?):\n  ' + '\n  '.join(missing[:12])
print(f'room refine: {checked} refined grid lines of {len(refine)} surfaces in {room.name} ({nv} vertices, {ni // 3} triangles) PASS')
