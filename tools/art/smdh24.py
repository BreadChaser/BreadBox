#!/usr/bin/env python3
"""Put a hand-made 24x24 icon into an SMDH (bannertool's icon.icn or devkitPro's .smdh).

bannertool makesmdh and smdhtool both make the 24 px icon by shrinking the 48 px one. This replaces it.
SMDH layout: small icon (24x24 RGB565, 8x8 tiles, Morton order inside a tile) at 0x2040, 0x480 bytes;
large icon (48x48) at 0x24C0, 0x1200 bytes.

Usage:  smdh24.py <file.icn|file.smdh> <icon24.png>          patch in place
        smdh24.py --dump <file> <out48.png> [out24.png]     decode the icons (to check)
Standard library only (it runs inside the pinned build container): 8-bit RGB/RGBA PNGs, not interlaced.
GPL-2.0-or-later (BreadBox)."""
import struct
import sys
import zlib

SMALL, LARGE = (0x2040, 24), (0x24C0, 48)


def png_read(path):
    """(width, height, rows of (r, g, b) tuples) from an 8-bit RGB or RGBA, non-interlaced PNG."""
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', f'{path}: not a PNG'
    at, idat, w = 8, b'', 0
    while at < len(data):
        n, kind = struct.unpack_from('>I4s', data, at)
        chunk = data[at + 8:at + 8 + n]
        if kind == b'IHDR':
            w, h, depth, color, _, _, interlace = struct.unpack('>IIBBBBB', chunk)
            assert depth == 8 and color in (2, 6) and not interlace, f'{path}: need 8-bit RGB/RGBA, not interlaced'
            bpp = 3 if color == 2 else 4
        elif kind == b'IDAT':
            idat += chunk
        elif kind == b'IEND':
            break
        at += 12 + n
    raw, stride, prev, rows, i = zlib.decompress(idat), w * bpp, bytearray(w * bpp), [], 0
    for _ in range(h):
        f, line = raw[i], bytearray(raw[i + 1:i + 1 + stride]); i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[x * bpp:x * bpp + 3]) for x in range(w)]); prev = line
    return w, h, rows


def png_write(path, rows):
    w, h = len(rows[0]), len(rows)
    raw = b''.join(b'\0' + bytes(v for px in row for v in px) for row in rows)
    chunk = lambda k, d: struct.pack('>I', len(d)) + k + d + struct.pack('>I', zlib.crc32(k + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def _order(n):
    """(x, y) of each texel in SMDH tile order."""
    out = []
    for ty in range(n // 8):
        for tx in range(n // 8):
            for i in range(64):
                x = (i & 1) | ((i >> 1) & 2) | ((i >> 2) & 4)
                y = ((i >> 1) & 1) | ((i >> 2) & 2) | ((i >> 3) & 4)
                out.append((tx * 8 + x, ty * 8 + y))
    return out


def encode(rows, n):
    assert len(rows) == n and len(rows[0]) == n, f'icon must be {n}x{n}'
    data = bytearray()
    for x, y in _order(n):
        r, g, b = rows[y][x]
        data += struct.pack('<H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return bytes(data)


def decode(buf, off, n):
    rows = [[(0, 0, 0)] * n for _ in range(n)]
    for i, (x, y) in enumerate(_order(n)):
        v = struct.unpack_from('<H', buf, off + i * 2)[0]
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        rows[y][x] = ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))
    return rows


def main(argv):
    if argv[:1] == ['--dump']:
        buf = open(argv[1], 'rb').read()
        assert buf[:4] == b'SMDH', 'not an SMDH file'
        png_write(argv[2], decode(buf, *LARGE))
        if len(argv) > 3:
            png_write(argv[3], decode(buf, *SMALL))
        return
    path, png = argv
    buf = bytearray(open(path, 'rb').read())
    assert buf[:4] == b'SMDH', 'not an SMDH file'
    w, h, rows = png_read(png)
    assert (w, h) == (24, 24), 'icon24 must be 24x24'
    buf[SMALL[0]:SMALL[0] + 24 * 24 * 2] = encode(rows, 24)
    open(path, 'wb').write(buf)


if __name__ == '__main__':
    main(sys.argv[1:])
