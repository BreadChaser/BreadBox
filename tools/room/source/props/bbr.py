"""BBR1: the compact room file (one per room style). See room3d/SPEC.md for the layout."""
import struct

import numpy as np

import pix

ORDER = ['static', 'p_tv', 'p_lib', 'p_set', 'p_tog', 'p_share',
         'a_clockh', 'a_clockm', 'a_knob', 'a_ctrl0', 'a_ctrl1', 'a_card', 'tv_screen',
         'hand', 'hand_sleeve', 'hand_mitt',          # bare hand = hand..sleeve, mitten = sleeve..mitt: one range either way
         'ps1_tray', 'ps1_lid', 'ps1_disc', 'gba_box', 'gba_flap', 'gba_cart',
         'shadow', 'rays', 'glow', 'dust', 'motes', 'steam', 'trace']
MODE = {'opaque': 0, 'alpha': 1, 'add': 2}
F_BILL, F_PIVOT, F_PLACE, F_UNLIT, F_LMAP = 1, 2, 4, 8, 16
FMT = {'rgba5551': 1, 'rgb565': 2, 'rgba4444': 3, 'runtime': 0}
UVS = 16          # uv stored as texel * 16 in a signed short
# Mesh vertices carry a palette index (matrix + tint slot) in the 4th position short, so the whole
# room (static, the five places and the moving pieces) is ONE draw call; see SPEC.md.
PALETTE = ['static', 'p_tv', 'p_lib', 'p_set', 'p_tog', 'p_share', 'a_clockh', 'a_clockm', 'a_knob', 'a_ctrl0', 'a_ctrl1', 'a_card']
HAND = {'hand': 1, 'hand_sleeve': 0, 'hand_mitt': 1}     # bone slots inside the hand draw (17 per hand; per-vertex SL wins)
SPRITE = {'rays': 0, 'glow': 0, 'dust': 1, 'steam': 2, 'trace': 3, 'motes': 4}   # round 5: motes in the window beam


def _name(s, n):
    b = s.encode()[:n]
    return b + b'\0' * (n - len(b))


def fix_winding(p):
    """Every triangle is wound so (b-a) x (c-a) agrees with its vertex normals (front = that side)."""
    out = []
    for (a, b, c) in p.T:
        P = p.P
        g = np.cross(P[b] - P[a], P[c] - P[a])
        n = p.N[a] + p.N[b] + p.N[c]
        out.append((a, c, b) if np.dot(g, n) < 0 else (a, b, c))
    p.T = out


def pack(parts, textures, anchors, style, probes=()):
    """textures: list of (name, w, h, fmt, bytes or None). anchors: {name: [floats]}."""
    names = [n for n in ORDER if n in parts and parts[n].T] + sorted(n for n in parts if n not in ORDER and parts[n].T)
    tex_index = {t[0]: i for i, t in enumerate(textures)}
    V, I, PT = [], [], []
    for n in names:
        p = parts[n]
        if p.mode != 'add':
            fix_winding(p)
        base = len(V)
        first = len(I)
        for i in range(len(p.P)):
            x, y, z = np.round(p.P[i]).astype(int)
            u, v = p.UV[i]
            c = np.asarray(p.C[i], float)
            if p.mode == 'add':
                rgb = np.clip(np.round(c[:3] * 255), 0, 255)
                a = p.K[i] | (SPRITE.get(n, 0) << 2)   # billboard corner 0..3, animation in bits 2-4
                size = int(round(p.S[i]))
            else:
                rgb = np.clip(np.round(c[:3] * 128), 0, 255)   # 128 = 1.0 (PS1 style, up to 2x)
                a = int(np.clip(round((c[3] if len(c) > 3 else 1.0) * 255), 0, 255))
                size = PALETTE.index(n) if n in PALETTE else PALETTE.index(p.group) if p.group in PALETTE else 0
                if n.startswith(('ps1_', 'gba_')):
                    size = 0
                if n in HAND:
                    size = HAND[n]
                if getattr(p, 'SL', None):
                    size = p.SL[i]                     # per-vertex slot (the rigged hand: strongest bone)
            u2, v2 = p.UV2[i] if getattr(p, 'UV2', None) is not None else (1.0, 1.0)     # round 4: lightmap uv (unlit: the neutral texel)
            V.append(struct.pack('<hhhhhhBBBBhh', int(x), int(y), int(z), size, int(round(u * UVS)), int(round(v * UVS)), *map(int, rgb), int(a),
                                 int(round(u2 * UVS)), int(round(v2 * UVS))))
        for t in p.T:
            I.extend(base + k for k in t)
        flags = (F_BILL if p.mode == 'add' else 0) | (F_PIVOT if n.startswith(('a_', 'ps1_', 'gba_')) or n in HAND else 0) | \
            (F_PLACE if n.startswith('p_') or p.group else 0) | (0 if p.lit else F_UNLIT) | (F_LMAP if getattr(p, 'UV2', None) is not None else 0)
        grp = (p.group or n)
        PT.append((n, MODE[p.mode], tex_index[p.tex], flags, grp, first, len(I) - first, p.pivot))
    assert len(V) < 65536, len(V)
    hdr_size = 32
    tex_tab = 24 * len(textures)
    part_tab = 48 * len(PT)
    anc_tab = 16 + 4 * 10
    off = hdr_size + tex_tab + part_tab + anc_tab * len(anchors) + 32 * len(probes)
    vb = b''.join(V)
    ib = struct.pack(f'<{len(I)}H', *I)
    off_v = off
    off_i = off_v + len(vb)
    off_t = off_i + len(ib)
    off_t += (-off_t) % 4
    out = bytearray()
    out += b'BBR1' + struct.pack('<HHHHHHII', 2, len(probes), len(textures), len(PT), len(anchors), 20, len(V), len(I)) + _name(style, 8)
    assert len(out) == hdr_size
    tcur = off_t
    blobs = []
    for (n, w, h, fmt, data) in textures:
        size = len(data) if data else 0
        out += _name(n, 8) + struct.pack('<HHHHII', w, h, FMT[fmt], 0, tcur if data else 0, size)
        if data:
            blobs.append(data)
            tcur += size + ((-size) % 4)
    for (n, mode, ti, flags, grp, first, cnt, piv) in PT:
        px, py, pz = (float(v) for v in piv)
        out += _name(n, 12) + struct.pack('<BBBB', mode, ti, flags, 0) + _name(grp, 12) + struct.pack('<II', first, cnt) + struct.pack('<fff', px, py, pz)
    for n, vals in anchors.items():
        vals = list(vals) + [0.0] * (10 - len(vals))
        out += _name(n, 16) + struct.pack('<10f', *vals[:10])
    for pos, cols in probes:                         # round 4: light probes (ambient cube, light / 2 in 0..255)
        out += struct.pack('<3f', *map(float, pos)) + bytes(int(np.clip(round(c * 255), 0, 255)) for c in np.asarray(cols).ravel()) + b'\0\0'
    assert len(out) == off_v, (len(out), off_v)
    out += vb + ib
    out += b'\0' * ((-len(out)) % 4)
    assert len(out) == off_t
    for b in blobs:
        out += b + b'\0' * ((-len(b)) % 4)
    stats = dict(verts=len(V), tris=len(I) // 3, vbytes=len(vb), ibytes=len(ib),
                 texbytes=sum(len(b) for b in blobs), file=len(out),
                 parts={n: dict(tris=cnt // 3, mode=mode, tex=ti) for (n, mode, ti, flags, grp, first, cnt, piv) in PT})
    return bytes(out), stats


def tex_bytes(im, fmt):
    a = {'rgba5551': pix.to_5551, 'rgb565': pix.to_565, 'rgba4444': pix.to_4444}[fmt](im)
    return a.astype('<u2').tobytes()


def anchors_from(A):
    """Flatten the room anchors into named float rows (see SPEC.md)."""
    out = {}
    for k, p in A['poses'].items():
        out['pose.' + k] = [*p['pos'], *p['look'], p['F'], p['eye'], p['conv']]
    for k, v in A['focus'].items():
        out['focus.' + k] = list(v)
    out['screen'] = list(A['screen']) + [1.0 if A['tv_kind'] == 'flat' else 0.0]
    for k in ('scope', 'slabel', 'knob', 'scope_box'):
        out[k] = list(A[k])
    c = A['card']
    out['card'] = [*c['c'], c['yaw'], c['pitch'], c['w'], c['h']]
    out['clock'] = list(A['clock'])
    out['mug'] = list(A['mug'])
    for i, p in enumerate(A['ctrl']):
        out[f'ctrl{i}'] = list(p)
    if 'hand' in A:
        out['hand'] = list(A['hand'])
    if 'hand2' in A:
        out['hand2'] = list(A['hand2'])
    if 'hand3' in A:
        out['hand3'] = list(A['hand3'])
    for k in sorted(A):
        if k.startswith('hb'):
            out[k] = list(A[k])                     # the hand's bones (round 3e): head, flex axis, spread axis, parent
    for k in ('poster', 'board'):                  # room walls
        if k in A:
            out[k] = list(A[k])
    cs = A['case']
    out['case'] = [cs['x0'], cs['bay'], cs['nbay'], cs['gap'], cs['rows'], cs['top'], cs['depth'], cs['upr'], cs['board'], cs.get('upc', cs['upr'])]
    return out
