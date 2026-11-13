"""Soft room textures (texture round 2026-11-11): the first room's painted surfaces at the Lamplight sizes.

A texture set with "finish": "soft" (lights.json) draws wood, floor, wallpaper, wainscot and cushions here instead of
tex.py's crisp ramps: soft shading across every plank and board, broad low-contrast grain, seams one step darker
instead of the darkest colour, wide soft stripes instead of 1-texel pinstripes, a bevelled panel, a near-flat weave.
Gradients are ramps of colours one RGB555 step apart, blended with the 4x4 Bayer matrix inside the texture (what
PS1 artists did), so every texture still has at most 16 colours and the same size and atlas slot: nothing changes
in the room file but texels.

    soft_textures(p) -> {name: Canvas}     p: the texture set (walnut, oak, wall, rug, optional fabric1/fabric2, soft)

p["soft"] (optional) tunes the look: grain, edge (plank and board edge shading), seam, stripe, mottle (all in ramp
steps), and shade (ramp steps of each texture's palette span).
"""
import math
import random

import numpy as np

import tex

B4 = tex.BAYER4


def q5(c):
    return tuple(int(round(round(x * 31 / 255) * 255 / 31)) for x in c)


def ramp(stops, n=12):
    """n colours interpolated along stops (dark -> light, hex), snapped to RGB555, consecutive repeats dropped."""
    cs = [tex.hexc(s) for s in stops]
    out = []
    for i in range(n):
        t = i / (n - 1) * (len(cs) - 1)
        k = min(int(t), len(cs) - 2)
        f = t - k
        c = q5(tuple(a + (b - a) * f for a, b in zip(cs[k], cs[k + 1])))
        if not out or c != out[-1]:
            out.append(c)
    return out


def paint(v, pal):
    """Continuous ramp positions (h, w) -> Canvas, ordered-dithered between neighbouring ramp colours."""
    h, w = v.shape
    yy, xx = np.mgrid[0:h, 0:w]
    i = np.clip(np.floor(v + B4[yy % 4, xx % 4]), 0, len(pal) - 1).astype(np.int16)
    c = tex.Canvas(w, h, ['#000000'], 0)
    c.pal = list(pal)
    c.a = i
    return c


def wave(x, period, phase=0.0):
    return np.sin(2 * math.pi * (x / period + phase))


def wood(stops, k, plank_m=0.25, seams=True, seed=1):
    """1 m tile: planks with soft shading across them, broad wavy grain, one soft knot, a seam one step darker."""
    rnd = random.Random(seed)
    S = tex.D
    pal = ramp(stops)
    n = len(pal)
    pl = max(3, int(round(plank_m * S))) if seams else S
    yy, xx = np.mgrid[0:S, 0:S].astype(float)
    v = np.full((S, S), n * 0.5)
    for p in range(S // pl):
        y0 = p * pl
        m = (yy >= y0) & (yy < y0 + pl)
        base = n * 0.5 + rnd.uniform(-1.0, 1.0) * k['shade']
        across = (yy - y0 + 0.5) / pl * 2 - 1                    # -1 .. 1 across the plank
        prof = (-k['edge'] * across ** 4 + 0.3 * k['edge'] * (1 - across ** 2)) if seams else 0.0   # one long board: no edges
        ph1, ph2 = rnd.random(), rnd.random()
        grain = k['grain'] * (0.6 * wave(xx, S, ph1) * wave(yy - y0, pl * 2, ph2) + 0.4 * wave(xx + 3 * across, S / 2, ph2))
        v = np.where(m, base + prof + grain, v)
        if seams:
            v[y0, :] -= k['seam']
    kx, ky = rnd.randrange(4, S - 4), rnd.randrange(2, max(3, pl - 2)) + pl * rnd.randrange(max(1, S // pl))
    if seams:   # one soft knot on planks; the long boards (no seams: uprights, sides) stay clear
        d = np.sqrt(np.minimum(abs(xx - kx), S - abs(xx - kx)) ** 2 + (yy - ky) ** 2 * 3)
        v -= k['grain'] * 1.6 * np.clip(1 - d / 2.5, 0, 1)
    return paint(v, pal)


def floor(stops, k, seed=3):
    """2 m tile: boards 12.5 cm (4 texels) with soft shading across each, the gap one soft step darker, broad grain."""
    rnd = random.Random(seed)
    S = 2 * tex.D
    pal = ramp(stops)
    n = len(pal)
    bw = max(3, int(round(0.125 * tex.D)))
    yy, xx = np.mgrid[0:S, 0:S].astype(float)
    v = np.zeros((S, S))
    for b in range(S // bw):
        x0 = b * bw
        m = (xx >= x0) & (xx < x0 + bw)
        base = n * 0.5 + [0, -0.6, 0.5, -0.2, 0.3, -0.5, 0.6, 0.0][b % 8] * k['shade']
        across = (xx - x0 + 0.5) / bw * 2 - 1
        prof = -k['edge'] * np.maximum(-across, 0) ** 2 - 0.4 * k['edge'] * np.maximum(across, 0) ** 3
        ph = rnd.random()
        grain = k['grain'] * 0.45 * wave(yy, S, ph)
        v = np.where(m, base + prof + grain, v)
        j = rnd.randrange(0, S)
        v[j, x0:x0 + bw] -= k['seam']
    return paint(v, pal)


def wallpaper(stops, k):
    """1 m tile: wide soft stripes (25 cm repeat, a soft-edged band of the lighter paper) and a faint mottle."""
    S = tex.D
    pal = ramp(stops, 10)
    n = len(pal)
    yy, xx = np.mgrid[0:S, 0:S].astype(float)
    p = S / 4
    band = 0.5 + 0.5 * np.cos(2 * math.pi * (xx + 0.5) / p)         # 1 at the stripe's centre, 0 between
    v = n * 0.35 + k['stripe'] * band ** 2
    v += k['mottle'] * (0.5 * wave(xx, S, 0.13) * wave(yy, S, 0.4) + 0.5 * wave(xx + yy, S, 0.7))
    return paint(v, pal)


def wainscot(stops, k):
    """One raised panel per 0.9 m with soft bevels: lit top and left, shaded bottom and right, a soft rail."""
    S = tex.D
    pal = ramp(stops)
    n = len(pal)
    yy, xx = np.mgrid[0:S, 0:S].astype(float)
    m = max(2, int(round(S * 7 / 64)))
    v = np.full((S, S), n * 0.45)
    inside = (xx >= m) & (xx < S - m) & (yy >= m) & (yy < S - m)
    # distance into the panel from each edge (texels): a 2-texel bevel
    dl, dr, dt, db = xx - m + 0.5, S - m - xx - 0.5, yy - m + 0.5, S - m - yy - 0.5
    bev = np.minimum.reduce([dl, dr, dt, db])
    light = np.where((dt <= dl) & (dt <= dr) & (dt <= db), 1.0, np.where((dl <= dr) & (dl <= db), 0.6,
                     np.where(db <= dr, -1.0, -0.7)))
    v = np.where(inside & (bev < 2.0), n * 0.45 + k['edge'] * 1.2 * light * (1 - bev / 2.0), v)
    v = np.where(inside & (bev >= 2.0), n * 0.5 + k['grain'] * 0.5 * wave(yy, S, 0.2), v)
    v[:, 0] -= k['seam']
    v[0, :] += k['seam'] * 0.8
    return paint(v, pal)


def fabric(stops, k):
    """Near-flat cloth, 25 cm tile at 64 texels/m: a faint diagonal weave and a soft low swell (the cushion's shading
    comes from the light)."""
    S = max(8, int(round(0.25 * 2 * tex.D)) // 4 * 4)
    pal = ramp(stops, 8)
    n = len(pal)
    yy, xx = np.mgrid[0:S, 0:S].astype(float)
    v = n * 0.5 + 0.35 * k['grain'] * wave(xx + yy, 4) + 0.25 * k['shade'] * wave(xx, S) * wave(yy, S, 0.25)
    return paint(v, pal)


DEFAULT = dict(grain=0.9, edge=1.2, seam=1.0, stripe=1.4, mottle=0.5, shade=0.6)


def soft_textures(p):
    k = dict(DEFAULT, **p.get('soft', {}))
    wal, oak, wall = p['walnut'], p['oak'], p['wall']
    return dict(wood=wood(wal, k), woodv=wood(wal, k, seams=False, seed=7), floor=floor(oak, k),
                wall=wallpaper(p.get('wall_soft', wall[:2]), k), wainscot=wainscot(wal, k),
                fabric1=fabric(p.get('fabric1', ['#6a4a14', '#94701e', '#b88c2e']), k),
                fabric2=fabric(p.get('fabric2', ['#163a3c', '#225452', '#33706a']), k))
