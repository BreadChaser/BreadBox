#!/usr/bin/env python3
"""Every room-side vertex buffer (BufInfo_Add) agrees with the attribute layout it is drawn with.

The PICA's vertex loader takes a buffer's component list (the permutation) at face value: a list that
names an attribute the AttrInfo does not load, or whose sizes do not add up to the stride, hangs or
corrupts the real GPU, while Azahar skips the extra attribute and draws correctly. 16503 (room BBR v3)
shrank room_vertex to 3 attributes / 16 bytes but left the walls' buffer at 4 / 0x3210: the Old hung
at the first frame that drew the poster. This check reads the sources, so it needs no console.
Per BufInfo (all its BufInfo_Add calls together): every component is a distinct attribute (a repeated one can still
add up to the stride, and the PICA does not define it), and together they are exactly the AttrInfo's loaded
attributes (a missing buffer leaves an attribute unread).
"""
from pathlib import Path
import re, subprocess, sys, tempfile

R = Path(__file__).resolve().parents[2]
SIZES = {'GPU_BYTE': 1, 'GPU_UNSIGNED_BYTE': 1, 'GPU_SHORT': 2, 'GPU_FLOAT': 4}
# vertex type -> (file, AttrInfo it is drawn with); a literal stride (the colour stream) uses the room's
LAYOUT = {'room_vertex': ('app/ui/room_scene.c', 'S.attr'), 'hand_vertex': ('app/ui/room_hands.c', 'H.attr_hand'),
          'prop_vertex': ('app/ui/room_hands.c', 'H.attr_prop')}
FILES = ['app/ui/room_scene.c', 'app/ui/room_walls.c', 'app/ui/room_hands.c', 'app/ui/room_board.c', 'app/ui/shelf_scene.c',
         'app/ui/shelf_scene_draw.c', 'app/ui/shelf_scene_spines.c', 'app/ui/shelf_scene_wood.c']


def attrs(path, var):
    """index -> bytes per attribute (fixed attributes have no loader: None)"""
    src = (R / path).read_text()
    out = {}
    for i, t, n in re.findall(r'AttrInfo_AddLoader\(&' + re.escape(var) + r',\s*(\d+),\s*(\w+),\s*(\d+)\)', src):
        out[int(i)] = (SIZES[t], SIZES[t] * int(n))
    for i in re.findall(r'AttrInfo_AddFixed\(&' + re.escape(var) + r',\s*(\d+)\)', src):
        out[int(i)] = None
    assert out, (path, var)
    return out


def sizeof(types):
    """the vertex types' real sizes, from the headers and room_hands.c's typedefs"""
    hands = (R / 'app/ui/room_hands.c').read_text()
    defs = '\n'.join(re.findall(r'typedef struct \{[^}]*\} (?:hand|prop)_vertex;', hands))
    c = '#include <stdio.h>\n#include <stdint.h>\n#include "room.h"\n' + defs + '\nint main(void){' + \
        ''.join(f'printf("%zu\\n",sizeof({t}));' for t in types) + '}'
    with tempfile.TemporaryDirectory() as d:
        Path(d, 'm.c').write_text(c)
        subprocess.run(['cc', '-I', str(R / 'include/breadbox'), '-I', str(R / 'tests/fakes'), '-I', str(R / 'include'),
                        '-o', f'{d}/m', f'{d}/m.c'], check=True)
        return dict(zip(types, map(int, subprocess.check_output([f'{d}/m'], text=True).split())))


def check(where, stride, count, perm, layout):
    comps = [(perm >> (4 * k)) & 15 for k in range(count)]
    assert perm >> (4 * count) == 0, f'{where}: permutation {perm:#x} names more than {count} components'
    off = 0
    for c in comps:
        if c >= 12:   # 0xC-0xF: 4-16 bytes of padding
            off = (off + 3) & ~3
            off += (c - 11) * 4
            continue
        assert c in layout and layout[c] is not None, f'{where}: component {c} is no loaded attribute of its AttrInfo {sorted(layout)}'
        align, size = layout[c]
        off = (off + align - 1) // align * align + size
    assert off == stride, f'{where}: components {comps} take {off} bytes, stride is {stride}'
    return comps


def main():
    sizes = sizeof(list(LAYOUT))
    assert sizes['room_vertex'] == 16, sizes
    n, groups = 0, {}
    for f in FILES:
        p = R / f
        if not p.exists():
            continue
        for m in re.finditer(r'BufInfo_Add\(([^;]*?),\s*(sizeof\((\w+)\)|\d+),\s*(\d+),\s*(0x[0-9A-Fa-f]+)\)', p.read_text()):
            line = p.read_text()[:m.start()].count('\n') + 1
            typ = m.group(3) or 'room_vertex'
            stride = sizes[typ] if m.group(3) else int(m.group(2))
            lf, var = LAYOUT[typ]
            layout = attrs(lf, var)
            comps = check(f'{f}:{line}', stride, int(m.group(4)), int(m.group(5), 16), layout)
            info = m.group(1).split(',')[0].strip()
            g = groups.setdefault((f, info), dict(where=f'{f}:{line}', comps=[], layout=layout))
            assert g['layout'] == layout, f'{f}:{line}: one BufInfo ({info}) drawn with two attribute layouts'
            g['comps'] += [c for c in comps if c < 12]
            n += 1
    for (f, info), g in groups.items():
        dup = sorted({c for c in g['comps'] if g['comps'].count(c) > 1})
        assert not dup, f"{g['where']}: {info} supplies attribute(s) {dup} more than once"
        loaded = {i for i, v in g['layout'].items() if v is not None}
        assert set(g['comps']) == loaded, f"{g['where']}: {info} supplies {sorted(set(g['comps']))}, its AttrInfo loads {sorted(loaded)}"
    assert n >= 9, f'only {n} vertex buffers found: the pattern no longer matches the sources'
    # the regression itself: the walls draw with the room's attributes
    walls = (R / 'app/ui/room_walls.c').read_text()
    assert 'sizeof(room_vertex), 3, 0x210' in walls, 'room_walls.c: the walls buffer must match room_vertex (3 attributes, 0x210)'
    print(f'room vertex loaders PASS: {n} buffers in {len(groups)} BufInfos match their attribute layouts and strides; each loaded attribute once')


if __name__ == '__main__':
    try:
        main()
    except AssertionError as e:
        print('FAIL:', e)
        sys.exit(1)
