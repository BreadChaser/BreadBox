#!/usr/bin/env python3
"""Offline preview of a BBR v3 room file: the file's own geometry, atlas and a style's colour stream, drawn the way
room_scene.c draws it (texture x vertex colour x 2, alpha test, nearest texels, additive glows, the final 4x4 Bayer
pass to RGB565: the PS1 dither matrix, then RGB565 precision). Runtime parts (the bookcase's games, the TV picture, the walls' pictures) are not drawn.

    python3 tools/room/lamplight/preview.py room.bbr OUT.png [--style day] [--pose home] [--no-dither]

Needs NumPy; writes PNG with zlib (no PIL). For reviewing art changes before a device build.
"""
import argparse
import math
import struct
import sys
import zlib

import numpy as np

PS1_DITHER = np.array([[-4, 0, -3, 1], [2, -2, 3, -1], [-3, 1, -4, 0], [3, -1, 2, -2]])
NEAR, FAR = 30.0, 9000.0


def read(path):
    d = open(path, 'rb').read()
    magic, ver, nstyle, ntex, npart, nanc, stride, nv, ni, style = struct.unpack_from('<4sHHHHHHII8s', d, 0)
    if magic != b'BBR1' or ver != 3 or stride != 16:
        sys.exit('%s: not a BBR v3 room file' % path)
    o = 32
    texs = {}
    for _ in range(ntex):
        n, w, h, fmt, _p, off, size = struct.unpack_from('<8sHHHHII', d, o)
        texs[n.rstrip(b'\0').decode()] = (w, h, fmt, off, size)
        o += 24
    parts = {}
    order = []
    for _ in range(npart):
        p = struct.unpack_from('<12sBBBB12sII3f', d, o)
        name = p[0].rstrip(b'\0').decode()
        parts[name] = dict(mode=p[1], tex=p[2], first=p[6], count=p[7])
        order.append(name)
        o += 48
    anchors = {}
    for _ in range(nanc):
        anchors[d[o:o + 16].split(b'\0')[0].decode()] = struct.unpack_from('<10f', d, o + 16)
        o += 56
    styles = {}
    for _ in range(nstyle):
        sid, col, prb, np_, atlas, lrange, _p = struct.unpack_from('<8sIIHHHH', d, o)
        styles[sid.rstrip(b'\0').decode()] = (col, atlas, lrange or 2)
        o += 24
    V = np.frombuffer(d, np.int16, nv * 8, o).reshape(nv, 8)
    o += nv * 16
    I = np.frombuffer(d, np.uint16, ni, o)
    tex_list = list(texs.items())
    return dict(d=d, texs=texs, tex_list=tex_list, parts=parts, order=order, anchors=anchors, styles=styles, V=V, I=I, nv=nv)


def texture(d, w, h, off):
    px = np.frombuffer(d, '<u2', w * h, off).reshape(h, w).astype(np.uint32)
    r, g, b, a = (px >> 11) & 31, (px >> 6) & 31, (px >> 1) & 31, px & 1
    return np.stack([r * 255 / 31, g * 255 / 31, b * 255 / 31, a * 255], -1)


def camera(pose):
    pos, look, F = np.array(pose[0:3]), np.array(pose[3:6]), pose[6]
    dv = look - pos
    yaw, pitch = math.atan2(dv[0], dv[2]), math.atan2(dv[1], math.hypot(dv[0], dv[2]))
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    R = np.array([cy, 0, -sy]); U = np.array([-sp * sy, cp, -sp * cy]); W = np.array([cp * sy, sp, cp * cy])
    return pos, R, U, W, F


def render(f, style, pose_name, dither=True, dk=None):
    col_off, atlas_i, lrange = f['styles'][style]
    unit = 128 if lrange == 2 else 255 / lrange
    nv = f['nv']
    C = np.frombuffer(f['d'], np.uint8, nv * 4, col_off).reshape(nv, 4).astype(np.float64)
    aname, (aw, ah, _fmt, aoff, _s) = f['tex_list'][atlas_i]
    atlas = texture(f['d'], aw, ah, aoff)
    pos, R, U, W, F = camera(f['anchors']['pose.' + pose_name])
    V = f['V'].astype(np.float64)
    q = V[:, 0:3] - pos
    xc, yc, zc = q @ R, q @ U, q @ W
    sxy = np.stack([200 + F * xc / np.maximum(zc, 1e-3), 120 - F * yc / np.maximum(zc, 1e-3)], -1)
    img = np.zeros((240, 400, 3))
    zb = np.full((240, 400), np.inf)

    def tri_range(name):
        p = f['parts'].get(name)
        return (p['first'], p['count']) if p else (0, 0)
    first, _ = tri_range('static')
    a0, ac = tri_range('a_card')
    ranges = [(first, a0 + ac - first, 'mesh')]
    for name, kind in (('glow', 'add'), ('steam', 'add')):
        s, c = tri_range(name)
        if c:
            ranges.append((s, c, kind))
    I = f['I']
    for start, count, kind in ranges:
        for t in range(start, start + count, 3):
            ids = I[t:t + 3]
            if kind == 'add':
                continue    # sprites: drawn below, as billboards
            if np.any(zc[ids] < NEAR):
                continue
            P = sxy[ids]
            area = (P[1, 0] - P[0, 0]) * (P[2, 1] - P[0, 1]) - (P[2, 0] - P[0, 0]) * (P[1, 1] - P[0, 1])
            if area <= 0:   # (b - a) x (c - a) points out of the face: seen from its front it runs clockwise on screen
                continue
            x0, x1 = int(max(0, math.floor(P[:, 0].min()))), int(min(399, math.ceil(P[:, 0].max())))
            y0, y1 = int(max(0, math.floor(P[:, 1].min()))), int(min(239, math.ceil(P[:, 1].max())))
            if x0 > x1 or y0 > y1:
                continue
            yy, xx = np.mgrid[y0:y1 + 1, x0:x1 + 1] + 0.5
            w0 = ((P[1, 0] - xx) * (P[2, 1] - yy) - (P[2, 0] - xx) * (P[1, 1] - yy)) / area
            w1 = ((P[2, 0] - xx) * (P[0, 1] - yy) - (P[0, 0] - xx) * (P[2, 1] - yy)) / area
            w2 = 1 - w0 - w1
            inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not inside.any():
                continue
            iz = np.array([1 / zc[i] for i in ids])
            pz = w0 * iz[0] + w1 * iz[1] + w2 * iz[2]
            z = 1 / pz
            k = [w * izz / pz for w, izz in zip((w0, w1, w2), iz)]
            uv = sum(k[j][..., None] * V[ids[j], 4:6] for j in range(3)) / 16
            col = sum(k[j][..., None] * C[ids[j], 0:3] for j in range(3))
            tx = np.clip(np.floor(uv[..., 0]).astype(int), 0, aw - 1)
            ty = np.clip(np.floor(uv[..., 1]).astype(int), 0, ah - 1)
            tc = atlas[ty, tx]
            ok = inside & (tc[..., 3] > 127) & (z < zb[y0:y1 + 1, x0:x1 + 1])
            rgb = np.minimum(255, tc[..., :3] * col / unit)
            sub = img[y0:y1 + 1, x0:x1 + 1]
            sub[ok] = rgb[ok]
            zb[y0:y1 + 1, x0:x1 + 1][ok] = z[ok]
    # glows: billboards facing the camera, colour x texture, added (depth tested, no depth write)
    for start, count, kind in ranges[1:]:
        for t in range(start, start + count, 6):
            ids = I[t:t + 6]
            c = V[ids[0]]
            if zc[ids[0]] < NEAR:
                continue
            size = c[3]
            cx, cy = sxy[ids[0]]
            half = F * size / 2 / zc[ids[0]]
            us, vs = V[ids, 4] / 16, V[ids, 5] / 16
            u0, u1, v0, v1 = us.min(), us.max(), vs.min(), vs.max()
            gx0, gx1 = int(max(0, cx - half)), int(min(399, cx + half))
            gy0, gy1 = int(max(0, cy - half)), int(min(239, cy + half))
            for y in range(gy0, gy1 + 1):
                for x in range(gx0, gx1 + 1):
                    if zb[y, x] < zc[ids[0]]:
                        continue
                    fu, fv = (x + .5 - (cx - half)) / (2 * half), (y + .5 - (cy - half)) / (2 * half)
                    if not (0 <= fu < 1 and 0 <= fv < 1):
                        continue
                    t_ = atlas[int(v0 + fv * (v1 - v0)), int(u0 + fu * (u1 - u0))]
                    img[y, x] = np.minimum(255, img[y, x] + C[ids[0], 0:3] * t_[:3] / 255)
    if dither:   # the final pass (room_dither_channel): + the PS1 dither matrix + 4 (half for green), AND F8 FC F8
        if dk is None:
            post = f['anchors'].get('post')
            dk = post[0] if post else 1.0
        m = np.floor(PS1_DITHER[np.arange(240)[:, None] % 4, np.arange(400)[None, :] % 4] * dk + 4.5).astype(int)
        add = np.stack([m, m // 2, m], -1)
        v = np.minimum(np.floor(img).astype(np.int64) + add, 255)
        img = (v & np.array([0xF8, 0xFC, 0xF8])).astype(np.float64)
    return np.clip(img, 0, 255).astype(np.uint8)


def png(path, img):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    h, w = img.shape[:2]
    raw = b''.join(b'\0' + img[y].tobytes() for y in range(h))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('room')
    ap.add_argument('out')
    ap.add_argument('--style', default='day')
    ap.add_argument('--pose', default='home')
    ap.add_argument('--no-dither', action='store_true')
    ap.add_argument('--dither', type=float, default=None, help='strength 0..1 (default: the file\'s post anchor, else 1)')
    a = ap.parse_args()
    f = read(a.room)
    png(a.out, render(f, a.style, a.pose, not a.no_dither, a.dither))


if __name__ == '__main__':
    main()
