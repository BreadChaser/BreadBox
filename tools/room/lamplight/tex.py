"""Lamplight room textures (art direction A, round 2, 32 texels/m): drawn natively at PS1 sizes from shapes and
small palette ramps. No noise, no text. NumPy only (runs in Blender's Python and in plain Python).

    textures(style) -> {name: (h, w, 4) uint8 RGBA}

Tiers: architecture 32 texels/m (wall, wood, woodv, floor, wainscot, rug), soft 64 texels/m (cushion twill), props at
fixed small sizes. Every texture has at most 16 colours (an art rule: the PICA has no palette textures) and is at most
64x64; check() asserts both. Drawn after the approved round 2 mockups (art direction A, 2026-11-06).
"""
import math
import random

import numpy as np

D = 32                      # architecture texels per metre (owner 2026-11-06: 32)
BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0
MAX_COLOURS, MAX_SIZE = 16, 64


def hexc(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


class Canvas:
    """Index canvas + palette; index -1 = transparent."""

    def __init__(self, w, h, pal, fill=0):
        self.w, self.h = w, h
        self.pal = [hexc(c) for c in pal]
        self.a = np.full((h, w), fill, dtype=np.int16)

    def px(self, x, y, i):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.a[y, x] = i

    def pxw(self, x, y, i):
        self.a[y % self.h, x % self.w] = i

    def rect(self, x0, y0, x1, y1, i):
        self.a[max(0, y0):max(0, y1), max(0, x0):max(0, x1)] = i

    def frame(self, x0, y0, x1, y1, i):
        self.rect(x0, y0, x1, y0 + 1, i); self.rect(x0, y1 - 1, x1, y1, i)
        self.rect(x0, y0, x0 + 1, y1, i); self.rect(x1 - 1, y0, x1, y1, i)

    def disc(self, cx, cy, r, i):
        yy, xx = np.mgrid[0:self.h, 0:self.w]
        self.a[(xx + .5 - cx) ** 2 + (yy + .5 - cy) ** 2 <= r * r] = i

    def rgba(self):
        out = np.zeros((self.h, self.w, 4), np.uint8)
        for i, c in enumerate(self.pal):
            m = self.a == i
            out[m, :3] = c
            out[m, 3] = 255
        return out


def glyph(c, x, y, rows, i):
    for dy, row in enumerate(rows):
        for dx, ch in enumerate(row):
            if ch == '#':
                c.px(x + dx, y + dy, i)


# ---------------------------------------------------------------- architecture tier

def wood(pal, plank_m=0.25, seams=True, seed=1, lines=1):
    """1 m tile. Planks plank_m tall, broken sine grain lines, one knot. pal: 5 ramp dark->light."""
    rnd = random.Random(seed)
    S = D
    pl = max(3, int(round(plank_m * D))) if seams else S
    c = Canvas(S, S, pal, 2)
    for p in range(S // pl):
        y0 = p * pl
        base = 2 if p % 3 != 1 else 3
        c.rect(0, y0, S, y0 + pl, base)
        n_lines = lines if seams else lines * 2
        for g in range(n_lines):
            yc = y0 + (g + 1) * pl / (n_lines + 1) + (0.5 if seams else 0)
            amp = rnd.uniform(0.4, 0.9)
            ph = rnd.uniform(0, S)
            col = base - 1 if g % 2 == 0 else base + 1
            run = rnd.randrange(9, 14)
            for x in range(S):
                if (x + g * 5 + p * 3) % run < 2:
                    continue
                y = yc + amp * math.sin(2 * math.pi * (x + ph) / S)
                c.pxw(x, int(math.floor(y)), min(col, 4))
        if seams:
            c.rect(0, y0, S, y0 + 1, 0)
    kx, ky = rnd.randrange(3, S - 3), rnd.randrange(2, pl - 1) + pl * rnd.randrange(max(1, S // pl))
    for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
        c.pxw(kx + dx, ky + dy, 1)
    c.pxw(kx, ky, 0)
    return c


def floor(pal, seed=3):
    """2 m tile. Boards 12.5 cm in two tones, a grain line on every other board, a gap one tone darker on the left
    edge, one end joint (light round 3: calm, the floor is minified in perspective and point-sampled)."""
    rnd = random.Random(seed)
    S = 2 * D
    bw = max(3, int(round(0.125 * D)))
    c = Canvas(S, S, pal, 2)
    for b in range(S // bw):
        x0 = b * bw
        tone = [2, 2, 3, 2, 2, 2, 3, 2][b % 8]
        c.rect(x0, 0, x0 + bw, S, tone)
        xc = x0 + 1 + (bw - 1) // 2
        ph = rnd.uniform(0, S)
        for y in range(S if b % 2 else 0):
            if (y + b * 7) % 13 < 3:
                continue
            x = xc + (0.6 * math.sin(2 * math.pi * (y + ph) / S * 2) if bw > 3 else 0)
            c.pxw(int(round(x)), y, 2 if tone == 3 else tone - 1)
        c.rect(x0, 0, x0 + 1, S, 1)
        j = rnd.randrange(0, S)
        c.rect(x0, j, x0 + bw, j + 1, 1)
    return c


def wallpaper(base, stripe, motif):
    """1 m tile = 4x4 cells of 25 cm: one pinstripe per cell and a 1x2 texel lozenge dash on every other cell, the
    dash's colour a third of the way from the paper (light round 3: calm)."""
    S = D
    p = S // 4
    b, m = hexc(base), hexc(motif)
    motif = '#%02x%02x%02x' % tuple((x + 2 * y) // 3 for x, y in zip(m, b))
    c = Canvas(S, S, [base, stripe, motif], 0)
    for x in range(0, S, p):
        c.rect(x, 0, x + 1, S, 1)
    for row in range(4):
        cy = row * p + p // 2
        for col in range(4):
            if (row + col) % 2:
                continue
            x0 = col * p
            cx = x0 + 3 + (p - 3) // 2 if row % 2 == 0 else x0 + 1
            c.pxw(cx, cy - 1, 2); c.pxw(cx, cy, 2)
    return c


def wainscot(pal):
    """One raised panel per 0.9 m, plain (no grain in the panel)."""
    S = D
    m = max(2, int(round(S * 7 / 64)))
    c = Canvas(S, S, pal, 2)
    c.rect(m, m, S - m, S - m, 1)
    c.rect(m + 1, m + 1, S - m - 1, S - m - 1, 2)
    c.rect(m, m, S - m, m + 1, 0); c.rect(m, m, m + 1, S - m, 0)
    c.rect(m, S - m - 1, S - m, S - m, 3); c.rect(S - m - 1, m, S - m, S - m, 3)
    c.rect(m + 2, m + 2, S - m - 2, m + 3, 3)
    c.rect(0, 0, 1, S, 0)
    c.rect(0, 0, S, 1, 4)
    return c


def rug(pal, W=64, H=48):
    """Kilim fitted to the 2.4 x 1.6 m rug. pal: 0 dark brown, 1 cream, 2 red, 3 dark red, 4 indigo, 5 ochre."""
    c = Canvas(W, H, pal, 2)
    F = 3
    x0, x1 = F, W - F

    def band(inset, width, i):
        c.rect(x0 + inset, inset, x1 - inset, inset + width, i)
        c.rect(x0 + inset, H - inset - width, x1 - inset, H - inset, i)
        c.rect(x0 + inset, inset, x0 + inset + width, H - inset, i)
        c.rect(x1 - inset - width, inset, x1 - inset, H - inset, i)
    for inset, w, i in [(0, 1, 0), (1, 1, 1), (2, 3, 3), (5, 1, 1)]:
        band(inset, w, i)
    cy = H / 2
    rings = [(15, 1), (13, 4), (8, 5), (5, 3)]
    for j in range(3):
        cx = x0 + (x1 - x0) * (j + 1) / 4
        for y in range(H):
            for x in range(W):
                d = abs(x + .5 - cx) + abs(y + .5 - cy)
                for r, i in rings:
                    if d < r:
                        c.a[y, x] = i
    for y in range(H):
        for x in list(range(0, F)) + list(range(x1, W)):
            c.a[y, x] = 1    # the fringe: solid
    return c


def fabric(pal):
    """Twill, 25 cm tile at 2x the architecture density."""
    S = max(8, int(round(0.25 * 2 * D)) // 4 * 4)
    c = Canvas(S, S, pal, 1)
    for y in range(S):
        for x in range(S):
            t = (x + y) % 4
            c.a[y, x] = 2 if t == 0 else (0 if t == 2 else 1)
    return c


def cork(pal):
    """1 m corkboard tile: flecks on a regular lattice (no noise)."""
    S = D
    c = Canvas(S, S, pal, 1)
    for y in range(S):
        for x in range(S):
            k = (x * 7 + y * 13 + (x * y) % 5) % 11
            c.a[y, x] = 0 if k == 0 else 2 if k in (3, 7) else 1
    return c


# ---------------------------------------------------------------- props tier

def shade(pal):
    S = 16
    c = Canvas(S, S, pal, 2)
    for x in range(S):
        c.rect(x, 0, x + 1, S, [1, 2, 3, 2][x % 4])
    c.rect(0, 0, S, 1, 0); c.rect(0, S - 1, S, S, 0)
    return c


def metal(pal):
    c = Canvas(8, 8, pal, 1)
    for x, i in enumerate([0, 1, 2, 3, 2, 1, 1, 0]):
        c.rect(x, 0, x + 1, 8, i)
    return c


def tv_front(pal):
    """32x32 over the 0.6 x 0.46 m front; the picture is geometry. Grille, two buttons and a LED on the right strip."""
    S = 32
    c = Canvas(S, S, pal + ['#e0402a'], 2)
    for x in (25, 27, 29):
        c.rect(x, 6, x + 1, 22, 0)
    for y in (25, 28):
        c.rect(25, y, 30, y + 2, 1)
        c.rect(25, y, 30, y + 1, 3)
    c.px(27, 23, 4)
    c.rect(0, 0, S, 1, 3); c.rect(0, S - 1, S, S, 0)
    return c


def scope_front(enamel):
    """64x48 front panel: screen recess on the left (the glass is its own quad), knobs and jacks on the right. No text."""
    W, H = 64, 48
    pal = ['#121210', enamel[0], enamel[1], enamel[2], '#3a3830', '#7cff6a', '#1e3a1a', '#b8b4a0']
    c = Canvas(W, H, pal, 2)
    c.rect(0, 0, W, 1, 3); c.rect(0, 0, 1, H, 3)
    c.rect(0, H - 1, W, H, 1); c.rect(W - 1, 0, W, H, 1)
    sx0, sy0, sx1, sy1 = 3, 4, 39, 35
    c.rect(sx0, sy0, sx1, sy1, 0)
    c.rect(sx0, sy0 - 1, sx1, sy0, 1)
    c.rect(sx0, sy1, sx1, sy1 + 1, 3)
    c.rect(43, 2, 44, H - 2, 1)
    c.rect(44, 2, 45, H - 2, 3)
    for (cx, cy, r, n) in [(53.5, 10, 4.5, 9), (48.5, 25, 3.0, 7), (58.5, 25, 3.0, 7)]:
        c.disc(cx, cy, r + 0.8, 1)
        for t in range(n):
            a = math.radians(225 - t * 270 / (n - 1))
            c.px(int(cx + (r + 2.2) * math.cos(a)), int(cy - (r + 2.2) * math.sin(a)), 4)
    for (cx, g) in [(48.5, ['.#..', '#.#.', '...#']), (58.5, ['##.#', '.#.#', '.###'])]:
        glyph(c, int(cx) - 2, 33, g, 4)
        c.disc(cx, 40, 2.6, 7)
        c.disc(cx, 40, 1.5, 1)
        c.px(int(cx), 40, 0)
    c.rect(4, 40, 6, 42, 5)
    c.rect(4, 42, 6, 43, 6)
    c.rect(7, 40, 20, 41, 1)
    c.rect(7, 42, 20, 43, 3)
    return c


def scope_side(enamel):
    c = Canvas(16, 16, ['#121210', enamel[0], enamel[1], enamel[2]], 2)
    for y in range(3, 13, 2):
        c.rect(3, y, 13, y + 1, 0)
    c.rect(3, 13, 13, 14, 3)
    return c


def scope_screen(pal):
    """The glass at rest: graticule and a quiet trace (Settings draws the channel name over it at rest only)."""
    W, H = 64, 48
    c = Canvas(W, H, pal, 0)
    for x in range(4, W, 8):
        for y in range(4, H, 2):
            c.px(x, y, 1)
    for y in range(4, H, 8):
        for x in range(4, W, 2):
            c.px(x, y, 1)
    c.rect(0, H // 2, W, H // 2 + 1, 4)
    c.rect(W // 2, 0, W // 2 + 1, H, 4)
    for x in range(W):
        y = H * 0.3 + H * 0.12 * math.sin(2 * math.pi * x / (W / 2))
        c.px(x, int(y), 3)
        c.px(x, int(y) + 1, 2)
    return c


def poster(pal):
    """24x32 framed print: a mountain lake at sunrise; flat shapes, dithered sky, blank mat."""
    W, H = 24, 32
    c = Canvas(W, H, pal, 0)
    m = 2
    for y in range(m, H - m):
        f = (y - m) / (H * 0.45)
        for x in range(m, W - m):
            c.a[y, x] = 1 if f + 0.25 * BAYER4[y % 4, x % 4] < 0.55 else 2
    c.disc(12, 13, 2.6, 7)
    for x in range(m, W - m):
        h1 = 15 - abs(x + .5 - 9) * 0.9
        h2 = 16.5 - abs(x + .5 - 16) * 0.7
        for y in range(int(h1), 20): c.a[y, x] = 3
        for y in range(int(h2), 20): c.a[y, x] = 4
    c.rect(m, 20, W - m, H - m, 5)
    for y in range(21, H - m - 3, 2):
        xs = m + (y * 5) % 7
        c.rect(xs, y, xs + 5, y + 1, 6)
    c.rect(m, H - m - 3, W - m, H - m, 0)
    return c


def gba_face(body):
    c = Canvas(16, 8, [body[0], body[1], body[2], '#20242a', '#5a6a5a', '#1a1a1e'], 1)
    c.rect(5, 1, 11, 6, 3)
    c.rect(6, 2, 10, 5, 4)
    for x, y in ((2, 3), (1, 4), (2, 4), (3, 4), (2, 5), (13, 4), (14, 3)):
        c.px(x, y, 5)
    c.rect(0, 0, 16, 1, 2)
    return c


def case_cover():
    c = Canvas(16, 16, ['#101014', '#2a2a34', '#e05a3a', '#f0b040', '#3a8ad0', '#e8e8e0'], 1)
    c.rect(0, 0, 2, 16, 0)
    c.disc(9, 7, 4.5, 2)
    c.disc(11, 5.5, 3, 3)
    c.rect(3, 12, 15, 14, 4)
    c.rect(3, 14, 9, 15, 5)
    return c


def card(pal):
    """32x48 share card: the bread-clip mark on top, a QR-like pattern below (pattern only: the real code is the
    app's, on the bottom screen)."""
    W, H = 32, 48
    c = Canvas(W, H, pal, 0)
    c.frame(0, 0, W, H, 3)
    c.disc(16, 9, 6, 2)
    c.rect(15, 9, 17, 16, 0)
    c.disc(16, 9, 2, 0)
    rnd = random.Random(21)
    N = 21
    m = np.zeros((N, N), bool)
    for y in range(N):
        for x in range(N):
            m[y, x] = rnd.random() < 0.5

    def finder(fx, fy):
        m[fy:fy + 7, fx:fx + 7] = True
        m[fy + 1:fy + 6, fx + 1:fx + 6] = False
        m[fy + 2:fy + 5, fx + 2:fx + 5] = True
        if fy + 7 < N: m[fy + 7, max(0, fx - 1):fx + 8] = False
        if fx + 7 < N: m[max(0, fy - 1):fy + 8, fx + 7] = False
        if fx - 1 >= 0: m[max(0, fy - 1):fy + 8, fx - 1] = False
    finder(0, 0); finder(14, 0); finder(0, 14)
    for y in range(N):
        for x in range(N):
            if m[y, x]:
                c.px(5 + x, 20 + y, 1)
    return c


def paper():
    c = Canvas(8, 8, ['#d8ccb0', '#c4b494'], 0)
    for y in range(0, 8, 2):
        c.rect(0, y, 8, y + 1, 1)
    return c


def glow():
    c = Canvas(16, 16, ['#000000', '#202020', '#404040', '#686868', '#909090'], 0)
    for r, i in [(7.5, 1), (5.5, 2), (3.5, 3), (2, 4)]:
        c.disc(8, 8, r, i)
    return c


def sky_hills(stops, sun=None, moon=None, hills='#1e1a34', lights='#ffcf70', stars=6, seed=5, far=None, clouds=None,
              nlights=9, moon_cut=(1, -0.6, 1)):
    """64x32 window view: stars, flat clouds, sun or moon, a far (hazy) hill range and the hills, on transparent (the
    sky gradient is vertex colour). moon_cut: the dark disc cut out of the moon (dx, dy, radius) in moon radii,
    dx/dy in texels when moon_cut[2] == 1 and the default is kept."""
    rnd = random.Random(seed)
    W, H = 64, 32
    n = len(stops)
    c = Canvas(W, H, list(stops) + [hills, lights, '#fff4d8', '#f8f0e0'] + [far or '#000000'] + list(clouds or ('#000000', '#000000')), -1)
    for _ in range(stars):
        c.px(rnd.randrange(W), rnd.randrange(int(H * 0.35)), n + 2)
    if clouds:   # flat clouds: stacked rounded bars, a shade row underneath
        for (cx, cy, w) in ((12, 7, 13), (44, 4, 10), (55, 12, 7), (27, 14, 6)):
            c.rect(cx - w // 2, cy, cx + w // 2 + 1, cy + 2, n + 5)
            c.rect(cx - w // 2 + 2, cy - 1, cx + w // 2 - 1, cy, n + 5)
            c.rect(cx - w // 4, cy - 2, cx + w // 4 + 1, cy - 1, n + 5)
            c.rect(cx - w // 2 + 1, cy + 2, cx + w // 2, cy + 3, n + 6)
    if far:
        for x in range(W):
            h = 19 + 2.5 * math.sin(x / 7.3 + 1.1) + 1.2 * math.sin(x / 3.1)
            c.rect(x, int(h), x + 1, H, n + 4)
    if sun:
        c.disc(*sun, n + 2)
    if moon:
        c.disc(*moon, n + 3)
        dx, dy, r = moon_cut
        c.disc(moon[0] + dx, moon[1] + dy, moon[2] * r, -1)
    for x in range(W):
        h = 23 + 2 * math.sin(x / 5.5) + 1.5 * math.sin(x / 2.65 + 2) + (1 if 30 < x < 35 else 0)
        c.rect(x, int(h), x + 1, H, n)
    for _ in range(nlights):
        c.px(rnd.randrange(2, 62), rnd.randrange(27, 31), n + 1)
    return c


# ---------------------------------------------------------------- texture sets (one per room style)

def textures(p):
    """Every room texture of a texture set (lights.json "texture_sets": walnut, oak, wall, rug palettes and the window
    view's sky): {name: RGBA array}."""
    en = ['#7a7666', '#a6a28e', '#ccc8b0']
    t = dict(
        wood=wood(p['walnut']), woodv=wood(p['walnut'], seams=False, seed=7),
        floor=floor(p['oak']), wall=wallpaper(*p['wall']), wainscot=wainscot(p['walnut']), rug=rug(p['rug']),
        fabric1=fabric(['#6a4a14', '#94701e', '#b88c2e']), fabric2=fabric(['#163a3c', '#225452', '#33706a']),
        cork=cork(['#6a4424', '#a0703e', '#c08a52']),
        shade=shade(['#8a6a3a', '#c8a46a', '#e2c48a', '#f2dcaa']), metal=metal(['#3a2a10', '#7a5a22', '#b89040', '#e8cc80']),
        scope_front=scope_front(en), scope_side=scope_side(en),
        scope_screen=scope_screen(['#06140a', '#0f2a14', '#2f6a30', '#6ef060', '#164a1c']),
        tv_front=tv_front(['#1a1614', '#2a2420', '#3e362e', '#544a3e']),
        poster=poster(['#e8dcc0', '#f2b46a', '#e07a4a', '#6a4a7a', '#3a3a6a', '#2a5a6a', '#9ad0d0', '#f8ecd0']),
        card=card(['#f2e8d0', '#1c1a18', '#c8783a', '#e0d4b4']), gba=gba_face(['#3a2a6a', '#5a44a0', '#7a64c0']),
        cover=case_cover(), paper=paper(), flat=Canvas(8, 8, ['#ffffff'], 0), glow=glow(),
        sky_hills=sky_hills(p['sky'], **{k: tuple(v) if isinstance(v, list) else v for k, v in p.get('sky_view', {}).items()}))
    if p.get('finish') == 'soft':   # the first room's soft painted surfaces (soft.py), same sizes and slots
        import soft
        t.update(soft.soft_textures(p))
    out = {k: v.rgba() for k, v in t.items()}
    check(out)
    return out


def sky_stops(p):
    return [hexc(c) for c in p['sky']]


def check(tex):
    for name, a in tex.items():
        h, w = a.shape[:2]
        opaque = a[..., 3] > 0
        cols = len(np.unique(a[opaque][:, :3].reshape(-1, 3), axis=0)) if opaque.any() else 0
        assert cols <= MAX_COLOURS, '%s: %d colours (at most %d)' % (name, cols, MAX_COLOURS)
        assert max(w, h) <= MAX_SIZE, '%s: %dx%d (at most %d)' % (name, w, h, MAX_SIZE)
