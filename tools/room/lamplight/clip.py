"""Clip check (a failing gate of tools/room/build.sh): no two props of the room intersect or come within 5 mm of each
other, except the resting contacts listed in CONTACTS, and those only up to SINK deep. Parts are grouped into props (the lamp = base + pole + shade,
...); every pair whose bounds come within 5 mm is tested for triangle intersection (BVH overlap) and for a surface gap
under 5 mm (nearest-surface probes from vertices, face centres and edge midpoints, both ways).
The same clearance rules apply to every room style.
"""
from mathutils import Vector
from mathutils.bvhtree import BVHTree

CLEAR = 0.005
SINK = 0.02       # how far a resting prop may sink into what holds it (the rug is 12 mm: legs and a cushion's edge pass through it)
SHELL = {'ceiling', 'beam', 'wall', 'wainscot', 'trim', 'frame', 'reveal'}
PROP = {
    'floor': 'floor', 'sky_grad': 'sky', 'sky_hills': 'sky', 'rug': 'rug', 'print_frame': 'print', 'print': 'print',
    'poster_frame': 'poster', 'poster_back': 'poster', 'board_frame': 'board', 'board_back': 'board',
    'bookcase': 'bookcase', 'bookcase_back': 'bookcase', 'picture_light': 'bookcase',
    'tvstand': 'tvstand', 'tvbody': 'tv', 'tvrear': 'tv', 'tvbezel': 'tv', 'tv_screen': 'tv',
    'lamp_metal': 'lamp', 'shade': 'lamp', 'desk': 'desk', 'drawers': 'desk', 'pulls': 'desk',
    'desklamp': 'desklamp', 'deskshade': 'desklamp', 'mug': 'desk_mug', 'coffee': 'desk_mug',
    'table': 'table', 'table_legs': 'table', 'card_foot': 'card_foot', 'card': 'card', 'gba_dev': 'gba',
    'gba_body': 'gba', 'case_cover': 'ps1case', 'case_body': 'ps1case', 'candle_jar': 'candle', 'candle_wax': 'candle',
    'mug2': 'table_mug', 'coffee2': 'table_mug', 'cushion1': 'cushion1', 'cushion2': 'cushion2',
    'pot': 'plant', 'snake': 'plant',
}
# Resting contacts, (what holds, what rests on it): the resting prop may sink at most SINK into its support.
CONTACTS = [
    ('floor', 'room'), ('floor', 'rug'), ('floor', 'tvstand'), ('floor', 'lamp'), ('floor', 'bookcase'),
    ('floor', 'desk'), ('floor', 'table'), ('floor', 'plant'), ('floor', 'cushion1'), ('floor', 'cushion2'),
    ('rug', 'table'), ('rug', 'cushion1'), ('rug', 'cushion2'),
    ('room', 'desk'), ('room', 'print'), ('room', 'poster'), ('room', 'board'),
    ('tvstand', 'tv'), ('desk', 'desklamp'), ('desk', 'desk_mug'), ('desk', 'scope'),
    ('table', 'card_foot'), ('table', 'gba'), ('table', 'ps1case'), ('table', 'candle'), ('table', 'table_mug'),
    ('card_foot', 'card')]
STRUCTURE = {('floor', 'room')}   # the shell's own joints: no depth to measure
ALLOW = {frozenset(p) for p in CONTACTS}
HOLDS = {frozenset(p): p for p in CONTACTS}


def prop_of(name):
    if name in SHELL:
        return 'room'
    if name.startswith('scope_') or name in ('knobs', 'knobcaps'):
        return 'scope'
    return PROP[name]


def check(polys):
    """polys: [(part, [(pos, uv, nrm), ...])] in world space -> (results, fails)."""
    props = {}
    for part, corners in polys:
        a = props.setdefault(prop_of(part.name), dict(v=[], f=[]))
        b = len(a['v'])
        a['v'] += [c[0] for c in corners]
        a['f'].append(list(range(b, b + len(corners))))
    for a in props.values():
        a['tree'] = BVHTree.FromPolygons(a['v'], a['f'], all_triangles=False, epsilon=0.0)
        a['lo'] = Vector(tuple(min(v[i] for v in a['v']) for i in range(3)))
        a['hi'] = Vector(tuple(max(v[i] for v in a['v']) for i in range(3)))
        pts = list(a['v'])
        for f in a['f']:
            ps = [a['v'][k] for k in f]
            pts.append(sum(ps, Vector()) / len(ps))
            pts += [(ps[j] + ps[(j + 1) % len(ps)]) / 2 for j in range(len(ps))]
        a['probe'] = pts

    def gap(a, b):
        best = None
        for src, dst in ((a, b), (b, a)):
            for p in src['probe']:
                if not all(dst['lo'][i] - CLEAR <= p[i] <= dst['hi'][i] + CLEAR for i in range(3)):
                    continue
                hit = dst['tree'].find_nearest(p, CLEAR)
                if hit[0] is not None and (best is None or hit[3] < best):
                    best = hit[3]
        return best

    def depth(a, b):
        """How far a's points lie behind b's nearest surface (signed by its face normal), metres."""
        worst = 0.0
        for p in a['probe']:
            if not all(b['lo'][i] - 0.2 <= p[i] <= b['hi'][i] + 0.2 for i in range(3)):   # a flat support has no depth of its own
                continue
            loc, nrm, idx, dist = b['tree'].find_nearest(p, 0.2)
            if loc is not None:
                worst = max(worst, -(p - loc).dot(nrm))   # depth along the support's normal (beside its edge: just the height)
        return worst

    names = sorted(props)
    results, fails = [], []
    for i, na in enumerate(names):
        for nb in names[i + 1:]:
            A, B = props[na], props[nb]
            if any(A['lo'][k] > B['hi'][k] + CLEAR or B['lo'][k] > A['hi'][k] + CLEAR for k in range(3)):
                continue
            allowed = frozenset((na, nb)) in ALLOW
            inter = len(A['tree'].overlap(B['tree']))
            g = gap(A, B)
            state = 'intersect' if inter else ('touch' if g is not None else 'clear')
            sunk = 0.0
            if allowed and inter and HOLDS[frozenset((na, nb))] not in STRUCTURE:
                support, prop = HOLDS[frozenset((na, nb))]
                sunk = round(depth(props[prop], props[support]), 4)
            r = dict(a=na, b=nb, state=state, tri_pairs=inter, min_gap_m=None if g is None else round(g, 4),
                     allowed=allowed, sunk_m=sunk, ok=(allowed and sunk <= SINK) or state == 'clear')
            results.append(r)
            if not r['ok']:
                fails.append(r)
    return results, fails
