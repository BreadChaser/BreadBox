#!/usr/bin/env python3
"""Round 9 framing check of the held case (owner: "the game cases are not centered when he is holding them").

Runs the clip simulation (#bb10=1&bbmotion=4&bbframe=1, the device's clip camera: F 600, 400 x 240) and prints,
for every clip with the case held (hold, the end of pick, the turns): the worst distance of the case's screen
centre from x = 200, its mean centre y, the smallest margin to the screen edge (px; < 0 = cut off) and the largest
share of the cover (the face toward the camera, front side only) the hands cover.
Targets: |dx| <= 8 px, margin >= 0, cover <= 15 %.
Usage: ROOM_MOCK=/path/to/ui-mock framing.py [OUT.json]"""
import html, json, os, pathlib, re, subprocess, sys, tempfile
if 'ROOM_MOCK' not in os.environ:
    sys.exit('set ROOM_MOCK to the UI mock directory containing index.html and shots/')
ROOT = pathlib.Path(os.environ['ROOM_MOCK']).resolve()
url = (ROOT / 'index.html').as_uri() + '#bb10=1&bbmotion=4&bbframe=1&panel=0'
with tempfile.TemporaryDirectory(prefix='bb10-frame-', dir=ROOT / 'shots') as t:
    r = subprocess.run(['/usr/lib/chromium/chromium', '--headless=new', '--no-sandbox', '--disable-extensions', '--mute-audio',
                        '--renderer-process-limit=1', f'--user-data-dir={t}/p', '--virtual-time-budget=200000', '--dump-dom', url],
                       capture_output=True, timeout=2400)
m = re.search(rb'<pre id="bake">([^<]+)</pre>', r.stdout)
if not m:
    sys.exit(r.stderr[-1500:].decode(errors='replace'))
rows = [c for c in json.loads(html.unescape(m[1].decode())) if c.get('frame')]
print(f'{"clip":18} {"|dx| px":>8} {"cy px":>6} {"margin":>7} {"cover %":>8}   last frame (cx, cy, margin, cover, face)')
for c in rows:
    f, l = c['frame'], c['frame']['last'] or {}
    bad = ' <--' if f['dx'] > 8 or f['m'] < 0 or f['cov'] > .15 else ''
    print(f"{c['name']:18} {f['dx']:8.1f} {f['cy']:6.1f} {f['m']:7.1f} {100 * f['cov']:8.1f}   {l.get('cx')}, {l.get('cy')}, {l.get('m')}, {l.get('cov')}, {l.get('face')}{bad}")
if len(sys.argv) > 1:
    json.dump(rows, open(sys.argv[1], 'w'), indent=1)
