"""tools/art/smdh24.py: the hand-made 24 px HOME icon goes into the SMDH exactly, the 48 px icon is left alone,
and the standard-library PNG reader agrees with ImageMagick on the shipped icon and banner."""
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/art'))
import smdh24  # noqa: E402

ART = ROOT / 'apps/breadbox'


def magick_rgb(path):
    w, h = (int(v) for v in subprocess.check_output(['magick', 'identify', '-format', '%w %h', str(path)]).split())
    raw = subprocess.check_output(['magick', str(path), '-depth', '8', 'rgba:-'])
    return [[tuple(raw[(y * w + x) * 4:(y * w + x) * 4 + 3]) for x in range(w)] for y in range(h)]


def q565(px):
    r, g, b = px[0] >> 3, px[1] >> 2, px[2] >> 3
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def main():
    for name in ('icon.png', 'icon24.png', 'banner.png'):
        w, h, rows = smdh24.png_read(ART / name)
        assert rows == magick_rgb(ART / name), f'{name}: PNG reader differs from ImageMagick'
    for n in (24, 48):
        order = smdh24._order(n)
        assert sorted(order) == [(x, y) for x in range(n) for y in range(n)], f'tile order {n} is not a permutation'
    assert smdh24._order(8)[:4] == [(0, 0), (1, 0), (0, 1), (1, 1)]

    _, _, big = smdh24.png_read(ART / 'icon.png')
    _, _, small = smdh24.png_read(ART / 'icon24.png')
    smdh = bytearray(b'SMDH' + bytes(0x36C0 - 4))
    smdh[0x24C0:0x24C0 + 0x1200] = smdh24.encode(big, 48)
    with tempfile.TemporaryDirectory() as d:
        icn = Path(d) / 'icon.icn'
        icn.write_bytes(smdh)
        smdh24.main([str(icn), str(ART / 'icon24.png')])
        out = icn.read_bytes()
        assert len(out) == len(smdh) and out[:0x2040] == smdh[:0x2040] and out[0x24C0:] == smdh[0x24C0:], \
            'only the 24 px icon may change'
        assert smdh24.decode(out, 0x2040, 24) == [[q565(p) for p in r] for r in small], '24 px icon not written exactly'
        assert smdh24.decode(out, 0x24C0, 48) == [[q565(p) for p in r] for r in big]
        smdh24.main(['--dump', str(icn), os.path.join(d, 'i48.png'), os.path.join(d, 'i24.png')])
        assert smdh24.png_read(Path(d) / 'i24.png')[2] == [[q565(p) for p in r] for r in small], 'dump differs'
    # The shipped icons are RGB565-exact, so the 3DS shows them unchanged.
    assert big == [[q565(p) for p in r] for r in big], 'icon.png is not RGB565-exact'
    assert small == [[q565(p) for p in r] for r in small], 'icon24.png is not RGB565-exact'
    print('smdh24: OK')


main()
