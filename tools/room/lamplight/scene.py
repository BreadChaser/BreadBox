"""The Lamplight room's geometry (art direction A, round 2): low-poly models, world-projected textures, walls and
floor cut into 30 cm cells so baked light lands on vertices. Geometry only: no Blender objects (gi.py bakes its light,
bbr3.py writes it). Space: metres, x right, y into the room (back wall y = 2.0), z up (Blender's world).

Made after the approved round 2 mockups (art direction A) with what the app needs added: the bookcase
built from the library's `case` contract (8 rows x 3 bays), the BreadBox poster on the left wall and the save
postcards' corkboard on the right wall (runtime pictures over them), the TV picture quad, the share card in the
hands' card size, the camera poses and anchors.
"""
import math

from mathutils import Vector

W1 = (1.0, 1.0, 1.0)
REFINE = {}                           # (part, normal, offset) -> {world axis: [line coords]}: refine.json (build.py)
D = 32.0                              # architecture texels per metre (tex.D)
FRONT = 1.58                          # the bookcase's front plane: BBR z = 0 here (bbr3.to_bbr)

# the library's case (BBR mm): first upright's centre x, bay, bays, row gap, rows, top, depth, upright, board, base
CASE = dict(x0=-735.0, bay=490.0, bays=3, gap=240.0, rows=8, depth=360.0, upr=30.0, board=26.0, base=60.0)
CASE['top'] = CASE['base'] + CASE['rows'] * CASE['gap']

# floor lamp: round 2's place (air on both sides of the shade at Home)
LAMP_X, LAMP_Y = -1.14, 1.74
SHADE_R = (0.19, 0.12, 0.13, 0.11)
RX0, RX1, RY0, RY1, RZ = -2.4, 2.4, -3.6, 2.0, 2.6
WAIN = 0.9
WIN = (1.0, 2.0, 1.05, 2.15)          # window x0 x1 z0 z1 in the back wall
RUG = (-1.2, -0.6, 1.2, 1.0, 0.012)   # x0 y0 x1 y1 top
SCOPE_LOC = Vector((1.56, 1.68, 0.76))
SCOPE_RZ = math.radians(-14)
TABLE = (0.35, 1.4, -0.12, 0.45, 0.42)   # x0 x1 y0 y1 top
CARD = dict(x=0.88, y=0.12, w=0.118, h=0.160, pitch=-0.2)   # the hands' card clips hold a 118 x 160 mm card
POSTER = dict(x=RX0, y=0.55, z=1.5, w=0.48, h=0.66)          # the BreadBox poster (left wall, picture at run time; out of the Home view)
BOARD = dict(x=RX1, y=0.25, z=1.42, w=0.86, h=0.54)          # the corkboard (right wall, postcards at run time; out of the Home view)
DESK_DZ = 0.0                         # the desk's height offset (0; build.py --sink-selftest sinks it)
WINDOW_GLOW = 0.0                     # the window bloom's size (m); 0 = none (build.py: a style's glow 'window')
CUSHIONS = {'cushion1': (-1.38, 0.7, 20), 'cushion2': (-0.62, 0.62, -10)}

# BBR parts in draw order: (name, palette slot). The room range runs from static to the end of a_card.
GROUPS = [('static', 0), ('p_tv', 1), ('p_lib', 2), ('p_set', 3), ('p_tog', 4), ('p_share', 5), ('a_card', 11)]


class Part:
    def __init__(self, name, mat, tint=None, group='static', smooth=False, unlit=False, dim=1.0, emit=0.0, cast=True):
        self.name, self.mat, self.tint, self.group = name, mat, tint, group
        self.smooth, self.unlit, self.dim, self.emit, self.cast = smooth, unlit, dim, emit, cast
        self.vcol = None              # unlit vertex colour by position (the sky's gradient): fn(style, p) -> rgb
        self.v, self.n, self.f, self.uv = [], [], [], []   # uv per face corner, in texture repeats

    def grid(self, p0, eu, ev, nu, nv, uvf, fu=None, fv=None):
        p0, eu, ev = Vector(p0), Vector(eu), Vector(ev)
        nrm = eu.cross(ev).normalized()
        fu = fu or [i / nu for i in range(nu + 1)]
        fv = fv or [j / nv for j in range(nv + 1)]
        nu, nv = len(fu) - 1, len(fv) - 1
        base = len(self.v)
        for j in range(nv + 1):
            for i in range(nu + 1):
                self.v.append(p0 + eu * fu[i] + ev * fv[j])
                self.n.append(nrm.copy())
        for j in range(nv):
            for i in range(nu):
                a = base + j * (nu + 1) + i
                idx = [a, a + 1, a + nu + 2, a + nu + 1]
                self.f.append(idx)
                self.uv.append([uvf(self.v[k], fu[(k - base) % (nu + 1)], fv[(k - base) // (nu + 1)]) for k in idx])


class Room:
    def __init__(self, texsize):
        self.texsize = texsize        # name -> (w, h) texels
        self.parts = []               # (Part, location, rot_z)
        self.sprites = []             # (kind, centre, size m, style -> rgb)
        self.anchors = {}             # BBR anchors (mm), filled by build()

    def add(self, part, loc=(0, 0, 0), rz=0.0):
        self.parts.append((part, Vector(loc), rz))
        return part

    def world_uv(self, tex, density, eu, ev, rot=False):
        tw, th = self.texsize[tex]
        eu, ev = Vector(eu).normalized(), Vector(ev).normalized()

        def f(p, s, t):
            u, v = p.dot(eu) * density / tw, p.dot(ev) * density / th
            return (v, u) if rot else (u, v)
        return f


def fit_uv(rect=(0, 0, 1, 1), flip=False):
    u0, v0, u1, v1 = rect

    def f(p, s, t):
        if flip:
            s = 1 - s
        return (u0 + (u1 - u0) * s, v0 + (v1 - v0) * t)
    return f


def cells(length, cell):
    return max(1, int(math.ceil(length / cell - 1e-6)))


FACES = {
    'px': lambda x0, y0, z0, x1, y1, z1: ((x1, y0, z0), (0, y1 - y0, 0), (0, 0, z1 - z0)),
    'nx': lambda x0, y0, z0, x1, y1, z1: ((x0, y1, z0), (0, y0 - y1, 0), (0, 0, z1 - z0)),
    'py': lambda x0, y0, z0, x1, y1, z1: ((x1, y1, z0), (x0 - x1, 0, 0), (0, 0, z1 - z0)),
    'ny': lambda x0, y0, z0, x1, y1, z1: ((x0, y0, z0), (x1 - x0, 0, 0), (0, 0, z1 - z0)),
    'pz': lambda x0, y0, z0, x1, y1, z1: ((x0, y0, z1), (x1 - x0, 0, 0), (0, y1 - y0, 0)),
    'nz': lambda x0, y0, z0, x1, y1, z1: ((x0, y1, z0), (x1 - x0, 0, 0), (0, y0 - y1, 0)),
}


def box(R, part, lo, hi, faces='px nx py ny pz nz', cell=0.35, density=D, rot=False, uvmap=None):
    """uvmap: face -> uv function (fitted), else a world projection of part.mat at density."""
    x0, y0, z0 = lo
    x1, y1, z1 = hi
    for fc in faces.split():
        p0, eu, ev = FACES[fc](x0, y0, z0, x1, y1, z1)
        nu, nv = cells(Vector(eu).length, cell), cells(Vector(ev).length, cell)
        uvf = uvmap[fc] if uvmap and fc in uvmap else R.world_uv(part.mat, density, eu, ev, rot=rot)
        fu = fv = None
        if REFINE:
            e1, e2 = Vector(eu), Vector(ev)
            n = e1.cross(e2).normalized()
            key = (part.name, tuple(round(x, 2) for x in n), round(n.dot(Vector(p0)), 3))
            if key in REFINE:
                def fr(e, k, lines):
                    a = max(range(3), key=lambda i: abs(e[i]))
                    out = {0.0, 1.0} | {(c - p0[a]) / e[a] for c in lines.get(a, []) + [math.floor(p0[a] / cell) * cell + cell * i for i in range(-1, k + 2)]}
                    return sorted(x for x in out if -1e-9 <= x <= 1 + 1e-9 and (x in (0.0, 1.0) or 1e-3 < x < 1 - 1e-3))
                fu, fv = fr(e1, nu, REFINE[key]), fr(e2, nv, REFINE[key])
        part.grid(p0, eu, ev, nu, nv, uvf, fu, fv)


def cyl(part, c, r0, r1, h, seg=10, caps='top', urep=1.0, cell=None):
    """Frustum along +z from c; the side's u wraps urep times; caps as fans."""
    c = Vector(c)
    base = len(part.v)
    rings = 1 if not cell else cells(h, cell)
    for j in range(rings + 1):
        t = j / rings
        r = r0 + (r1 - r0) * t
        for i in range(seg + 1):
            a = 2 * math.pi * i / seg
            part.v.append(c + Vector((r * math.cos(a), r * math.sin(a), h * t)))
            slope = (r0 - r1) / h if h else 0
            part.n.append(Vector((math.cos(a), math.sin(a), slope)).normalized())
    for j in range(rings):
        for i in range(seg):
            a = base + j * (seg + 1) + i
            idx = [a, a + 1, a + seg + 2, a + seg + 1]
            part.f.append(idx)
            part.uv.append([(urep * ((k - base) % (seg + 1)) / seg, ((k - base) // (seg + 1)) / rings) for k in idx])
    for cap in caps.split():
        z = h if cap == 'top' else 0
        r = r1 if cap == 'top' else r0
        nz = 1 if cap == 'top' else -1
        b = len(part.v)
        for i in range(seg):
            a = 2 * math.pi * i / seg * (1 if nz > 0 else -1)
            part.v.append(c + Vector((r * math.cos(a), r * math.sin(a), z)))
            part.n.append(Vector((0, 0, nz)))
        part.f.append(list(range(b, b + seg)))
        part.uv.append([(0.5 + 0.5 * math.cos(2 * math.pi * i / seg), 0.5 + 0.5 * math.sin(2 * math.pi * i / seg))
                        for i in range(seg)])


# ---------------------------------------------------------------- the room

def build_shell(R):
    cell = 0.3
    fl = R.add(Part('floor', 'floor'))
    box(R, fl, (RX0, RY0, -0.05), (RX1, RY1, 0.0), faces='pz', cell=cell)
    ce = R.add(Part('ceiling', 'flat', 'ceiling'))
    box(R, ce, (RX0, RY0, RZ), (RX1, RY1, RZ + 0.05), faces='nz', cell=0.6)
    bm = R.add(Part('beam', 'woodv'))
    box(R, bm, (RX0, 0.2, RZ - 0.18), (RX1, 0.42, RZ), faces='ny py nz', cell=0.6)
    wall = R.add(Part('wall', 'wall'))
    x0, x1, z0, z1 = WIN
    for (a, b, za, zb) in [(RX0, x0, WAIN, RZ), (x1, RX1, WAIN, RZ), (x0, x1, z1, RZ), (x0, x1, WAIN, z0)]:
        box(R, wall, (a, RY1, za), (b, RY1 + 0.01, zb), faces='ny', cell=cell)
    box(R, wall, (RX0 - 0.01, RY0, WAIN), (RX0, RY1, RZ), faces='px', cell=cell)
    box(R, wall, (RX1, RY0, WAIN), (RX1 + 0.01, RY1, RZ), faces='nx', cell=cell)
    wa = R.add(Part('wainscot', 'wainscot'))
    box(R, wa, (RX0, RY1 - 0.02, 0), (RX1, RY1, WAIN), faces='ny', cell=cell, density=D / 0.9)
    box(R, wa, (RX0, RY0, 0), (RX0 + 0.02, RY1, WAIN), faces='px', cell=cell, density=D / 0.9)
    box(R, wa, (RX1 - 0.02, RY0, 0), (RX1, RY1, WAIN), faces='nx', cell=cell, density=D / 0.9)
    tr = R.add(Part('trim', 'wood'))
    box(R, tr, (RX0, RY1 - 0.05, WAIN - 0.02), (RX1, RY1, WAIN + 0.04), faces='ny pz', cell=0.6)
    box(R, tr, (RX0, RY0, WAIN - 0.02), (RX0 + 0.05, RY1 - 0.05, WAIN + 0.04), faces='px pz', cell=0.6)
    box(R, tr, (RX1 - 0.05, RY0, WAIN - 0.02), (RX1, RY1 - 0.05, WAIN + 0.04), faces='nx pz', cell=0.6)
    fr = R.add(Part('frame', 'flat', 'frame'))
    t = 0.06
    box(R, fr, (x0 - t, RY1 - 0.04, z0 - t), (x1 + t, RY1, z0), faces='ny pz', cell=0.5)
    box(R, fr, (x0 - t - 0.02, RY1 - 0.09, z0 - t - 0.02), (x1 + t + 0.02, RY1 - 0.04, z0 - t + 0.02), faces='ny pz', cell=0.5)
    box(R, fr, (x0 - t, RY1 - 0.04, z1), (x1 + t, RY1, z1 + t), faces='ny nz', cell=0.5)
    box(R, fr, (x0 - t, RY1 - 0.04, z0), (x0, RY1, z1), faces='ny px', cell=0.5)
    box(R, fr, (x1, RY1 - 0.04, z0), (x1 + t, RY1, z1), faces='ny nx', cell=0.5)
    xm, zm = (x0 + x1) / 2, (z0 + z1) / 2 + 0.12
    box(R, fr, (xm - 0.02, RY1 + 0.02, z0), (xm + 0.02, RY1 + 0.05, z1), faces='ny px nx', cell=0.5)
    box(R, fr, (x0, RY1 + 0.02, zm - 0.02), (x1, RY1 + 0.05, zm + 0.02), faces='ny pz nz', cell=0.5)
    rv = R.add(Part('reveal', 'flat', 'frame'))
    box(R, rv, (x0, RY1, z0), (x1, RY1 + 0.12, z0 + 0.001), faces='pz', cell=0.5)
    box(R, rv, (x0, RY1, z1 - 0.001), (x1, RY1 + 0.12, z1), faces='nz', cell=0.5)
    box(R, rv, (x0, RY1, z0), (x0 + 0.001, RY1 + 0.12, z1), faces='px', cell=0.5)
    box(R, rv, (x1 - 0.001, RY1, z0), (x1, RY1 + 0.12, z1), faces='nx', cell=0.5)
    # the view: a Gouraud gradient (dithered at screen scale by the final pass) and an alpha-tested hills card
    sky_lo, sky_hi = (x0 - 0.5, RY1 + 0.45, z0 - 0.45), (x1 + 0.5, RY1 + 0.46, z1 + 0.45)
    sg = R.add(Part('sky_grad', 'flat', unlit=True, cast=False))
    sg.vcol = ('sky', sky_hi[2], sky_lo[2])
    sg.grid((sky_lo[0], sky_lo[1] + 0.01, sky_lo[2]), (sky_hi[0] - sky_lo[0], 0, 0), (0, 0, sky_hi[2] - sky_lo[2]), 1, 16,
            lambda p, s_, t_: (0.5, 0.5))
    sk = R.add(Part('sky_hills', 'sky_hills', unlit=True, cast=False))
    box(R, sk, (sky_lo[0], sky_lo[1] - 0.005, sky_lo[2]), (sky_hi[0], sky_hi[1] - 0.005, sky_hi[2]), faces='ny', cell=10,
        uvmap={'ny': fit_uv()})
    rg = R.add(Part('rug', 'rug'))
    box(R, rg, (RUG[0], RUG[1], 0.0), (RUG[2], RUG[3], RUG[4]), faces='pz', cell=0.4, uvmap={'pz': fit_uv()})
    # the dusk print on the back wall, left of the lamp
    po = R.add(Part('print_frame', 'wood'))
    box(R, po, (-2.15, RY1 - 0.03, 1.28), (-1.55, RY1, 2.08), faces='ny px nx pz nz', cell=0.5)
    pp = R.add(Part('print', 'poster'))
    box(R, pp, (-2.11, RY1 - 0.031, 1.32), (-1.59, RY1 - 0.03, 2.04), faces='ny', cell=0.5, uvmap={'ny': fit_uv()})


def build_walls(R):
    """The BreadBox poster (left wall) and the corkboard (right wall): frames and backing in the file, the picture
    and the postcards drawn over them at run time (room_walls.c, anchors `poster` and `board`)."""
    for key, sx in (('poster', 1), ('board', -1)):
        w = POSTER if key == 'poster' else BOARD
        x, y, z, hw, hh = w['x'], w['y'], w['z'], w['w'] / 2, w['h'] / 2
        t, d = 0.035, 0.03                    # frame width, depth off the wall
        xa, xb = (x, x + sx * d) if sx > 0 else (x + sx * d, x)
        fr = R.add(Part(key + '_frame', 'wood'))
        fx = 'px' if sx > 0 else 'nx'
        for lo, hi in (((xa, y - hw - t, z + hh), (xb, y + hw + t, z + hh + t)), ((xa, y - hw - t, z - hh - t), (xb, y + hw + t, z - hh)),
                       ((xa, y - hw - t, z - hh), (xb, y - hw, z + hh)), ((xa, y + hw, z - hh), (xb, y + hw + t, z + hh))):
            box(R, fr, lo, hi, faces=fx + ' py ny pz nz', cell=0.5)
        bk = R.add(Part(key + '_back', 'cork' if key == 'board' else 'flat', 'mat' if key == 'poster' else None))
        xs = x + sx * 0.012
        lo, hi = ((x, y - hw, z - hh), (xs, y + hw, z + hh)) if sx > 0 else ((xs, y - hw, z - hh), (x, y + hw, z + hh))
        box(R, bk, lo, hi, faces=fx, cell=0.15)


def build_bookcase(R):
    """The library's case from the `case` contract: uprights on the bay lines, a board under every row, a crown,
    a plinth and a back; the games, plates and cubby backs are the app's (room_case_mesh)."""
    c = CASE
    Y0, Y1 = FRONT, FRONT + c['depth'] / 1000
    upr, board = c['upr'] / 1000, c['board'] / 1000
    xs = [(c['x0'] + k * c['bay']) / 1000 for k in range(c['bays'] + 1)]
    X0, X1 = xs[0] - upr / 2, xs[-1] + upr / 2
    top = c['top'] / 1000
    bc = R.add(Part('bookcase', 'wood', group='p_lib'))
    for x in xs:
        box(R, bc, (x - upr / 2, Y0, 0.0), (x + upr / 2, Y1, top), cell=0.45, rot=True)
    for r in range(c['rows'] + 1):
        zt = (c['base'] + r * c['gap']) / 1000
        box(R, bc, (X0 + upr, Y0, zt - board), (X1 - upr, Y1, zt), faces='pz nz ny', cell=0.4)
    box(R, bc, (X0 - 0.03, Y0 - 0.03, top), (X1 + 0.03, Y1, top + 0.06), cell=0.4)                  # crown
    box(R, bc, (X0 + upr, Y0 + 0.01, 0.0), (X1 - upr, Y0 + 0.03, c['base'] / 1000 - board), faces='ny pz', cell=0.4)  # plinth
    back = R.add(Part('bookcase_back', 'wood', group='p_lib', dim=0.8))
    box(R, back, (X0, Y1 - 0.01, 0.0), (X1, Y1, top), faces='ny', cell=0.3, rot=True)
    pl = R.add(Part('picture_light', 'metal', group='p_lib'))
    box(R, pl, (-0.4, Y0 - 0.02, top - 0.01), (0.4, Y0, top + 0.01), cell=1)
    box(R, pl, (-0.42, Y0 - 0.16, top - 0.01), (0.42, Y0 - 0.09, top + 0.05), cell=1)
    box(R, pl, (-0.01, Y0 - 0.1, top), (0.01, Y0 - 0.02, top + 0.02), cell=1)


def build_tv(R):
    loc, rz = (-1.65, 1.35, 0), math.radians(28)
    st = R.add(Part('tvstand', 'wood', group='p_tv'), loc=loc, rz=rz)
    box(R, st, (-0.45, -0.22, 0.06), (0.45, 0.22, 0.46), cell=0.35)
    box(R, st, (-0.43, -0.2, 0.0), (-0.38, 0.2, 0.06), cell=1)
    box(R, st, (0.38, -0.2, 0.0), (0.43, 0.2, 0.06), cell=1)
    tvb = R.add(Part('tvbody', 'tv_front', group='p_tv'), loc=loc, rz=rz)
    W, H, Dp = 0.6, 0.46, 0.44
    box(R, tvb, (-W / 2, -Dp / 2, 0.46), (W / 2, Dp / 2, 0.46 + H), faces='ny', cell=1, uvmap={'ny': fit_uv((0, 0, 1, 1))})
    box(R, tvb, (-W / 2, -Dp / 2, 0.46), (W / 2, Dp / 2, 0.46 + H), faces='px nx pz', cell=0.3)
    tvr = R.add(Part('tvrear', 'flat', 'knob', group='p_tv'), loc=loc, rz=rz)
    box(R, tvr, (-0.2, Dp / 2, 0.52), (0.2, Dp / 2 + 0.2, 0.86), faces='px nx pz', cell=1)
    bz = R.add(Part('tvbezel', 'flat', 'glass', group='p_tv'), loc=loc, rz=rz)
    sx0, sx1, sz0, sz1 = -W / 2 + 0.03, -W / 2 + 0.03 + 0.44, 0.46 + 0.05, 0.46 + 0.05 + 0.34
    box(R, bz, (sx0, -Dp / 2 - 0.004, sz0), (sx1, -Dp / 2, sz1), faces='ny', cell=1)
    # the picture (BBR part tv_screen, the app's `tv` texture: 128 x 96 texels of a 128 x 128 picture)
    pic = R.add(Part('tv_screen', 'tv', group='tv_screen', unlit=True, cast=False), loc=loc, rz=rz)
    box(R, pic, (sx0 + 0.02, -Dp / 2 - 0.006, sz0 + 0.02), (sx1 - 0.02, -Dp / 2 - 0.005, sz1 - 0.02), faces='ny', cell=0.1,
        uvmap={'ny': fit_uv()})
    R.tv = (loc, rz, ((sx0 + sx1) / 2, -Dp / 2 - 0.006, (sz0 + sz1) / 2))


def build_lamp(R):
    lm = R.add(Part('lamp_metal', 'metal'), loc=(LAMP_X, LAMP_Y, 0))
    cyl(lm, (0, 0, 0), SHADE_R[2], SHADE_R[3], 0.03, seg=10, caps='top')
    cyl(lm, (0, 0, 0.03), 0.014, 0.014, 1.5, seg=6, caps='')
    sh = R.add(Part('shade', 'shade', emit='shade'), loc=(LAMP_X, LAMP_Y, 0))
    cyl(sh, (0, 0, 1.42), SHADE_R[0], SHADE_R[1], 0.3, seg=12, caps='', urep=3, cell=0.15)


def build_desk(R):
    dk = R.add(Part('desk', 'wood'), loc=(0, 0, DESK_DZ))
    X0, X1, Y0, Y1 = 0.95, 2.3, 1.42, 1.98
    box(R, dk, (X0, Y0, 0.72), (X1, Y1, 0.76), cell=0.35)
    for xa in (X0, X1 - 0.42):
        box(R, dk, (xa, Y0 + 0.02, 0.0), (xa + 0.42, Y1, 0.72), faces='ny px nx', cell=0.35)
    dr = R.add(Part('drawers', 'wood', dim=0.85), loc=(0, 0, DESK_DZ))
    for xa in (X0, X1 - 0.42):
        for (za, zb) in ((0.06, 0.36), (0.38, 0.69)):
            box(R, dr, (xa + 0.02, Y0, za), (xa + 0.4, Y0 + 0.02, zb), faces='ny pz', cell=0.35)
    mt = R.add(Part('pulls', 'metal'), loc=(0, 0, DESK_DZ))
    for xa in (X0, X1 - 0.42):
        for zc in (0.25, 0.57):
            box(R, mt, (xa + 0.17, Y0 - 0.018, zc), (xa + 0.25, Y0, zc + 0.02), cell=1)
    dl = R.add(Part('desklamp', 'metal'), loc=(2.05, 1.72, 0.76))
    cyl(dl, (0, 0, 0), 0.07, 0.06, 0.02, seg=8, caps='top')
    cyl(dl, (0, 0, 0.02), 0.008, 0.008, 0.32, seg=5, caps='')
    ds = R.add(Part('deskshade', 'metal'), loc=(2.05, 1.66, 0.76))
    cyl(ds, (0, 0, 0.30), 0.09, 0.03, 0.09, seg=10, caps='')
    mg = R.add(Part('mug', 'flat', 'mug'), loc=(2.22, 1.86, 0.76))
    cyl(mg, (0, 0, 0), 0.04, 0.04, 0.09, seg=8, caps='top')
    cf = R.add(Part('coffee', 'flat', 'coffee'), loc=(2.22, 1.86, 0.76))
    cyl(cf, (0, 0, 0.08), 0.034, 0.034, 0.001, seg=8, caps='top')


def build_scope(R):
    W, H, Dp = 0.40, 0.30, 0.32
    g = dict(group='p_set')
    body = R.add(Part('scope_body', 'scope_side', **g), loc=SCOPE_LOC, rz=SCOPE_RZ)
    box(R, body, (-W / 2, -Dp / 2, 0.02), (W / 2, Dp / 2, 0.02 + H), faces='px nx pz py', cell=1,
        uvmap={f: fit_uv() for f in ('px', 'nx', 'pz', 'py')})
    fr = R.add(Part('scope_front', 'scope_front', **g), loc=SCOPE_LOC, rz=SCOPE_RZ)
    box(R, fr, (-W / 2, -Dp / 2, 0.02), (W / 2, Dp / 2, 0.02 + H), faces='ny', cell=1, uvmap={'ny': fit_uv()})
    ft = R.add(Part('scope_feet', 'flat', 'knob', **g), loc=SCOPE_LOC, rz=SCOPE_RZ)
    for xa in (-W / 2 + 0.02, W / 2 - 0.06):
        box(R, ft, (xa, -Dp / 2 + 0.02, 0), (xa + 0.04, -Dp / 2 + 0.07, 0.02), cell=1)
        box(R, ft, (xa, Dp / 2 - 0.07, 0), (xa + 0.04, Dp / 2 - 0.02, 0.02), cell=1)
    box(R, ft, (-0.12, -0.02, 0.02 + H), (-0.1, 0.02, 0.02 + H + 0.03), cell=1)
    box(R, ft, (0.1, -0.02, 0.02 + H), (0.12, 0.02, 0.02 + H + 0.03), cell=1)
    box(R, ft, (-0.12, -0.02, 0.02 + H + 0.03), (0.12, 0.02, 0.02 + H + 0.045), cell=1)
    # the glass at rest (graticule and a quiet trace); Settings draws the channel name on it, at rest only
    sc = R.add(Part('scope_screen', 'scope_screen', unlit=True, cast=False, **g), loc=SCOPE_LOC, rz=SCOPE_RZ)
    sx0, sx1 = -W / 2 + W * 8 / 128, -W / 2 + W * 76 / 128
    sz1, sz0 = 0.02 + H - H * 10 / 96, 0.02 + H - H * 68 / 96
    box(R, sc, (sx0, -Dp / 2 - 0.003, sz0), (sx1, -Dp / 2 - 0.002, sz1), faces='ny', cell=1, uvmap={'ny': fit_uv()})
    R.scope = (sx0, sx1, sz0, sz1, -Dp / 2 - 0.003)
    kn = R.add(Part('knobs', 'flat', 'knob', smooth=True, **g), loc=SCOPE_LOC, rz=SCOPE_RZ)
    kc = R.add(Part('knobcaps', 'flat', 'knobcap', **g), loc=SCOPE_LOC, rz=SCOPE_RZ)
    for (u, v, r) in [(107, 20, 9), (97, 50, 6), (117, 50, 6)]:
        x = -W / 2 + W * u / 128
        z = 0.02 + H - H * v / 96
        rr = W * r / 128 * 0.85
        tmp = Part('k', 'flat')
        cyl(tmp, (0, 0, 0), rr, rr * 0.85, 0.025, seg=10, caps='top')
        b = len(kn.v)
        for p, n in zip(tmp.v, tmp.n):
            kn.v.append(Vector((x + p.x, -Dp / 2 - p.z, z + p.y)))
            kn.n.append(Vector((n.x, -n.z, n.y)))
        for f, uvs in zip(tmp.f, tmp.uv):
            kn.f.append([i + b for i in f])
            kn.uv.append(uvs)
        box(R, kc, (x - 0.002, -Dp / 2 - 0.0262, z), (x + 0.002, -Dp / 2 - 0.0255, z + rr * 0.8), faces='ny', cell=1)


def build_table(R):
    X0, X1, Y0, Y1, Z = TABLE
    g = dict(group='p_share')
    tb = R.add(Part('table', 'wood', **g))
    box(R, tb, (X0, Y0, Z - 0.04), (X1, Y1, Z), cell=0.3)
    lg = R.add(Part('table_legs', 'wood', dim=0.7, **g))
    for (x, y) in [(X0 + 0.04, Y0 + 0.04), (X1 - 0.09, Y0 + 0.04), (X0 + 0.04, Y1 - 0.09), (X1 - 0.09, Y1 - 0.09)]:
        box(R, lg, (x, y, 0), (x + 0.05, y + 0.05, Z - 0.04), faces='px nx py ny', cell=1, rot=True)
    box(R, lg, (X0 + 0.05, Y0 + 0.06, 0.1), (X1 - 0.05, Y1 - 0.06, 0.12), faces='pz ny', cell=0.4)
    cx, cy, cw, ch = CARD['x'], CARD['y'], CARD['w'], CARD['h']
    ft = R.add(Part('card_foot', 'wood', 'standwood', **g))
    box(R, ft, (cx - 0.075, cy - 0.025, Z), (cx + 0.075, cy + 0.025, Z + 0.03), cell=1)
    # the card leans back a little on its foot (the clips' card frame: centre, yaw, pitch); slot 11 lifts it
    cd = R.add(Part('card', 'card', group='a_card'), loc=(cx, cy, Z + 0.03))
    box(R, cd, (-cw / 2, -0.003, 0), (cw / 2, 0.003, ch), faces='ny', cell=1, uvmap={'ny': fit_uv()})
    box(R, cd, (-cw / 2, -0.003, 0), (cw / 2, 0.003, ch), faces='py px nx pz', cell=1,
        uvmap={f: fit_uv((0, 0.98, 0.02, 1)) for f in ('py', 'px', 'nx', 'pz')})
    R.card = (Vector((cx, cy, Z + 0.03 + ch / 2)), 0.0, 0.0)
    gd = R.add(Part('gba_dev', 'gba', **g), loc=(0.58, 0.05, Z), rz=math.radians(12))
    box(R, gd, (-0.072, -0.04, 0), (0.072, 0.04, 0.022), faces='pz', cell=1, uvmap={'pz': fit_uv()})
    gb = R.add(Part('gba_body', 'flat', 'gbabody', **g), loc=(0.58, 0.05, Z), rz=math.radians(12))
    box(R, gb, (-0.072, -0.04, 0), (0.072, 0.04, 0.022), faces='px nx py ny', cell=1)
    c = R.add(Part('case_cover', 'cover', **g), loc=(1.16, 0.1, Z), rz=math.radians(-18))
    box(R, c, (-0.071, -0.0625, 0), (0.071, 0.0625, 0.0105), faces='pz', cell=1, uvmap={'pz': fit_uv()})
    cb = R.add(Part('case_body', 'flat', 'ps1top', **g), loc=(1.16, 0.1, Z), rz=math.radians(-18))
    box(R, cb, (-0.071, -0.0625, 0), (0.071, 0.0625, 0.0105), faces='px nx py ny', cell=1)
    jar = R.add(Part('candle_jar', 'flat', 'jar', emit='jar', **g), loc=(0.55, 0.36, Z))
    cyl(jar, (0, 0, 0), 0.045, 0.045, 0.08, seg=8, caps='')
    wx = R.add(Part('candle_wax', 'flat', 'wax', emit='wax', **g), loc=(0.55, 0.36, Z))
    cyl(wx, (0, 0, 0.06), 0.04, 0.04, 0.001, seg=8, caps='top')
    mg = R.add(Part('mug2', 'flat', 'mug', **g), loc=(1.25, 0.33, Z))
    cyl(mg, (0, 0, 0), 0.042, 0.042, 0.095, seg=8, caps='top')
    cf = R.add(Part('coffee2', 'flat', 'coffee', **g), loc=(1.25, 0.33, Z))
    cyl(cf, (0, 0, 0.085), 0.036, 0.036, 0.001, seg=8, caps='top')
    R.mug = Vector((1.25, 0.33, Z + 0.1))


def cushion(R, name, mat, cx, cy, rz, s=0.62, h=0.17):
    on_rug = RUG[0] + s / 2 < cx < RUG[2] - s / 2 and RUG[1] + s / 2 < cy < RUG[3] - s / 2
    cu = R.add(Part(name, mat, group='p_tog', smooth=True), loc=(cx, cy, RUG[4] if on_rug else 0.0), rz=rz)
    box(R, cu, (-s / 2, -s / 2, 0.0), (s / 2, s / 2, h), faces='pz px nx py ny', cell=0.1, density=D * 2)
    for p in cu.v:
        u, v = p.x / (s / 2), p.y / (s / 2)
        bul = (1 - min(1, u * u)) * (1 - min(1, v * v))
        if p.z > h * 0.5:
            dimple = math.exp(-(p.x ** 2 + p.y ** 2) / 0.004) * 0.045
            p.z = h + 0.07 * bul - dimple
        p.x *= 1 - 0.06 * (p.z / h - 0.5) ** 2
        p.y *= 1 - 0.06 * (p.z / h - 0.5) ** 2


def leaves(R, part, cx, cy, z, n, h0, h1, r0, r1, w, lean):
    for i in range(n):
        a = 2 * math.pi * i / n + lean
        r = r0 + r1 * (i % 2)
        h = h0 + h1 * ((i * 5) % 3) / 2
        base = Vector((cx + math.cos(a) * r, cy + math.sin(a) * r, z))
        tip = Vector((cx + math.cos(a) * (r + 0.07), cy + math.sin(a) * (r + 0.07), z + h))
        side = Vector((-math.sin(a + 0.7), math.cos(a + 0.7), 0)) * w
        mid = base.lerp(tip, 0.55)
        b = len(part.v)
        for p in (base - side * 0.5, base + side * 0.5, mid + side, mid - side, tip):
            part.v.append(p)
            part.n.append(Vector((math.cos(a), math.sin(a), 0.3)).normalized())
        part.f += [[b, b + 1, b + 2, b + 3], [b + 3, b + 2, b + 4]]
        part.uv += [[(0, 0)] * 4, [(0, 0)] * 3]


def build_plant(R):
    pt = R.add(Part('pot', 'flat', 'pot'), loc=(2.05, 0.95, 0))
    cyl(pt, (0, 0, 0), 0.13, 0.17, 0.32, seg=10, caps='top')
    lf = R.add(Part('snake', 'flat', 'plant2', smooth=True), loc=(2.05, 0.95, 0))
    leaves(R, lf, 0, 0, 0.3, 9, 0.55, 0.22, 0.06, 0.03, 0.035, 0)


def build_sprites(R):
    """Additive glows (lamps, candle, TV at night) and the table mug's steam; their colours are per style."""
    R.sprites.append(('glow', Vector((LAMP_X, LAMP_Y, 1.55)), 0.55, 'lamp'))
    R.sprites.append(('glow', Vector((2.05, 1.62, 1.12)), 0.3, 'desk'))
    R.sprites.append(('glow', Vector((0.55, 0.36, 0.52 - 0.42 + TABLE[4])), 0.07, 'candle'))
    R.sprites.append(('glow', Vector((-1.62, 1.32, 0.98)), 0.5, 'tv'))
    if WINDOW_GLOW:   # texture round: the window's bloom, only for rooms whose styles give it a colour (lights.json)
        R.sprites.append(('glow', Vector(((WIN[0] + WIN[1]) / 2, 1.9, (WIN[2] + WIN[3]) / 2)), WINDOW_GLOW, 'window'))
    for i in range(3):
        R.sprites.append(('steam', R.mug + Vector((0.005 * i, 0, 0.05 * i)), 0.05, 'steam'))


def build(texsize):
    R = Room(texsize)
    build_shell(R)
    build_walls(R)
    build_bookcase(R)
    build_tv(R)
    build_lamp(R)
    build_desk(R)
    build_scope(R)
    build_table(R)
    for name, (cx, cy, rz) in CUSHIONS.items():
        cushion(R, name, 'fabric1' if name == 'cushion1' else 'fabric2', cx, cy, math.radians(rz))
    build_plant(R)
    build_sprites(R)
    return R
