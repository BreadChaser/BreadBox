"""Game-start props: a PS1 jewel case that opens and a GBA box whose flap opens and cartridge slides out.

Built in the animation's box-local frame so they replace its pulled box exactly:
  x = thickness (front cover at +x), y = 0..H up, z = width (spine at -z). Units: mm.
All parts use one runtime texture 'case' (256x256) that the app/mock fills from the game's cover art:
  front (0,0)-(128,128)  back (128,0)-(256,128)  spine (0,128)-(16,256)  disc label (128,128)-(256,256)
  white (16,128)-(32,144)  cartridge label (32,128)-(96,192)  cardboard (32,192)-(64,224)
"""
import math
import os

import numpy as np

import bbr
import geo

PS1 = dict(T=14, H=125, W=142)
GBA = dict(T=27, H=137, W=120)
CASE = {'front': (0, 0, 128, 128), 'back': (128, 0, 128, 128), 'spine': (0, 128, 16, 128), 'disc': (128, 128, 128, 128),
        'white': (16, 128, 16, 16), 'cart': (32, 128, 64, 64), 'card': (32, 192, 32, 32)}


class CaseAtlas:
    """uv lookups into the runtime 'case' texture (texels)."""

    def uv(self, name, u, v):
        x, y, w, h = CASE[name]
        return (x + 0.5 + u * (w - 1), y + 0.5 + v * (h - 1))


def face(B, reg, col, W, uv=(0, 0, 1, 1), n=None):
    B.quad(W, reg, col, uv, n=n)


def ps1(B):
    geo.Part.OBJ = 'ps1_case'
    T, H, W = PS1['T'], PS1['H'], PS1['W']
    t, w = T / 2, W / 2
    lid = 3.2
    xl = t - lid                      # lid / tray split
    black, clear = (0.11, 0.11, 0.13), (0.82, 0.86, 0.9)
    B.part('ps1_tray', tex='case', lit=False)
    # back cover (seen through the clear back of the tray), spine, black rims
    face(B, 'back', (1, 1, 1), [(-t, H, w), (-t, H, -w), (-t, 0, -w), (-t, 0, w)])
    face(B, 'spine', (1, 1, 1), [(-t, H, -w), (xl, H, -w), (xl, 0, -w), (-t, 0, -w)])
    face(B, 'white', black, [(xl, H, w), (-t, H, w), (-t, 0, w), (xl, 0, w)])
    face(B, 'white', black, [(-t, H, w), (xl, H, w), (xl, H, -w), (-t, H, -w)])
    face(B, 'white', black, [(-t, 0, -w), (xl, 0, -w), (xl, 0, w), (-t, 0, w)])
    # the inside of the tray: black plastic with the disc rings
    face(B, 'white', (0.07, 0.07, 0.08), [(xl, H - 2, -w + 4), (xl, H - 2, w - 2), (xl, 2, w - 2), (xl, 2, -w + 4)])
    B.part('ps1_disc', tex='case', lit=False)
    cz, cy, r = 6.0, H / 2, 59.0
    seg = 20
    pts = [(xl + 0.8, cy + r * math.sin(-i / seg * 2 * math.pi), cz + r * math.cos(-i / seg * 2 * math.pi)) for i in range(seg)]
    B.fan(pts, 'disc', (1, 1, 1), lambda p: (0.5 + (p[2] - cz) / (2 * r), 0.5 - (p[1] - cy) / (2 * r)), center=(xl + 0.8, cy, cz), n=(1, 0, 0))
    hub = [(xl + 1.2, cy + 11 * math.sin(-i / 10 * 2 * math.pi), cz + 11 * math.cos(-i / 10 * 2 * math.pi)) for i in range(10)]
    B.fan(hub, 'white', (0.55, 0.57, 0.6), lambda p: (0.5, 0.5), center=(xl + 1.2, cy, cz), n=(1, 0, 0))
    # the lid: hinged at the spine edge; the cover insert under clear plastic
    B.part('ps1_lid', tex='case', pivot=(xl, 0, -w), lit=False)
    face(B, 'front', (1, 1, 1), [(t, H, -w), (t, H, w), (t, 0, w), (t, 0, -w)])
    face(B, 'back', (0.55, 0.55, 0.58), [(xl, H, w), (xl, H, -w), (xl, 0, -w), (xl, 0, w)])
    face(B, 'white', clear, [(t, H, w), (xl, H, w), (xl, 0, w), (t, 0, w)])
    face(B, 'white', clear, [(xl, H, w), (t, H, w), (t, H, -w), (xl, H, -w)])
    face(B, 'white', clear, [(xl, 0, -w), (t, 0, -w), (t, 0, w), (xl, 0, w)])
    face(B, 'white', clear, [(xl, H, -w), (t, H, -w), (t, 0, -w), (xl, 0, -w)])


def gba(B):
    geo.Part.OBJ = 'gba_box'
    T, H, W = GBA['T'], GBA['H'], GBA['W']
    t, w = T / 2, W / 2
    card = (0.86, 0.8, 0.68)
    B.part('gba_box', tex='case', lit=False)
    face(B, 'front', (1, 1, 1), [(t, H, -w), (t, H, w), (t, 0, w), (t, 0, -w)])
    face(B, 'back', (1, 1, 1), [(-t, H, w), (-t, H, -w), (-t, 0, -w), (-t, 0, w)])
    face(B, 'spine', (1, 1, 1), [(-t, H, -w), (t, H, -w), (t, 0, -w), (-t, 0, -w)])
    face(B, 'white', (0.92, 0.9, 0.86), [(t, H, w), (-t, H, w), (-t, 0, w), (t, 0, w)])
    face(B, 'white', (0.85, 0.83, 0.8), [(-t, 0, -w), (t, 0, -w), (t, 0, w), (-t, 0, w)])
    # inside walls, seen when the top is open
    face(B, 'card', card, [(t - 1, H, w), (t - 1, H, -w), (t - 1, H - 40, -w), (t - 1, H - 40, w)])
    face(B, 'card', card, [(-t + 1, H, -w), (-t + 1, H, w), (-t + 1, H - 40, w), (-t + 1, H - 40, -w)])
    face(B, 'card', card, [(-t, H, -w + 1), (t, H, -w + 1), (t, H - 40, -w + 1), (-t, H - 40, -w + 1)])
    face(B, 'card', card, [(t, H, w - 1), (-t, H, w - 1), (-t, H - 40, w - 1), (t, H - 40, w - 1)])
    face(B, 'card', (0.4, 0.36, 0.3), [(-t, H - 40, w), (t, H - 40, w), (t, H - 40, -w), (-t, H - 40, -w)])
    # the top flap: hinged on the back top edge, opens upwards and back
    B.part('gba_flap', tex='case', pivot=(-t, H, 0), lit=False)
    face(B, 'white', (0.92, 0.9, 0.86), [(-t, H, w), (t, H, w), (t, H, -w), (-t, H, -w)])
    face(B, 'card', card, [(-t, H - 0.5, -w), (t, H - 0.5, -w), (t, H - 0.5, w), (-t, H - 0.5, w)])
    face(B, 'card', (0.86, 0.8, 0.68), [(t - 0.6, H, w), (t - 0.6, H - 22, w), (t - 0.6, H - 22, -w), (t - 0.6, H, -w)])
    # the cartridge, standing inside; slides up out of the box at runtime
    B.part('gba_cart', tex='case', pivot=(0, H - 44, 0), lit=False)
    cw, ch, ct = 57.0, 35.0, 8.0
    y0 = H - 44
    grey = (0.62, 0.62, 0.66)
    face(B, 'white', grey, [(ct / 2, y0 + ch, -cw / 2), (ct / 2, y0 + ch, cw / 2), (ct / 2, y0, cw / 2), (ct / 2, y0, -cw / 2)])
    face(B, 'cart', (1, 1, 1), [(ct / 2 + 0.3, y0 + ch - 3, -cw / 2 + 5), (ct / 2 + 0.3, y0 + ch - 3, cw / 2 - 5), (ct / 2 + 0.3, y0 + 9, cw / 2 - 5), (ct / 2 + 0.3, y0 + 9, -cw / 2 + 5)])
    face(B, 'white', grey, [(-ct / 2, y0 + ch, cw / 2), (-ct / 2, y0 + ch, -cw / 2), (-ct / 2, y0, -cw / 2), (-ct / 2, y0, cw / 2)])
    face(B, 'white', (0.7, 0.7, 0.74), [(-ct / 2, y0 + ch, cw / 2), (ct / 2, y0 + ch, cw / 2), (ct / 2, y0 + ch, -cw / 2), (-ct / 2, y0 + ch, -cw / 2)])
    face(B, 'white', (0.5, 0.5, 0.54), [(ct / 2, y0 + ch, cw / 2), (-ct / 2, y0 + ch, cw / 2), (-ct / 2, y0, cw / 2), (ct / 2, y0, cw / 2)])
    face(B, 'white', (0.5, 0.5, 0.54), [(-ct / 2, y0 + ch, -cw / 2), (ct / 2, y0 + ch, -cw / 2), (ct / 2, y0, -cw / 2), (-ct / 2, y0, -cw / 2)])


def build(out):
    B = geo.Builder(CaseAtlas())
    ps1(B)
    gba(B)
    for p in B.parts.values():
        p.C = [np.concatenate([np.asarray(c[:3], float), [1.0]]) for c in p.C]
    anchors = {'ps1': [PS1['T'], PS1['H'], PS1['W']], 'gba': [GBA['T'], GBA['H'], GBA['W']]}
    data, stats = bbr.pack(B.parts, [('case', 256, 256, 'runtime', None)], anchors, 'props')
    open(os.path.join(out, 'props.bbr'), 'wb').write(data)
    return stats
