"""Small pixel-art texture painter (numpy) and an atlas packer for the R10 room.

Every texture is painted here by code at its final size (16..128 texels), PS1 style: few colours,
hard edges, a little hand-placed noise. Images are float RGBA arrays, shape (h, w, 4), 0..1.
Nothing is loaded from outside the project.
"""
import math
import numpy as np


def rgb(*c):
    """'#rrggbb' or 0..255 triple -> float RGB."""
    if len(c) == 1 and isinstance(c[0], str):
        h = c[0].lstrip('#')
        return np.array([int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)])
    if len(c) == 1:
        c = c[0]
    return np.array([v / 255 for v in c[:3]])


def img(w, h, col=(0, 0, 0), a=1.0):
    im = np.zeros((h, w, 4))
    im[..., :3] = rgb(col) if not isinstance(col, np.ndarray) else col
    im[..., 3] = a
    return im


def hash2(x, y, seed=0):
    v = np.sin(x * 127.1 + y * 311.7 + seed * 74.7) * 43758.5453
    return v - np.floor(v)


def vnoise(w, h, sx, sy, seed=0):
    """Value noise on a (h, w) grid, cell size sx, sy texels."""
    ys, xs = np.mgrid[0:h, 0:w].astype(float)
    fx, fy = xs / sx, ys / sy
    ix, iy = np.floor(fx), np.floor(fy)
    tx, ty = fx - ix, fy - iy
    tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
    a, b = hash2(ix, iy, seed), hash2(ix + 1, iy, seed)
    c, d = hash2(ix, iy + 1, seed), hash2(ix + 1, iy + 1, seed)
    return (a + (b - a) * tx) * (1 - ty) + (c + (d - c) * tx) * ty


def grain(im, amount=0.08, seed=1, sx=1, sy=1):
    n = vnoise(im.shape[1], im.shape[0], sx, sy, seed) - 0.5
    im[..., :3] = np.clip(im[..., :3] * (1 + 2 * amount * n[..., None]), 0, 1)
    return im


def rect(im, x0, y0, x1, y1, col, a=1.0):
    h, w = im.shape[:2]
    x0, y0, x1, y1 = max(0, int(x0)), max(0, int(y0)), min(w, int(x1)), min(h, int(y1))
    if x1 <= x0 or y1 <= y0:
        return im
    c = rgb(col) if not isinstance(col, np.ndarray) else col
    im[y0:y1, x0:x1, :3] = im[y0:y1, x0:x1, :3] * (1 - a) + c * a
    im[y0:y1, x0:x1, 3] = np.maximum(im[y0:y1, x0:x1, 3], a)
    return im


def mul(im, x0, y0, x1, y1, k):
    im[int(y0):int(y1), int(x0):int(x1), :3] *= k
    np.clip(im[..., :3], 0, 1, out=im[..., :3])
    return im


def disc(im, cx, cy, r, col, a=1.0, ring=None):
    h, w = im.shape[:2]
    ys, xs = np.mgrid[0:h, 0:w]
    d = np.hypot(xs + 0.5 - cx, ys + 0.5 - cy)
    m = d <= r if ring is None else (d <= r) & (d > r - ring)
    c = rgb(col) if not isinstance(col, np.ndarray) else col
    im[m, :3] = im[m, :3] * (1 - a) + c * a
    im[m, 3] = np.maximum(im[m, 3], a)
    return im


def line(im, x0, y0, x1, y1, col, a=1.0):
    n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
    for i in range(n + 1):
        t = i / max(1, n)
        rect(im, round(x0 + (x1 - x0) * t), round(y0 + (y1 - y0) * t), round(x0 + (x1 - x0) * t) + 1,
             round(y0 + (y1 - y0) * t) + 1, col, a)
    return im


def vgrad(im, top, bot, y0=0, y1=None):
    y1 = im.shape[0] if y1 is None else y1
    for y in range(int(y0), int(y1)):
        t = (y - y0) / max(1, y1 - y0 - 1)
        im[y, :, :3] = rgb(top) * (1 - t) + rgb(bot) * t
        im[y, :, 3] = 1
    return im


def radial(w, h, col, power=2.0):
    """Soft radial sprite, alpha falls from 1 at the centre to 0 at the edge."""
    ys, xs = np.mgrid[0:h, 0:w]
    d = np.hypot((xs + 0.5) / w - 0.5, (ys + 0.5) / h - 0.5) * 2
    im = img(w, h, col, 0)
    im[..., 3] = np.clip(1 - d, 0, 1) ** power
    return im


def quantize(im, levels=32, dither=True):
    """15/16-bit look: 5 bits per channel with a 2x2 ordered dither."""
    out = im.copy()
    if dither:
        bay = np.array([[0, 2], [3, 1]]) / 4 - 0.375
        h, w = im.shape[:2]
        d = np.tile(bay, (h // 2 + 1, w // 2 + 1))[:h, :w][..., None] / (levels - 1)
        out[..., :3] = out[..., :3] + d
    out[..., :3] = np.round(np.clip(out[..., :3], 0, 1) * (levels - 1)) / (levels - 1)
    return out


# ---- 5x7 pixel font (hand-made), 7 rows of 5; '#' = ink.
_F5 = """
A .###. #...# #...# ##### #...# #...# #...#
B ####. #...# #...# ####. #...# #...# ####.
C .#### #.... #.... #.... #.... #.... .####
D ####. #...# #...# #...# #...# #...# ####.
E ##### #.... #.... ####. #.... #.... #####
F ##### #.... #.... ####. #.... #.... #....
G .#### #.... #.... #..## #...# #...# .####
H #...# #...# #...# ##### #...# #...# #...#
I ##### ..#.. ..#.. ..#.. ..#.. ..#.. #####
J ..### ...#. ...#. ...#. ...#. #..#. .##..
K #...# #..#. #.#.. ##... #.#.. #..#. #...#
L #.... #.... #.... #.... #.... #.... #####
M #...# ##.## #.#.# #.#.# #...# #...# #...#
N #...# ##..# #.#.# #..## #...# #...# #...#
O .###. #...# #...# #...# #...# #...# .###.
P ####. #...# #...# ####. #.... #.... #....
Q .###. #...# #...# #...# #.#.# #..#. .##.#
R ####. #...# #...# ####. #.#.. #..#. #...#
S .#### #.... #.... .###. ....# ....# ####.
T ##### ..#.. ..#.. ..#.. ..#.. ..#.. ..#..
U #...# #...# #...# #...# #...# #...# .###.
V #...# #...# #...# #...# #...# .#.#. ..#..
W #...# #...# #...# #.#.# #.#.# ##.## #...#
X #...# #...# .#.#. ..#.. .#.#. #...# #...#
Y #...# #...# .#.#. ..#.. ..#.. ..#.. ..#..
Z ##### ....# ...#. ..#.. .#... #.... #####
0 .###. #...# #..## #.#.# ##..# #...# .###.
1 ..#.. .##.. ..#.. ..#.. ..#.. ..#.. .###.
2 .###. #...# ....# ...#. ..#.. .#... #####
3 ####. ....# ....# .###. ....# ....# ####.
4 ...#. ..##. .#.#. #..#. ##### ...#. ...#.
5 ##### #.... ####. ....# ....# #...# .###.
6 .###. #.... #.... ####. #...# #...# .###.
7 ##### ....# ...#. ..#.. .#... .#... .#...
8 .###. #...# #...# .###. #...# #...# .###.
9 .###. #...# #...# .#### ....# ....# .###.
: ..... ..##. ..##. ..... ..##. ..##. .....
. ..... ..... ..... ..... ..... ..##. ..##.
- ..... ..... ..... .###. ..... ..... .....
/ ....# ...#. ...#. ..#.. .#... .#... #....
' ..#.. ..#.. .#... ..... ..... ..... .....
& .##.. #..#. #.#.. .#... #.#.# #..#. .##.#
* ..... ..#.. #.#.# .###. #.#.# ..#.. .....
> .#... ..#.. ...#. ....# ...#. ..#.. .#...
"""
FONT = {' ': '.' * 35}
for _l in _F5.strip().splitlines():
    _k, *_rows = _l.split()
    FONT[_k] = ''.join(_rows)


def text(im, x, y, s, col, a=1.0, scale=1, space=1):
    """Draw s with the 5x7 font, top-left at x, y. Returns the width."""
    cx = x
    for ch in s.upper():
        g = FONT.get(ch, FONT[' '])[:35].ljust(35, '.')
        for j in range(7):
            for i in range(5):
                if g[j * 5 + i] == '#':
                    rect(im, cx + i * scale, y + j * scale, cx + (i + 1) * scale, y + (j + 1) * scale, col, a)
        cx += (5 + space) * scale
    return cx - x - space * scale


def text_w(s, scale=1, space=1):
    return len(s) * (5 + space) * scale - space * scale


def text_c(im, cx, y, s, col, a=1.0, scale=1, space=1):
    return text(im, cx - text_w(s, scale, space) // 2, y, s, col, a, scale, space)


# ---- tiny font (3x5) for labels where 5x7 is too big, 5 rows of 3
_F3 = """
A .#. #.# ### #.# #.#
B ##. #.# ##. #.# ##.
C .## #.. #.. #.. .##
D ##. #.# #.# #.# ##.
E ### #.. ##. #.. ###
F ### #.. ##. #.. #..
G .## #.. #.# #.# .##
H #.# #.# ### #.# #.#
I ### .#. .#. .#. ###
J ..# ..# ..# #.# .#.
K #.# #.# ##. #.# #.#
L #.. #.. #.. #.. ###
M #.# ### ### #.# #.#
N ##. #.# #.# #.# #.#
O .#. #.# #.# #.# .#.
P ##. #.# ##. #.. #..
Q .#. #.# #.# ##. .##
R ##. #.# ##. #.# #.#
S .## #.. .#. ..# ##.
T ### .#. .#. .#. .#.
U #.# #.# #.# #.# .#.
V #.# #.# #.# .#. .#.
W #.# #.# ### ### #.#
X #.# #.# .#. #.# #.#
Y #.# #.# .#. .#. .#.
Z ### ..# .#. #.. ###
0 .#. #.# #.# #.# .#.
1 .#. ##. .#. .#. ###
2 ##. ..# .#. #.. ###
3 ##. ..# .#. ..# ##.
4 #.# #.# ### ..# ..#
5 ### #.. ##. ..# ##.
6 .## #.. ##. #.# .#.
7 ### ..# .#. .#. .#.
8 .#. #.# .#. #.# .#.
9 .#. #.# .## ..# ##.
. ... ... ... ... .#.
: ... .#. ... .#. ...
- ... ... ### ... ...
"""
FONT3 = {' ': '.' * 15}
for _l in _F3.strip().splitlines():
    _k, *_rows = _l.split()
    FONT3[_k] = ''.join(_rows)


def text3(im, x, y, s, col, a=1.0):
    cx = x
    for ch in s.upper():
        g = FONT3.get(ch, FONT3[' ']).ljust(15, '.')[:15]
        for j in range(5):
            for i in range(3):
                if g[j * 3 + i] == '#':
                    rect(im, cx + i, y + j, cx + i + 1, y + j + 1, col, a)
        cx += 4
    return cx - x - 1


def text3_w(s):
    return len(s) * 4 - 1


# ---- materials
def wood(w, h, light, dark, planks=4, along='x', seed=3, knots=1, gaps=True):
    """Plank wood: grain along x (or y), a few planks, darker joints, one or two knots."""
    if along == 'y':
        return np.transpose(wood(h, w, light, dark, planks, 'x', seed, knots, gaps), (1, 0, 2)).copy()
    L, D = rgb(light), rgb(dark)
    ys, xs = np.mgrid[0:h, 0:w].astype(float)
    ph = h / planks
    p = np.floor(ys / ph)
    off = hash2(p, 3, seed) * 97
    g = 0.5 + 0.5 * np.sin((ys + 3 * np.sin((xs + off) * 0.11 + p)) * 1.9 + vnoise(w, h, 9, 2, seed) * 5)
    t = 0.35 + 0.4 * g + 0.25 * (hash2(p, 7, seed) - 0.5)
    for k in range(knots):
        kx, ky = hash2(k, 1, seed + 9) * w, (math.floor(hash2(k, 2, seed + 9) * planks) + 0.5) * ph
        d = np.hypot((xs - kx) * 0.6, ys - ky)
        t = np.where(d < 2.2, 0.05, t - 0.25 * np.exp(-d * 0.35) * np.sin(d * 1.6) ** 2)
    im = img(w, h)
    im[..., :3] = D + (L - D) * np.clip(t, 0, 1)[..., None]
    if gaps and planks > 1:
        for i in range(1, planks):
            y = int(i * ph)
            mul(im, 0, y, w, y + 1, 0.45)
            # butt joints at random positions
        for i in range(planks):
            x = int(hash2(i, 5, seed) * w)
            mul(im, x, int(i * ph), x + 1, int((i + 1) * ph), 0.55)
    return im


def plaster(w, h, a, b, seed=5):
    n = vnoise(w, h, 7, 7, seed) * 0.6 + vnoise(w, h, 2, 2, seed + 1) * 0.4
    im = img(w, h)
    im[..., :3] = rgb(a) * (1 - n[..., None]) + rgb(b) * n[..., None]
    return im


def fabric(w, h, col, seed=7, weave=0.08, k=1.0):
    im = img(w, h, rgb(col) * k)
    ys, xs = np.mgrid[0:h, 0:w]
    tw = np.where((xs + ys) % 2 == 0, 1 + weave, 1 - weave)
    n = 0.88 + 0.24 * vnoise(w, h, 5, 5, seed)
    im[..., :3] = np.clip(im[..., :3] * (tw * n)[..., None], 0, 1)
    return im


def knit(w, h, col, seed=11):
    """Chunky wool: rows of V stitches."""
    im = img(w, h, rgb(col) if not isinstance(col, np.ndarray) else (col / 255 if col.max() > 1.5 else col))
    ys, xs = np.mgrid[0:h, 0:w]
    v = ((xs % 4) - 1.5) ** 2 + ((ys + (xs % 4 >= 2) * 1) % 3 - 1) ** 2
    im[..., :3] *= (1.12 - 0.07 * v)[..., None]
    im[..., :3] *= (0.92 + 0.16 * vnoise(w, h, 6, 6, seed))[..., None]
    np.clip(im[..., :3], 0, 1, out=im[..., :3])
    return im


class Atlas:
    """Shelf packer. Regions are named; UVs are returned in texels (x, y from the top-left)."""

    def __init__(self, w, h, pad=1):
        self.w, self.h, self.pad = w, h, pad
        self.im = img(w, h, (255, 0, 255), 1)
        self.reg = {}
        self.x = self.y = self.row = 0

    def add(self, name, im):
        h, w = im.shape[:2]
        if self.x + w + self.pad > self.w:
            self.x, self.y, self.row = 0, self.y + self.row + self.pad, 0
        if self.y + h > self.h:
            raise RuntimeError(f'atlas full at {name} ({w}x{h}); used rows to {self.y}')
        self.im[self.y:self.y + h, self.x:self.x + w] = im
        # 1-texel bleed border so nearest sampling at edges never picks a neighbour
        if self.pad:
            y0, x0 = self.y, self.x
            if x0 + w < self.w:
                self.im[y0:y0 + h, x0 + w] = im[:, -1]
            if y0 + h < self.h:
                self.im[y0 + h, x0:x0 + w] = im[-1, :]
        self.reg[name] = (self.x, self.y, w, h)
        self.x += w + self.pad
        self.row = max(self.row, h)
        return self.reg[name]

    def used(self):
        return self.y + self.row

    def uv(self, name, u, v):
        """u, v in 0..1 of the region -> texel coordinates in the atlas (half-texel inset)."""
        x, y, w, h = self.reg[name]
        return (x + 0.5 + u * (w - 1), y + 0.5 + v * (h - 1))


def to_5551(im):
    q = np.round(np.clip(im[..., :3], 0, 1) * 31).astype(np.uint16)
    a = (im[..., 3] > 0.5).astype(np.uint16)
    return (q[..., 0] << 11) | (q[..., 1] << 6) | (q[..., 2] << 1) | a


def to_565(im):
    r = np.round(np.clip(im[..., 0], 0, 1) * 31).astype(np.uint16)
    g = np.round(np.clip(im[..., 1], 0, 1) * 63).astype(np.uint16)
    b = np.round(np.clip(im[..., 2], 0, 1) * 31).astype(np.uint16)
    return (r << 11) | (g << 5) | b


def to_4444(im):
    q = np.round(np.clip(im, 0, 1) * 15).astype(np.uint16)
    return (q[..., 0] << 12) | (q[..., 1] << 8) | (q[..., 2] << 4) | q[..., 3]


def write_png(path, im):
    """PNG without Pillow (Blender's Python has zlib)."""
    import struct
    import zlib
    a = (np.clip(im, 0, 1) * 255 + 0.5).astype(np.uint8)
    h, w = a.shape[:2]
    raw = b''.join(b'\x00' + a[y].tobytes() for y in range(h))

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + \
        chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(path, 'wb').write(data)


def dither565(im):
    """light / 2 in 0..1 -> RGB565 levels with a light 4x4 ordered dither (no banding in the soft gradients)"""
    h, w = im.shape[:2]
    b = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16 - .47
    d = np.tile(b, (h // 4 + 1, w // 4 + 1))[:h, :w][..., None]
    lv = np.array([31, 63, 31], float)
    q = np.floor(np.clip(im[..., :3], 0, 1) * lv + .5 + d) / lv
    return np.clip(q, 0, 1)
