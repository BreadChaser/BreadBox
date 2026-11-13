#!/usr/bin/env python3
"""Pack the hands: hands.bbh (BBH1 v3, see tools/room/source/SPEC.md and the UI mock's room3d/SPEC.md "BBH1").

    pack-hands.py --bake hands-bake.json --hand hands/ --out DIR [--textures rustic] [--credits CREDITS.md] [--props props.bbr]

1. Reads the sampled animation (--bake, committed as hands-bake.json): every clip at 30 Hz, from the UI mock's
   simulation (camera, held objects, fade, per hand the palm and sleeve matrices + 15 joint angles, with the
   contact check in mm). Making that file needs the mock and one headless Chromium: `--rebake` (below).
2. Takes the hand mesh (one hand: body, sleeve, mitten), its 17-bone rig and anchors from hands/hand.json and its
   texture(s) from hands/hand-<name>.png (512 x 256, RGBA5551 values): the standalone hand source, recorded from the
   retired legacy room builder. The app uses one texture (the room's sleeve), the first.
3. Writes DIR/hands.bbh and MANIFEST.json (sizes, SHA256, clips, contact numbers); copies props.bbr/CREDITS.md when given.
   Pure Python, no Pillow, no Blender: the same bake and hand source always give the same bytes.

    pack-hands.py --rebake MOCK_DIR --bake NEW.json   runs the mock's bake (Chromium) and stores the sampled clips
"""
import argparse, hashlib, html, json, pathlib, re, shutil, struct, subprocess, tempfile

PARTS = ['hand', 'hand_sleeve', 'hand_mitt']
NB = 17
SPACE = {'slot': 0, 'card': 1, 'camera': 2}
BOX = {'': 0, 'ps1': 1, 'gba': 2, 'ps1p': 3, 'ps1d': 4}
OBJ = ['case', 'lid', 'flap', 'cart', 'card']          # mask bits 2..6 (bit 0 right hand, bit 1 left hand)
ROT_Q, POS_Q, ANG_Q = 16384.0, 4.0, 8192.0               # rotation x16384, translation 1/4 mm, angles x8192 (rad)
HDR, TEXE, PARTE, BONEE, ANCE, CLIPE = 48, 16, 32, 40, 40, 48


def bake(root):
    root = pathlib.Path(root).resolve()
    url = (root / 'index.html').as_uri() + '#bb10=1&bbbake=1&panel=0'
    with tempfile.TemporaryDirectory(prefix='bb10-bake-', dir=root / 'shots') as t:
        r = subprocess.run(['/usr/lib/chromium/chromium', '--headless=new', '--no-sandbox', '--disable-extensions',
                            '--disable-background-networking', '--mute-audio', '--renderer-process-limit=1',
                            f'--user-data-dir={t}/p', '--virtual-time-budget=120000', '--dump-dom', url],
                           capture_output=True, timeout=1500)
    m = re.search(rb'<pre id="bake">([^<]+)</pre>', r.stdout)
    if not m:
        err = re.search(rb'JS error:[^<]*', r.stdout)
        raise RuntimeError((err[0] if err else r.stderr[-1500:]).decode(errors='replace'))
    return json.loads(html.unescape(m[1].decode()))


def read_png(path):
    """RGBA8 PNG (any filter) -> RGBA5551 bytes, little endian, rows top first (the values must be 5551's)."""
    import zlib
    d = pathlib.Path(path).read_bytes()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', path
    o, idat, w = 8, b'', 0
    while o < len(d):
        n, kind = struct.unpack_from('>I4s', d, o)
        body = d[o + 8:o + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype = struct.unpack_from('>IIBB', body)
            assert depth == 8 and ctype == 6, '%s: RGBA8 only' % path
        elif kind == b'IDAT':
            idat += body
        o += 12 + n
    raw, bpp, stride = zlib.decompress(idat), 4, w * 4
    prev, out, i = bytearray(stride), bytearray(), 0
    for _ in range(h):
        f, row = raw[i], bytearray(raw[i + 1:i + 1 + stride]); i += 1 + stride
        for x in range(stride):
            a = row[x - bpp] if x >= bpp else 0
            b, c = prev[x], prev[x - bpp] if x >= bpp else 0
            pr = a + b - c
            pred = {0: 0, 1: a, 2: b, 3: (a + b) // 2, 4: a if abs(pr - a) <= abs(pr - b) and abs(pr - a) <= abs(pr - c) else b if abs(pr - b) <= abs(pr - c) else c}[f]
            row[x] = (row[x] + pred) & 255
        for x in range(w):
            r, g, bl, al = row[4 * x:4 * x + 4]
            px = (r >> 3) << 11 | (g >> 3) << 6 | (bl >> 3) << 1 | (1 if al >= 128 else 0)
            assert ((r >> 3) << 3 | r >> 5) == r and al in (0, 255), '%s: not an RGBA5551 picture' % path
            out += struct.pack('<H', px)
        prev = row
    return w, h, bytes(out)


def q16(v, k):
    return max(-32768, min(32767, int(round(v * k))))


def m34(m):
    """3x4 row-major (r00 r01 r02 tx ...) -> 12 s16"""
    return [q16(m[r * 4 + c], ROT_Q if c < 3 else POS_Q) for r in range(3) for c in range(4)]


I34 = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0]
GLOVE_OFF = [(0, 0), (128, 0), (256, 0), (0, 128), (128, 128), (256, 128)]   # bare, knit, leather, fingerless, work, mittens (hand.py UTILE)


def hand_block(h):
    if not h:
        return struct.pack('<54h', *([0] * 54))
    return struct.pack('<12h', *m34(h['palm'])) + struct.pack('<12h', *m34(h['sleeve'])) + \
        struct.pack('<30h', *[q16(a, ANG_Q) for a in h['j']])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--bake', required=True, help='the sampled clips (JSON)')
    ap.add_argument('--hand', help='the hand source: hand.json and hand-<name>.png')
    ap.add_argument('--textures', default='room', help='the hand textures to pack, in order (the app uses the first)')
    ap.add_argument('--out', help='output directory')
    ap.add_argument('--credits'), ap.add_argument('--props')
    ap.add_argument('--rebake', metavar='MOCK_DIR', help='run the UI mock bake (Chromium) and write --bake')
    opt = ap.parse_args()
    if opt.rebake:
        B = bake(opt.rebake)
        pathlib.Path(opt.bake).write_text(json.dumps(B, separators=(',', ':')))
        print('bake:', len(B['clips']), 'clips ->', opt.bake)
        return
    if not (opt.hand and opt.out):
        ap.error('--hand and --out are needed')
    OUT = pathlib.Path(opt.out)
    OUT.mkdir(parents=True, exist_ok=True)
    B = json.loads(pathlib.Path(opt.bake).read_text())
    H = json.loads((pathlib.Path(opt.hand) / 'hand.json').read_text())
    anchors = H['anchors']
    hvb = b''.join(struct.pack('<4h2h4B', *v) for v in H['vertices'])
    hib = H['indices']
    hparts = [(n, f0, cnt, tuple(piv)) for n, f0, cnt, piv in H['parts']]
    assert [p[0] for p in hparts] == PARTS
    parts = {n: dict(first=f0, count=cnt) for n, f0, cnt, _ in hparts}
    first, last = 0, hparts[-1][1] + hparts[-1][2]
    STYLES = opt.textures.split(',')
    htex = []
    for name in STYLES:
        w, h, data = read_png(pathlib.Path(opt.hand) / ('hand-%s.png' % name[:8]))
        assert (w, h) == (512, 256)
        htex.append(dict(data=data))
    # bones: 0 sleeve, 1 palm (roots), 2..16 from the anchors hb2..hb16
    bones = b''
    for k in range(NB):
        if k < 2:
            bones += struct.pack('<9fb3x', *([0.0] * 9), -1)
        else:
            a = anchors['hb%d' % k]
            bones += struct.pack('<9fb3x', *a[:9], int(round(a[9])))
    anc = b''.join(n.encode().ljust(8, b'\0')[:8] + struct.pack('<8f', *anchors[n][:8]) for n in ('hand', 'hand2', 'hand3'))
    assert len(anc) == 3 * ANCE
    gloves = b''.join(struct.pack('<hh', *g) for g in GLOVE_OFF)       # v3: texel offset per glove tile
    # clips
    cliptab, clipdata = [], []
    for c in B['clips']:
        fr = c['frames']
        mask = (1 if any(f['R'] for f in fr) else 0) | (2 if any(f['L'] for f in fr) else 0)
        for f in fr:
            for k in f['obj']:
                mask |= 4 << OBJ.index(k)
        body = bytearray()
        for f in fr:
            body += struct.pack('<BBH', (1 if f['R'] else 0) | (2 if f['L'] else 0), int(round(f['fade'] * 255)), 0)
            body += struct.pack('<7hh', *[q16(v, POS_Q) for v in f['cam'][:6]], q16(f['cam'][6], 1), 0)
            if mask & 1:
                body += hand_block(f['R'])
            if mask & 2:
                body += hand_block(f['L'])
            for j, k in enumerate(OBJ):
                if mask & (4 << j):
                    body += struct.pack('<12h', *m34(f['obj'].get(k, I34)))
        fb = len(body) // max(1, len(fr))
        assert fb * len(fr) == len(body)
        name = c['name']
        assert len(name) <= 15, name                  # char[16], zero-terminated
        flags = 1 if name.endswith('_m') else 0
        cliptab.append((name, SPACE[c['space']], BOX[c['box']], c['loop'], flags, len(fr), fb, mask, c['boxTHW']))
        clipdata.append(bytes(body))
    nT, nC = len(htex), len(cliptab)
    off_tex = HDR
    off_part = off_tex + TEXE * nT
    off_bone = off_part + PARTE * len(hparts)
    off_clip = off_bone + BONEE * NB + 3 * ANCE + len(gloves)
    off_v = off_clip + CLIPE * nC
    off_i = off_v + len(hvb)
    off_t = off_i + 2 * len(hib)
    off_t += (-off_t) % 4
    tex_off, cur = [], off_t
    for t in htex:
        tex_off.append(cur)
        cur += len(t['data'])
    clip_off = []
    for d in clipdata:
        clip_off.append(cur)
        cur += len(d) + (-len(d)) % 4
    out = bytearray(b'BBH1')
    out += struct.pack('<HHHHHH', 3, B['fps'], nT, len(hparts), NB, nC)
    out += struct.pack('<IIII', len(hvb) // 16, len(hib), off_v, off_i)
    out += struct.pack('<IIII', off_tex, off_part, off_bone, off_clip)
    assert len(out) == HDR, len(out)
    for s, o in zip(STYLES, tex_off):
        out += s.encode()[:8].ljust(8, b'\0') + struct.pack('<HHI', 512, 256, o)
    for n, f0, cnt, piv in hparts:
        out += n.encode()[:12].ljust(12, b'\0') + struct.pack('<II3f', f0, cnt, *piv)
    out += bones + anc + gloves
    for (n, sp, bx, lp, fl, nf, fb, mask, thw), o in zip(cliptab, clip_off):
        out += n.encode()[:16].ljust(16, b'\0') + struct.pack('<BBBBHHII3f', sp, bx, lp, fl, nf, fb, mask, o, *thw) + b'\0' * 4
    assert len(out) == off_v, (len(out), off_v)
    out += hvb + struct.pack(f'<{len(hib)}H', *hib)
    out += b'\0' * ((-len(out)) % 4)
    assert len(out) == off_t
    for t in htex:
        out += t['data']
    for d in clipdata:
        out += d + b'\0' * ((-len(d)) % 4)
    data = bytes(out)
    (OUT / 'hands.bbh').write_bytes(data)
    for src in (opt.props, opt.credits):
        if src:
            shutil.copy(src, OUT / pathlib.Path(src).name)
    rng = {n: (f0 // 3, cnt // 3) for n, f0, cnt, _ in hparts}
    used = lambda a, b: len(set(hib[a:b]))
    h0, s0, m0 = (parts[n]['first'] - first for n in PARTS)
    man = dict(format='BBH1 v3', bytes=len(data), mesh=dict(
        verts_total=len(hvb) // 16, tris={n: c for n, (_, c) in rng.items()},
        bare_draw=dict(indices=s0 - h0 + parts['hand_sleeve']['count'], verts=used(h0, s0 + parts['hand_sleeve']['count'])),
        mitten_draw=dict(indices=parts['hand_sleeve']['count'] + parts['hand_mitt']['count'], verts=used(s0, last - first)),
        bones=NB), files={}, clips=[], pen={'max_mm': max(p[1] for p in B['pen']), 'per_clip': {p[0]: p[1] for p in B['pen']}})
    for fn in ('hands.bbh', 'props.bbr', 'CREDITS.md'):
        if not (OUT / fn).exists():
            continue
        b = (OUT / fn).read_bytes()
        man['files'][fn] = dict(bytes=len(b), sha256=hashlib.sha256(b).hexdigest())
    for (n, sp, bx, lp, fl, nf, fb, mask, thw), d in zip(cliptab, clipdata):
        man['clips'].append(dict(name=n, frames=nf, seconds=round(nf / B['fps'], 2), frame_bytes=fb, bytes=len(d), loop=bool(lp),
                                 space=[k for k, v in SPACE.items() if v == sp][0], hands=('R' if mask & 1 else '') + ('L' if mask & 2 else ''),
                                 objects=[k for j, k in enumerate(OBJ) if mask & (4 << j)], max_pen_mm=man['pen']['per_clip'][n]))
    (OUT / "MANIFEST.json").write_text(json.dumps(man, indent=1))
    print('hands.bbh', len(data), 'bytes;', nC, 'clips; max penetration', man['pen']['max_mm'], 'mm;', man['mesh'])


if __name__ == '__main__':
    main()
