"""BBR v3 writer: the room's geometry, one atlas per texture set and one colour stream per room style.
Format: tools/room/source/SPEC.md, "BBR v3". Pure Python (+ the scene's mathutils vectors).

The atlas has no hardware wrap per texture, so every polygon is cut on its texture's repeat lines (u, v integers)
first; each piece then maps into its texture's rectangle. Positions become BBR millimetres (x right, y up, z into the
room from the bookcase front) and triangles wind so (b - a) x (c - a) points out of the face.
"""
import struct

ATLAS_W, ATLAS_H = 256, 128
GRID = 8                                      # packing cell (texels)
EPS = 1.0 / 16                                # a sixteenth of a texel in from a texture's edges (uv are texels x 16)
FRONT = 1.58

HDR = '<4sHHHHHHII8s'
TEX = '<8sHHHHII'
PART = '<12sBBBB12sII3f'
STYLE = '<8sIIHHHH'   # id, colours, probes, nProbe, atlas, light range (0 = 2.0), pad
FMT = {'runtime': 0, 'rgba5551': 1}
MODE = {'opaque': 0, 'blend': 1, 'add': 2}


def to_bbr(p):
    return (p[0] * 1000.0, p[2] * 1000.0, (p[1] - FRONT) * 1000.0)


def i16(x):
    v = int(round(x))
    assert -32768 <= v <= 32767, x
    return v


# ---------------------------------------------------------------- atlas

def pack(sizes):
    """{name: (w, h)} -> {name: (x, y)} in ATLAS_W x ATLAS_H: largest first, each at the lowest then leftmost free
    spot of an 8-texel grid. Deterministic (ties by name)."""
    cw, ch = ATLAS_W // GRID, ATLAS_H // GRID
    used = [[False] * cw for _ in range(ch)]
    out = {}
    for name, (w, h) in sorted(sizes.items(), key=lambda kv: (-kv[1][0] * kv[1][1], -kv[1][1], kv[0])):
        gw, gh = -(-w // GRID), -(-h // GRID)
        spot = None
        for gy in range(ch - gh + 1):
            for gx in range(cw - gw + 1):
                if all(not used[gy + j][gx + i] for j in range(gh) for i in range(gw)):
                    spot = (gx, gy)
                    break
            if spot:
                break
        if not spot:
            raise SystemExit('atlas: no room for %s (%dx%d) in %dx%d' % (name, w, h, ATLAS_W, ATLAS_H))
        for j in range(gh):
            for i in range(gw):
                used[spot[1] + j][spot[0] + i] = True
        out[name] = (spot[0] * GRID, spot[1] * GRID)
    return out


def atlas_image(tex, layout):
    """RGBA rows (top first) of the atlas: list of lists of (r, g, b, a)."""
    img = [[(0, 0, 0, 0)] * ATLAS_W for _ in range(ATLAS_H)]
    for name, (x0, y0) in layout.items():
        a = tex[name]
        h, w = a.shape[:2]
        for y in range(h):
            row = img[y0 + y]
            for x in range(w):
                row[x0 + x] = tuple(int(c) for c in a[y, x])
    return img


def rgba5551(img):
    def q(c):
        return (c * 31 + 127) // 255
    out = bytearray()
    for row in img:
        for r, g, b, a in row:
            out += struct.pack('<H', q(r) << 11 | q(g) << 6 | q(b) << 1 | (1 if a >= 128 else 0))
    return bytes(out)


# ---------------------------------------------------------------- cutting polygons on texture repeats

def _clip(poly, axis, k, keep_below):
    """Sutherland-Hodgman: the part of poly (corners (pos, uv, nrm)) with uv[axis] <= k (or >= k)."""
    out = []
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        da, db = a[1][axis] - k, b[1][axis] - k
        ina, inb = (da <= 1e-9) if keep_below else (da >= -1e-9), (db <= 1e-9) if keep_below else (db >= -1e-9)
        if ina:
            out.append(a)
        if ina != inb:
            t = da / (da - db)
            out.append((a[0].lerp(b[0], t), tuple(a[1][j] + (b[1][j] - a[1][j]) * t for j in (0, 1)),
                        a[2].lerp(b[2], t).normalized()))
    return out if len(out) >= 3 else []


def split(poly):
    """Pieces of a polygon, each inside one repeat of its texture: [(corners, (ou, ov))], uv minus (ou, ov) in 0..1."""
    import math
    pieces = [poly]
    for axis in (0, 1):
        nxt = []
        for p in pieces:
            lo = min(c[1][axis] for c in p)
            hi = max(c[1][axis] for c in p)
            k = math.floor(lo + 1e-6) + 1
            rest = p
            while k < hi - 1e-6 and rest:
                below = _clip(rest, axis, k, True)
                if below:
                    nxt.append(below)
                rest = _clip(rest, axis, k, False)
                k += 1
            if rest:
                nxt.append(rest)
        pieces = nxt
    out = []
    for p in pieces:
        cu = sum(c[1][0] for c in p) / len(p)
        cv = sum(c[1][1] for c in p) / len(p)
        out.append((p, (math.floor(cu), math.floor(cv))))
    return out


# ---------------------------------------------------------------- the file

class Mesh:
    """Vertices shared per (group, slot, BBR position, texel uv, light key); triangles per BBR part."""

    def __init__(self):
        self.verts = []        # (pos mm (3), w, u16, v16, key) ; key -> colour per style
        self.index = {}
        self.tris = {}         # group -> [i, i, i, ...]

    def vertex(self, pos, w, uv, key, alpha=255):
        k = (tuple(i16(x) for x in pos), w, uv, key, alpha)
        if k not in self.index:
            self.index[k] = len(self.verts)
            self.verts.append(k)
        return self.index[k]


def write(path, groups, mesh, textures, anchors, styles, default_style):
    """groups: [(name, mode, tex, flags, group, pivot)] in draw order (mesh.tris keys); textures: [(name, w, h, fmt,
    bytes|None)]; styles: [(id, colours [(r, g, b, a) per vertex], probes [(pos mm, cube)], atlas tex index)]."""
    nv = len(mesh.verts)
    assert nv < 65536, nv
    idx, parts = [], []
    for (name, mode, tex, flags, group, pivot) in groups:
        t = mesh.tris.get(name, [])
        parts.append((name, mode, tex, flags, group, len(idx), len(t), pivot))
        idx += t
    tex_rows = len(textures) * 24
    head = 32 + tex_rows + 48 * len(parts) + 56 * len(anchors) + 24 * len(styles)
    vbytes = bytearray()
    first = styles[0][1]
    for i, (pos, w, uv, key, alpha) in enumerate(mesh.verts):
        r, g, b, a = first[i]
        vbytes += struct.pack('<4h2h4B', pos[0], pos[1], pos[2], w, uv[0], uv[1], r, g, b, a)
    ibytes = struct.pack('<%dH' % len(idx), *idx)
    ibytes += b'\0' * (-len(ibytes) % 4)
    body = bytes(vbytes) + ibytes
    off = head + len(body)
    style_rows, style_data = [], b''
    for sid, cols, probes, atlas, lrange in styles:
        c = b''.join(struct.pack('<4B', *col) for col in cols)
        pb = b''.join(struct.pack('<3f', *pos) + bytes(sum((list(f) for f in cube), [])) + b'\0\0' for pos, cube in probes)
        style_rows.append(struct.pack(STYLE, sid.encode(), off + len(style_data), off + len(style_data) + len(c),
                                      len(probes), atlas, lrange, 0))
        style_data += c + pb
    off += len(style_data)
    trows, tdata = [], b''
    for name, w, h, fmt, data in textures:
        if data:
            trows.append(struct.pack(TEX, name.encode(), w, h, FMT[fmt], 0, off + len(tdata), len(data)))
            tdata += data + b'\0' * (-len(data) % 4)
        else:
            trows.append(struct.pack(TEX, name.encode(), w, h, 0, 0, 0, 0))
    out = struct.pack(HDR, b'BBR1', 3, len(styles), len(textures), len(parts), len(anchors), 16, nv, len(idx),
                      default_style.encode())
    out += b''.join(trows)
    out += b''.join(struct.pack(PART, n.encode(), MODE[m], t, fl, 0, g.encode(), f, c, *pv) for n, m, t, fl, g, f, c, pv in parts)
    for name, vals in anchors.items():
        vals = list(vals) + [0.0] * (10 - len(vals))
        assert len(vals) == 10 and len(name) < 16, name
        out += struct.pack('<16s10f', name.encode(), *vals)
    out += b''.join(style_rows)
    assert len(out) == head
    out += body + style_data + tdata
    open(path, 'wb').write(out)
    return dict(bytes=len(out), vertices=nv, triangles=len(idx) // 3, parts={p[0]: p[6] // 3 for p in parts},
                textures=len(tdata), styles=[s[0] for s in styles])
