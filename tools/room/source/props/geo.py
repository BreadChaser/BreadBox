"""Low-poly geometry builder for the R10 room (units: mm; x right, y up, z into the room).

A Part is one submesh of the export: one texture, one blend mode, optionally its own pivot
(animated parts get a model matrix at runtime; everything else is static and pre-lit).
Vertices carry: position, atlas texel uv, a base colour (albedo tint, 0..1), a normal for the
light bake, an emissive flag (self-lit, the bake keeps its colour) and, for billboards,
a size in mm and a corner index 0..3.
"""
import math

import numpy as np


def v3(*a):
    return np.array(a if len(a) == 3 else a[0], dtype=float)


def nrm(v):
    v = np.asarray(v, float)
    l = np.linalg.norm(v)
    return v / l if l > 1e-9 else v


def rot_y(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


class Part:
    OBJ = 'room'                            # which model new triangles belong to (for .blend / turntables)

    def __init__(self, name, mode='opaque', tex='atlas', group=None, pivot=None, lit=True):
        self.name, self.mode, self.tex, self.group = name, mode, tex, group
        self.pivot = v3(pivot) if pivot is not None else v3(0, 0, 0)
        self.lit = lit                      # False: vertex colours are used as they are (glows)
        self.P, self.UV, self.C, self.N, self.E, self.S, self.K = [], [], [], [], [], [], []
        self.T = []
        self.cast = []                      # per triangle: does it block light in the bake
        self.obj = []                       # per triangle: model name

    def vert(self, p, uv, col, n, emis=0.0, size=0, corner=0):
        self.P.append(np.asarray(p, float))
        self.UV.append(uv)
        self.C.append(np.asarray(col, float))
        self.N.append(np.asarray(n, float))
        self.E.append(emis)
        self.S.append(size)
        self.K.append(corner)
        return len(self.P) - 1

    def tri(self, a, b, c, cast=True):
        self.T.append((a, b, c))
        self.cast.append(cast)
        self.obj.append(Part.OBJ)

    @property
    def ntri(self):
        return len(self.T)


class Builder:
    """Holds the atlas (for uv lookups) and all parts. Shapes pick a part by name."""

    def __init__(self, atlas):
        self.atlas = atlas
        self.parts = {}
        self.cur = None
        self.xf = None              # optional (matrix, offset) applied to every new vertex
        self.cast = True            # new triangles block light in the bake

    def part(self, name, **kw):
        if name not in self.parts:
            self.parts[name] = Part(name, **kw)
        self.cur = self.parts[name]
        return self.cur

    # ---- transforms for building objects in local space
    def push(self, M=None, o=(0, 0, 0)):
        prev = self.xf
        M = np.eye(3) if M is None else M
        o = v3(o)
        if prev is not None:
            M, o = prev[0] @ M, prev[0] @ o + prev[1]
        self.xf = (M, o)
        return prev

    def pop(self, prev):
        self.xf = prev

    def _p(self, p):
        p = v3(p)
        return p if self.xf is None else self.xf[0] @ p + self.xf[1]

    def _n(self, n):
        n = v3(n)
        return nrm(n if self.xf is None else self.xf[0] @ n)

    def _uv(self, reg, u, v):
        if reg is None:
            return (u, v)
        return self.atlas.uv(reg, u, v)

    # ---- primitives
    def quad(self, W, reg='white', col=(1, 1, 1), uv=(0, 0, 1, 1), nu=1, nv=1, emis=0.0, n=None, both=False):
        """W = 4 corners TL, TR, BR, BL (front face winding seen from the front). uv = u0, v0, u1, v1."""
        W = [self._p(w) for w in W]
        if n is None:
            n = nrm(np.cross(W[1] - W[0], W[3] - W[0]))
        else:
            n = self._n(n)
        part = self.cur
        u0, v0, u1, v1 = uv
        idx = {}
        for j in range(nv + 1):
            for i in range(nu + 1):
                s, t = i / nu, j / nv
                p = (W[0] * (1 - s) + W[1] * s) * (1 - t) + (W[3] * (1 - s) + W[2] * s) * t
                idx[i, j] = part.vert(p, self._uv(reg, u0 + (u1 - u0) * s, v0 + (v1 - v0) * t), col, n, emis)
        for j in range(nv):
            for i in range(nu):
                a, b, c, d = idx[i, j], idx[i + 1, j], idx[i + 1, j + 1], idx[i, j + 1]
                part.tri(a, b, c, self.cast)
                part.tri(a, c, d, self.cast)
        if both:
            self.quad([W[1], W[0], W[3], W[2]], reg, col, (u1, v0, u0, v1), nu, nv, emis)

    def tiled(self, W, reg, cells_u, cells_v, col=(1, 1, 1), sub=1, emis=0.0, jitter=0.0, seed=0, skip=None):
        """A plane made of cells_u x cells_v cells; every cell shows the whole tile region
        (atlas textures can't repeat). sub = light-bake subdivisions per cell."""
        W = [v3(w) for w in W]
        for j in range(cells_v):
            for i in range(cells_u):
                def at(s, t):
                    return (W[0] * (1 - s) + W[1] * s) * (1 - t) + (W[3] * (1 - s) + W[2] * s) * t
                s0, s1, t0, t1 = i / cells_u, (i + 1) / cells_u, j / cells_v, (j + 1) / cells_v
                if skip and skip(at(s0, t0), at(s1, t1)):
                    continue
                k = 1 + jitter * (math.sin(i * 12.9 + j * 78.2 + seed) * 0.5)
                self.quad([at(s0, t0), at(s1, t0), at(s1, t1), at(s0, t1)], reg,
                          np.asarray(col) * k, (0, 0, 1, 1), sub, sub, emis)

    def box(self, x0, x1, y0, y1, z0, z1, mats, skip=(), nu=1, nv=1):
        """Axis box. mats: {face: (reg, col, uv)} with faces front(-z) back(+z) left(-x) right(+x)
        top bottom; 'all' is the default. A face mapped to None is left out."""
        F = {
            'front': [(x0, y1, z0), (x1, y1, z0), (x1, y0, z0), (x0, y0, z0)],
            'back': [(x1, y1, z1), (x0, y1, z1), (x0, y0, z1), (x1, y0, z1)],
            'left': [(x0, y1, z1), (x0, y1, z0), (x0, y0, z0), (x0, y0, z1)],
            'right': [(x1, y1, z0), (x1, y1, z1), (x1, y0, z1), (x1, y0, z0)],
            'top': [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)],
            'bottom': [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)],
        }
        for k, W in F.items():
            if k in skip:
                continue
            m = mats.get(k, mats.get('all'))
            if m is None:
                continue
            reg, col = m[0], m[1]
            uv = m[2] if len(m) > 2 else (0, 0, 1, 1)
            emis = m[3] if len(m) > 3 else 0.0
            self.quad(W, reg, col, uv, nu if k not in ('top', 'bottom') else nu, nv, emis)

    def prism(self, poly, y0, y1, side, top=None, bottom=None, nseg_uv=None):
        """Vertical extrusion of a convex/simple polygon (list of (x, z), counter-clockwise seen
        from above). side = (reg, col); top/bottom = (reg, col) mapped by bounding box."""
        n = len(poly)
        per = [0]
        for i in range(n):
            a, b = poly[i], poly[(i + 1) % n]
            per.append(per[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
        for i in range(n):
            a, b = poly[i], poly[(i + 1) % n]
            u0, u1 = per[i] / per[-1], per[i + 1] / per[-1]
            self.quad([(a[0], y1, a[1]), (b[0], y1, b[1]), (b[0], y0, b[1]), (a[0], y0, a[1])],
                      side[0], side[1], (u0, 0, u1, 1))
        xs, zs = [p[0] for p in poly], [p[1] for p in poly]
        for cap, y, up in ((top, y1, True), (bottom, y0, False)):
            if cap is None:
                continue
            self.fan([(x, y, z) for x, z in poly], cap[0], cap[1],
                     lambda p: ((p[0] - min(xs)) / (max(xs) - min(xs) + 1e-9), (p[2] - min(zs)) / (max(zs) - min(zs) + 1e-9)),
                     n=(0, 1 if up else -1, 0))

    def fan(self, pts, reg, col, uvf, emis=0.0, center=None, n=None):
        pts = [self._p(p) for p in pts]
        c = np.mean(pts, axis=0) if center is None else self._p(center)
        n = nrm(np.cross(pts[1] - c, pts[0] - c)) if n is None else self._n(n)
        part = self.cur
        inv = None if self.xf is None else np.linalg.inv(self.xf[0])

        def uvl(p):
            q = p if inv is None else inv @ (p - self.xf[1])
            return self._uv(reg, *uvf(q))
        ci = part.vert(c, uvl(c), col, n, emis)
        ids = [part.vert(p, uvl(p), col, n, emis) for p in pts]
        for i in range(len(ids)):
            part.tri(ci, ids[(i + 1) % len(ids)], ids[i])

    def lathe(self, cx, cz, prof, seg, reg, col, cap=None, smooth=True, u_rep=1.0, squash=1.0, phase=0.0):
        """Surface of revolution. prof = [(r, y), ...] from bottom to top. cap = (reg, col) on top.
        squash scales z (ovals). Smooth normals (Gouraud)."""
        part = self.cur
        rings = []
        for j, (r, y) in enumerate(prof):
            if j == 0:
                dr, dy = prof[1][0] - r, prof[1][1] - y
            elif j == len(prof) - 1:
                dr, dy = r - prof[j - 1][0], y - prof[j - 1][1]
            else:
                dr, dy = prof[j + 1][0] - prof[j - 1][0], prof[j + 1][1] - prof[j - 1][1]
            ring = []
            for i in range(seg + 1):
                a = phase + i / seg * 2 * math.pi
                ca, sa = math.cos(a), math.sin(a)
                p = (cx + r * ca, y, cz + r * sa * squash)
                n = (dy * ca, -dr, dy * sa / max(squash, 1e-3)) if smooth else (ca, 0, sa)
                ring.append(part.vert(self._p(p), self._uv(reg, i / seg * u_rep % 1.0001, 1 - j / (len(prof) - 1)), col, self._n(n)))
            rings.append(ring)
        for j in range(len(prof) - 1):
            for i in range(seg):
                a, b, c, d = rings[j][i], rings[j][i + 1], rings[j + 1][i + 1], rings[j + 1][i]
                part.tri(a, c, b)
                part.tri(a, d, c)
        if cap:
            r, y = prof[-1]
            pts = [(cx + r * math.cos(phase - i / seg * 2 * math.pi), y, cz + r * math.sin(phase - i / seg * 2 * math.pi) * squash) for i in range(seg)]
            self.fan(pts, cap[0], cap[1], lambda p: (0.5 + (p[0] - cx) / (2 * r + 1e-9), 0.5 + (p[2] - cz) / (2 * r * squash + 1e-9)),
                     center=(cx, y, cz), n=(0, 1, 0))

    def cyl(self, cx, cz, r, y0, y1, seg, reg, col, cap=None, bottom=None):
        self.lathe(cx, cz, [(r, y0), (r, y1)], seg, reg, col, cap, smooth=True)
        if bottom:
            pts = [(cx + r * math.cos(i / seg * 2 * math.pi), y0, cz + r * math.sin(i / seg * 2 * math.pi)) for i in range(seg)]
            self.fan(pts, bottom[0], bottom[1], lambda p: (0.5, 0.5), center=(cx, y0, cz), n=(0, -1, 0))

    def strip(self, pts, width, reg, col, up=(0, 1, 0), emis=0.0):
        """A ribbon through pts, width across `up` x direction (wires, cables, steam)."""
        up = v3(up)
        L = len(pts)
        for i in range(L - 1):
            a, b = v3(pts[i]), v3(pts[i + 1])
            d = nrm(b - a)
            s = nrm(np.cross(d, up)) * width / 2
            if np.linalg.norm(s) < 1e-6:
                s = v3(width / 2, 0, 0)
            self.quad([a - s, a + s, b + s, b - s], reg, col, (0, i / (L - 1), 1, (i + 1) / (L - 1)), emis=emis, n=up)

    def bill(self, p, size, reg, col):
        """Camera-facing sprite (the vertex shader expands it): 4 vertices at the centre."""
        part = self.cur
        p = self._p(p)
        ids = [part.vert(p, self._uv(reg, u, v), col, (0, 0, -1), 1.0, size, k)
               for k, (u, v) in enumerate(((0, 0), (1, 0), (1, 1), (0, 1)))]
        part.tri(ids[0], ids[1], ids[2])
        part.tri(ids[0], ids[2], ids[3])

    def floor_blob(self, x, z, rx, rz, y, a=0.5, reg='blob'):
        self.quad([(x - rx, y, z + rz), (x + rx, y, z + rz), (x + rx, y, z - rz), (x - rx, y, z - rz)], reg, (0, 0, 0, a), n=(0, 1, 0))
