"""Build the Lamplight room file (BBR v3). Headless Blender (tools/room/build.sh runs it with the pinned build):

  blender -b --factory-startup --python tools/room/lamplight/build.py -- --out DIR

Writes DIR/room.bbr (what the app ships), DIR/atlas-<set>.png (the atlases, for review), DIR/check.json (the clip
check) and DIR/stats.json. Exits non-zero when the clip check or the Home framing check fails.
Deterministic: no randomness beyond fixed seeds, one thread of Python, the same Blender build.
"""
import json
import math
import os
import struct
import sys
import zlib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from mathutils import Matrix, Vector  # noqa: E402

import bbr3  # noqa: E402
import clip  # noqa: E402
import gi  # noqa: E402
import rig as rigs  # noqa: E402
import scene  # noqa: E402
import tex  # noqa: E402

TV_RECT = (0, 0, 128, 96)
OPT = {}                           # args(): threads, quick, ref          # the TV picture's texels in the app's 128 x 128 `tv` texture


def args():
    """build.py options after '--': --out DIR, --threads N (Cycles), --quick (GI_SAMPLES / GI_AO_SAMPLES from the
    environment: not the shipped room), --ref (Cycles reference renders of the styles, no room file), the self-tests."""
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    out = os.path.join(os.getcwd(), 'out')
    OPT.update(threads=2, quick=False, ref=False)
    for i, k in enumerate(a):
        if k == '--out':
            out = os.path.abspath(a[i + 1])
        if k == '--threads':
            OPT['threads'] = int(a[i + 1])
        if k in ('--quick', '--ref'):
            OPT[k[2:]] = True
        if k == '--clip-selftest':   # The lamp (shade 4 cm into the bookcase): the check must fail
            scene.LAMP_X, scene.LAMP_Y, scene.SHADE_R = -0.88, 1.72, (0.24, 0.15, 0.16, 0.14)
        if k == '--sink-selftest':   # the desk 5 cm into the floor (an allowed contact, too deep): must fail
            scene.DESK_DZ = -0.05
    return out


def png(path, img):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    h, w = len(img), len(img[0])
    raw = b''.join(b'\0' + bytes(c for px in row for c in px) for row in img)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def world_polys(R):
    """Every face in world space: [(part, [(pos, uv, nrm)])]; smooth parts average their normals per position."""
    out = []
    for part, loc, rz in R.parts:
        M = Matrix.Rotation(rz, 3, 'Z')
        nmap = {}
        if part.smooth:
            for f in part.f:
                a, b, c = part.v[f[0]], part.v[f[1]], part.v[f[2]]
                fn = (b - a).cross(c - a)
                for i in f:
                    key = tuple(round(x, 4) for x in part.v[i])
                    nmap[key] = nmap.get(key, Vector()) + fn
        for f, uvs in zip(part.f, part.uv):
            corners = []
            for k, i in enumerate(f):
                n = part.n[i]
                if part.smooth:
                    n = nmap.get(tuple(round(x, 4) for x in part.v[i]), n)
                corners.append((M @ part.v[i] + loc, tuple(uvs[k]), (M @ n).normalized()))
            out.append((part, corners))
    return out


def bbr_dir(n):
    return Vector((n.x, n.z, n.y))


def triangles(ids, P, nrm):
    """Fan of a convex polygon, each triangle wound so (b - a) x (c - a) points along nrm (BBR space)."""
    out = []
    for k in range(1, len(ids) - 1):
        a, b, c = ids[0], ids[k], ids[k + 1]
        pa, pb, pc = (Vector(P[i]) for i in (0, k, k + 1))
        cr = (pb - pa).cross(pc - pa)
        if cr.length < 1e-6:
            continue
        out += [a, b, c] if cr.dot(nrm) > 0 else [a, c, b]
    return out


def mesh_from(R, polys, layout, sizes):
    """The BBR mesh and the light keys (part, position, normal) every vertex is coloured by."""
    M = bbr3.Mesh()
    M.by_part = {}             # triangles per model part, before and after the cuts on texture repeats
    M.pieces = []              # the cut polygons: (part, [(pos, normal, light key)]) (the GI bake's mesh)
    keys, key_of = [], {}
    slot = dict(scene.GROUPS)
    slot['tv_screen'] = 1
    for part, corners in polys:
        if part.mat == 'tv':
            x0, y0, w, h = TV_RECT
        else:
            (x0, y0), (w, h) = layout[part.mat], sizes[part.mat]
        nf = bbr_dir(sum((c[2] for c in corners), Vector()))
        for piece, (ou, ov) in bbr3.split(corners):
            ids, pc = [], []
            for pos, uv, n in piece:
                fu = min(1.0, max(0.0, uv[0] - ou))
                fv = min(1.0, max(0.0, uv[1] - ov))
                tx = x0 + bbr3.EPS + fu * (w - 2 * bbr3.EPS)
                ty = y0 + bbr3.EPS + (1 - fv) * (h - 2 * bbr3.EPS)
                lk = (part.name, tuple(round(x, 4) for x in pos), tuple(round(x, 3) for x in n))
                if lk not in key_of:
                    key_of[lk] = len(keys)
                    keys.append((part, pos.copy(), n.copy()))
                ids.append(M.vertex(bbr3.to_bbr(pos), slot[part.group], (bbr3.i16(tx * 16), bbr3.i16(ty * 16)), key_of[lk]))
                pc.append((pos.copy(), n.copy(), key_of[lk]))
            M.pieces.append((part, pc))
            P = [M.verts[i][0] for i in ids]
            t = triangles(ids, P, nf)
            M.tris.setdefault(part.group, []).extend(t)
            c = M.by_part.setdefault(part.name, [0, 0])
            c[1] += len(t) // 3
        c = M.by_part.setdefault(part.name, [0, 0])
        c[0] += len(corners) - 2
    return M, keys


SPRITE_CORNERS = ((0, 0), (1, 0), (1, 1), (0, 1))     # TL TR BR BL (the shader's corner table)


def add_sprites(R, M, keys, layout, sizes):
    (x0, y0), (w, h) = layout['glow'], sizes['glow']
    for kind, c, size, key in R.sprites:
        kid = len(keys)
        keys.append(('sprite', key))
        ids = []
        for corner, (cu, cv) in enumerate(SPRITE_CORNERS):
            uv = (bbr3.i16((x0 + bbr3.EPS + cu * (w - 2 * bbr3.EPS)) * 16), bbr3.i16((y0 + bbr3.EPS + cv * (h - 2 * bbr3.EPS)) * 16))
            M.verts.append((tuple(bbr3.i16(x) for x in bbr3.to_bbr(c)), bbr3.i16(size * 2000), uv, kid, corner))
            ids.append(len(M.verts) - 1)
        M.tris.setdefault(kind, []).extend([ids[0], ids[1], ids[2], ids[0], ids[2], ids[3]])


def colours(M, keys, rig, sky, light, unit):
    """The style's colour stream: rgba per vertex. light: the Cycles bake's display light per key (gi.tone);
    unit: vertex bytes per 1.0 of light (128, or 255 / 4 for a range-4 style)."""
    cache, out = {}, []
    top = 255 / unit
    for (pos, w, uv, kid, alpha) in M.verts:
        if kid not in cache:
            k = keys[kid]
            if k[0] == 'sprite':
                g = rig.st['glow'].get(k[1])
                c = rigs.hx(g[0], g[1]) if g else Vector((0, 0, 0))
                cache[kid] = tuple(max(0, min(255, int(round(x * 255)))) for x in c)
            else:
                part, p, n = k
                if part.vcol or part.unlit:
                    c = rigs.unlit_colour(rig, part, p, sky)
                else:
                    L = Vector(light[kid]) * part.dim
                    e = rig.emit.get(part.emit, 0.0) if part.emit else 0.0
                    L = L + Vector((1, 0.9, 0.7)) * e
                    t = rig.tint(part.tint)
                    c = (min(L.x * t.x, top), min(L.y * t.y, top), min(L.z * t.z, top))
                cache[kid] = tuple(max(0, min(255, int(round(x * unit)))) for x in c)
        out.append(cache[kid] + (alpha,))
    return out


def style_rig(rig_all, sid, T):
    """A style's rig, window gradient and the flat albedo of a part for the Cycles scene (texture mean x tint)."""
    st = rig_all['styles'][sid]
    g = st['gi']
    rig = rigs.Rig(st)
    sky = dict(stops=[Vector(c) / 255 for c in tex.sky_stops(rig_all['texture_sets'][st['textures']])], span=0.78)
    mean = {}
    for name, a in T[st['textures']].items():
        m = a[..., 3] > 127
        mean[name] = gi.lin(tuple(a[..., :3][m].mean(0) / 255)) if m.any() else (0.5, 0.5, 0.5)
    mean['tv'] = (0.05, 0.05, 0.06)

    def albedo_of(part):
        t = gi.lin(tuple(rig.tint(part.tint)))
        a = g.get('albedo', {}).get(part.name)
        base = gi.lin(gi.hx(a)) if a else mean.get(part.mat, (0.5, 0.5, 0.5))
        return tuple(min(0.9, b * c * part.dim) for b, c in zip(base, t))
    return rig, sky, albedo_of


def bake_style(rig_all, sid, T, M, keys, probes_at, out):
    """One style's Cycles bake -> (colour stream, probes, light range). The sample counts are the style's own
    (lights.json: bit-exact rebuilds) unless --quick lets GI_SAMPLES / GI_AO_SAMPLES override them."""
    g = rig_all['styles'][sid]['gi']
    rig, sky, albedo_of = style_rig(rig_all, sid, T)
    env = os.environ if OPT['quick'] else {}
    E, cnt, pl = gi.bake(rig_all, sid, M.pieces, len(keys), albedo_of, probes_at, scene,
                         samples=int(env.get('GI_SAMPLES', g['samples'])), ao_samples=int(env.get('GI_AO_SAMPLES', g['ao_samples'])),
                         threads=OPT['threads'])
    lrange = g.get('range', 2)
    np.save(os.path.join(out, 'gi-%s.npy' % sid), E)
    E = E * gi.falloff([k[1] if k[0] != 'sprite' else None for k in keys], g)[:, None]
    gain = g.get('gain', {})   # art direction: light x factor per part (e.g. the bookcase's deep cubby backs)
    if gain:
        E = E * np.array([gain.get(k[0].name, 1.0) if k[0] != 'sprite' else 1.0 for k in keys])[:, None]
    for name, k in g.get('smooth', {}).items():   # art direction: pull a part's light toward its mean (calm blotches)
        m = np.array([kk[0] != 'sprite' and kk[0].name == name for kk in keys])
        if m.any():
            E[m] = (1 - k) * E[m] + k * E[m].mean(0)
    pl = np.array(pl) * gi.falloff(probes_at, g)[:, None, None]
    cols = colours(M, keys, rig, sky, gi.tone(E, g), 255 / lrange if lrange != 2 else 128)
    pd = gi.tone(np.array(pl).reshape(-1, 3), g).reshape(-1, 6, 3)
    probes = [(bbr3.to_bbr(p), [tuple(max(0, min(255, int(round(x / lrange * 255)))) for x in ax) for ax in pd[i]])
              for i, p in enumerate(probes_at)]
    print('gi', sid, 'keys', len(keys), 'unsampled', int((cnt == 0).sum()), flush=True)
    return cols, probes, lrange


def probe_points():
    """A tiny probe set (world space): in front of every cubby, over the two wall pictures, at the hands' places."""
    c = scene.CASE
    pts = []
    for r in range(c['rows']):
        for b in range(c['bays']):
            x = (c['x0'] + (b + 0.5) * c['bay']) / 1000
            z = (c['base'] + (r + 0.5) * c['gap']) / 1000
            pts.append((x, scene.FRONT - 0.06, z))
    for w, sx in ((scene.POSTER, 1), (scene.BOARD, -1)):
        for i in (-1, 0, 1):
            for j in (-1, 1):
                pts.append((w['x'] + sx * 0.15, w['y'] + i * w['w'] / 3, w['z'] + j * w['h'] / 3))
    pts += [(1.4, 1.1, 1.0), (0.88, -0.1, 0.65), (-1.3, 0.9, 0.9), (-1.0, 0.4, 0.5), (0.0, 1.2, 1.3)]
    return [Vector(p) for p in pts]


def pose(pos, look, F, eye, conv):
    return list(bbr3.to_bbr(pos)) + list(bbr3.to_bbr(look)) + [F, eye, conv]


def fov_F(deg):
    return round(200 / math.tan(math.radians(deg) / 2), 1)


def anchors_of(R):
    A = {}
    tv_loc, tv_rz, tv_c = R.tv
    Mtv = Matrix.Rotation(tv_rz, 3, 'Z')
    screen = Mtv @ Vector(tv_c) + Vector(tv_loc)
    tv_n = (Mtv @ Vector((0, -1, 0))).normalized()
    Ms = Matrix.Rotation(scene.SCOPE_RZ, 3, 'Z')
    sx0, sx1, sz0, sz1, sy = R.scope

    def scope_pt(x, y, z):
        return Ms @ Vector((x, y, z)) + scene.SCOPE_LOC
    glass = scope_pt((sx0 + sx1) / 2, sy, (sz0 + sz1) / 2)
    c = scene.CASE
    cx = (c['x0'] + c['bay'] * c['bays'] / 2)
    D = (c['bay'] * c['bays'] + 20) * 300 / 400
    ly = 1180.0
    A['pose.home'] = pose((0.05, -2.75, 1.52), (0.1, 1.2, 0.98), fov_F(64), 100, 2900)
    A['pose.continue'] = pose(screen + tv_n * 1.25 + Vector((0, 0, 0.12)), screen, fov_F(40), 18, 1150)
    A['pose.library'] = [cx, ly + 40, -D, cx, ly, 0, 300, 40, D]
    A['pose.settings'] = pose((1.34, 0.80, 1.15), (1.58, 1.66, 0.95), fov_F(44), 14, 900)
    A['pose.together'] = pose((-1.0, -1.45, 1.2), (-1.0, 0.62, 0.15), 400, 20, 2200)
    A['pose.share'] = pose((0.84, -0.66, 0.96), (0.86, 0.2, 0.52), fov_F(54), 14, 810)
    for key, w, sx in (('poster', scene.POSTER, 1), ('board', scene.BOARD, -1)):
        ctr = (w['x'] + sx * 0.012, w['y'], w['z'])
        A['pose.' + key] = pose((w['x'] + sx * (1.5 if key == 'poster' else 1.36), w['y'] - 0.25 * sx, w['z']), ctr,
                               370 if key == 'poster' else 325, 30, 1500)
        p = bbr3.to_bbr(ctr)
        A[key] = [p[0], p[1], p[2], w['w'] * 1000, w['h'] * 1000, sx]
    A['focus.continue'] = list(bbr3.to_bbr((-1.65, 1.35, 0.95)))
    A['focus.library'] = [cx, 1200.0, -10.0]
    A['focus.settings'] = list(bbr3.to_bbr(scope_pt(0, -0.16, 0.36)))
    A['focus.together'] = list(bbr3.to_bbr((-1.0, 0.66, 0.35)))
    A['focus.share'] = list(bbr3.to_bbr((scene.CARD['x'], scene.CARD['y'], 0.64)))
    A['focus.poster'] = list(bbr3.to_bbr((scene.POSTER['x'] + 0.03, scene.POSTER['y'], scene.POSTER['z'] + 0.4)))
    A['focus.board'] = list(bbr3.to_bbr((scene.BOARD['x'] - 0.03, scene.BOARD['y'], scene.BOARD['z'] + 0.33)))
    A['screen'] = list(bbr3.to_bbr(screen)) + [0.0]
    A['scope'] = list(bbr3.to_bbr(glass)) + [(sx1 - sx0) * 500, (sz1 - sz0) * 500, 11.0, 0.0, 0.52, 6.0, 0.5]
    A['slabel'] = list(bbr3.to_bbr(scope_pt((sx0 + sx1) / 2, sy - 0.001, sz0 + 0.22 * (sz1 - sz0)))) + [(sx1 - sx0) * 800, 17.0]
    A['knob'] = list(bbr3.to_bbr(scope_pt(-0.2 + 0.4 * 107 / 128, -0.185, 0.32 - 0.3 * 20 / 96))) + [0.4 * 9 / 128 * 0.85 * 1000]
    card_c, yaw, pitch = R.card
    A['card'] = list(bbr3.to_bbr(card_c)) + [yaw, pitch, scene.CARD['w'] * 1000, scene.CARD['h'] * 1000]
    A['mug'] = list(bbr3.to_bbr(R.mug))
    A['case'] = [c['x0'], c['bay'], c['bays'], c['gap'], c['rows'], c['top'], c['depth'], c['upr'], c['board'], c['base']]
    return A


def frame_check(A, polys, names):
    """Where the named parts land in the Home view (400 x 240), from the BBR camera (room_camera's model)."""
    p = A['pose.home']
    pos, look, F = Vector(p[0:3]), Vector(p[3:6]), p[6]
    d = (look - pos).normalized()
    right = Vector((d.z, 0, -d.x)).normalized()
    up = d.cross(right).normalized()
    if up.y < 0:
        up = -up
    out = {}
    for part, corners in polys:
        if part.name not in names:
            continue
        for c in corners:
            q = Vector(bbr3.to_bbr(c[0])) - pos
            z = q.dot(d)
            x, y = 200 + F * q.dot(right) / z, 120 - F * q.dot(up) / z
            b = out.setdefault(part.name, [1e9, 1e9, -1e9, -1e9])
            b[0], b[1], b[2], b[3] = min(b[0], x), min(b[1], y), max(b[2], x), max(b[3], y)
    return {k: dict(box=[round(v) for v in b], inside=b[0] >= 0 and b[1] >= 0 and b[2] <= 400 and b[3] <= 240)
            for k, b in out.items()}


GROUPS_ORDER = [(g, 'opaque', 0, 0 if g == 'static' else 2 if g == 'a_card' else 4) for g, _ in scene.GROUPS] + \
    [('tv_screen', 'opaque', 'tv', 4), ('glow', 'add', 0, 9), ('steam', 'add', 0, 9)]


def main():
    out = args()
    os.makedirs(out, exist_ok=True)
    # nothing of an earlier run may pass for this one's (build.sh checks OK last)
    for f in ('room.bbr', 'room.bbr.tmp', 'check.json', 'stats.json', 'OK'):
        if os.path.exists(os.path.join(out, f)):
            os.remove(os.path.join(out, f))
    rig_all = json.load(open(os.path.join(HERE, 'lights.json')))
    order = rig_all['order']
    sets = []
    for sid in order:
        s = rig_all['styles'][sid]['textures']
        if s not in sets:
            sets.append(s)
    T = {s: tex.textures(rig_all['texture_sets'][s]) for s in sets}
    sizes = {n: (a.shape[1], a.shape[0]) for n, a in T[sets[0]].items()}
    for s in sets:
        assert {n: (a.shape[1], a.shape[0]) for n, a in T[s].items()} == sizes, 'texture sets differ in layout: %s' % s
    layout = bbr3.pack(sizes)
    # extra grid lines where the light changes (refine.json, kept data), so Gouraud light follows the Cycles bake
    scene.REFINE = {tuple([k[0], tuple(k[1]), k[2]]): {int(a): v for a, v in lines.items()}
                    for k, lines in json.load(open(os.path.join(HERE, 'refine.json')))}
    scene.WINDOW_GLOW = max([st.get('glow', {}).get('window', [0, 0, 0])[2] if len(st.get('glow', {}).get('window', [])) > 2
                             else 0.0 for st in rig_all['styles'].values()])
    R = scene.build(sizes)
    polys = world_polys(R)
    results, fails = clip.check(polys)
    for r in results:
        if r['state'] != 'clear':
            print('CLIP', r['a'], r['b'], r['state'], r['tri_pairs'], r['min_gap_m'], 'allowed' if r['allowed'] else 'FAIL')
    print('CLIP-CHECK', 'PASS' if not fails else 'FAIL %d' % len(fails), 'pairs', len(results), flush=True)
    A = anchors_of(R)
    # [the final pass's dither strength 0..1, the share of empty cubbies with decor 0..1] (room_scene.c, room_bbr.c)
    A['post'] = list(rig_all['post']) + [0.0] * (10 - len(rig_all['post']))
    frame = frame_check(A, polys, ('cushion1', 'cushion2', 'shade', 'bookcase'))
    print('FRAME', json.dumps(frame, sort_keys=True))
    json.dump(dict(pairs_checked=len(results), fails=fails, results=results, frame=frame), open(os.path.join(out, 'check.json'), 'w'), indent=1)
    if fails or not all(frame[k]['inside'] for k in ('cushion1', 'cushion2')):
        sys.exit('room: clip or framing check failed (see %s/check.json)' % out)
    M, keys = mesh_from(R, polys, layout, sizes)
    add_sprites(R, M, keys, layout, sizes)
    tex_index, textures = {}, []
    for s in sets:
        tex_index[s] = len(textures)
        img = bbr3.atlas_image(T[s], layout)
        png(os.path.join(out, 'atlas-%s.png' % s), img)
        textures.append(('atlas' if s == sets[0] else 'atlas_' + s[0], bbr3.ATLAS_W, bbr3.ATLAS_H, 'rgba5551', bbr3.rgba5551(img)))
    tv_tex = len(textures)
    textures += [('tv', 128, 128, 'runtime', None), ('shelf', 256, 512, 'runtime', None), ('spines', 512, 256, 'runtime', None)]
    groups = [(g, m, tv_tex if t == 'tv' else 0, fl, 'p_tv' if g == 'tv_screen' else g,
               tuple(bbr3.to_bbr(R.card[0])) if g == 'a_card' else (0.0, 0.0, 0.0)) for g, m, t, fl in GROUPS_ORDER]
    probes_at = probe_points()
    if OPT['ref']:   # reference renders only: no room file, no OK marker (build.sh ships nothing)
        for sid in order:
            gi.reference(rig_all, sid, M.pieces, style_rig(rig_all, sid, T)[2], scene, os.path.join(out, 'ref-%s.png' % sid),
                         OPT['threads'])
        print('REF-DONE', flush=True)
        return
    styles = []
    for sid in order:
        cols, probes, lrange = bake_style(rig_all, sid, T, M, keys, probes_at, out)
        styles.append((sid, cols, probes, tex_index[rig_all['styles'][sid]['textures']], lrange if lrange != 2 else 0))
        print('style', sid, 'baked', flush=True)
    json.dump([[k[0].name, list(k[1]), list(k[2])] if k[0] != 'sprite' else ['sprite', [0, 0, 0], [0, 0, 1]] for k in keys],
              open(os.path.join(out, 'gi-keys.json'), 'w'))
    stats = bbr3.write(os.path.join(out, 'room.bbr.tmp'), groups, M, textures, A, styles, order[0])
    os.replace(os.path.join(out, 'room.bbr.tmp'), os.path.join(out, 'room.bbr'))
    stats.update(tris_by_part={k: dict(uncut=v[0], cut=v[1]) for k, v in sorted(M.by_part.items(), key=lambda kv: -kv[1][1])},
                 atlas=dict(layout={k: list(v) + list(sizes[k]) for k, v in sorted(layout.items())},
                            texels=sum(w * h for w, h in sizes.values())), frame=frame, clip_pairs=len(results))
    json.dump(stats, open(os.path.join(out, 'stats.json'), 'w'), indent=1, sort_keys=True)
    print('room.bbr', json.dumps({k: stats[k] for k in ('bytes', 'vertices', 'triangles', 'styles')}), flush=True)
    # the success marker, written last: the room file's SHA-256 (Blender exits 0 on a Python error)
    import hashlib
    open(os.path.join(out, 'OK'), 'w').write(hashlib.sha256(open(os.path.join(out, 'room.bbr'), 'rb').read()).hexdigest() + '\n')
    print('ROOM-BUILD-OK', flush=True)


if __name__ == '__main__':
    main()
