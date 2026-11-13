#!/usr/bin/env python3
"""Bar glyphs drawn here rather than taken from a Dreamstale original (icons.tsv kind `bar`, id `drawn`).

  tools/icons/drawn.py [--update [dreamstale.bin]] [--preview out.png]

`start` (UI_ICON_START, start over): a bar and PLAY's triangle pointing back at it, an outline at the stroke of
the bar glyphs (the UI mock's glyph, devtools continue-card NOTES.md: one meaning per icon, the circular arrow
already means Rescan). Drawn as 16x16 8-bit alpha, 16x16 samples a pixel, like pack.py's masks.

--update rewrites the bar glyph section of the packed file in icons.tsv order: glyphs already in it are kept by
name, drawn ones are made here. pack.py calls glyph() for the drawn rows when it packs from the originals."""
import math, pathlib, struct, sys

here = pathlib.Path(__file__).resolve().parent
root = here.parents[1]
N, SS = 16, 16
PIC_BLOCK = 12 + 2 * (128 * 128 + 64 * 64 + 32 * 32) + 64 * 64


def seg_dist(px, py, ax, ay, bx, by):
	dx, dy = bx - ax, by - ay
	t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
	return math.hypot(px - ax - t * dx, py - ay - t * dy)


def start(x, y):
	"""Inside the start-over glyph at (x, y) in pixel units (0..16, y down)."""
	if 2 <= x <= 4 and 3 <= y <= 13:   # the bar
		return True
	tri = ((13.2, 3.2), (13.2, 12.8), (5.6, 8.0))   # PLAY's triangle turned back, stroked 1.6 px with round joins
	return any(seg_dist(x, y, *tri[i], *tri[(i + 1) % 3]) <= 0.8 for i in range(3))


SHAPES = {'start': start}


def glyph(name):
	inside = SHAPES[name]
	out = bytearray()
	for py in range(N):
		for px in range(N):
			hit = sum(inside(px + (i + .5) / SS, py + (j + .5) / SS) for j in range(SS) for i in range(SS))
			out.append(round(255 * hit / (SS * SS)))
	return bytes(out)


def bar_rows():
	rows = [l.split('\t') for l in (here / 'icons.tsv').read_text().splitlines() if l and not l.startswith('#')]
	return [r for r in rows if r[1] == 'bar']


def update(path):
	blob = path.read_bytes()
	assert blob[:4] == b'PSXI', path
	_, npics, nglyphs, _ = struct.unpack('<HHHH', blob[4:12])
	start_at = 12 + npics * PIC_BLOCK
	have = {}
	for i in range(nglyphs):
		off = start_at + i * (12 + N * N)
		have[blob[off:off + 12].rstrip(b'\0').decode()] = blob[off + 12:off + 12 + N * N]
	out = bytearray(blob[:start_at])
	bars = bar_rows()
	for name, _, fid, _ in bars:
		mask = glyph(name) if fid == 'drawn' else have[name]
		out += name.encode().ljust(12, b'\0') + mask
	out[4:12] = struct.pack('<HHHH', 1, npics, len(bars), 0)
	path.write_bytes(out)
	print(f'{path}: {len(bars)} bar glyphs ({", ".join(r[0] for r in bars if r[2] == "drawn")} drawn), {len(out)} bytes')


def preview(path, out):
	"""The bar glyphs at 8x, one row, for checking by eye."""
	import subprocess
	blob = path.read_bytes()
	_, npics, nglyphs, _ = struct.unpack('<HHHH', blob[4:12])
	start_at = 12 + npics * PIC_BLOCK
	row = bytearray()
	for y in range(N):
		for i in range(nglyphs):
			off = start_at + i * (12 + N * N) + 12
			row += blob[off + y * N:off + y * N + N] + b'\0' * 4
	subprocess.run(['magick', '-size', f'{nglyphs * (N + 4)}x{N}', '-depth', '8', 'gray:-', '-filter', 'point', '-resize', '800%', str(out)],
		input=bytes(row), check=True)


if __name__ == '__main__':
	args = sys.argv[1:]
	path = root / 'assets/romfs/icons/dreamstale.bin'
	if '--update' in args:
		i = args.index('--update')
		if i + 1 < len(args) and not args[i + 1].startswith('--'):
			path = pathlib.Path(args[i + 1])
		update(path)
	if '--preview' in args:
		preview(path, pathlib.Path(args[args.index('--preview') + 1]))
