#!/usr/bin/env python3
"""Per-frame motion check of every hand clip (round 7, owner: "the animations are so rough").

Runs the mock's clip simulation (#bb10=1&bbmotion=N: the same runs as the BBH1 bake, sampled every N sim steps,
2 = 60 Hz, 4 = 30 Hz) in ONE headless Chromium and prints, per clip, the largest move (mm, clip space) of any
hand vertex and of any corner of the held object between two samples, and when it happens.
Limit: 30 mm per 60 Hz frame.
Usage: ROOM_MOCK=/path/to/ui-mock motion.py [60|30] [OUT.json]"""
import html, json, os, pathlib, re, subprocess, sys, tempfile

if 'ROOM_MOCK' not in os.environ:
    sys.exit('set ROOM_MOCK to the UI mock directory containing index.html and shots/')
ROOT = pathlib.Path(os.environ['ROOM_MOCK']).resolve()
hz = int(sys.argv[1]) if len(sys.argv) > 1 else 60
url = (ROOT / 'index.html').as_uri() + f'#bb10=1&bbmotion={120 // hz}&panel=0'
with tempfile.TemporaryDirectory(prefix='bb10-motion-', dir=ROOT / 'shots') as t:
    r = subprocess.run(['/usr/lib/chromium/chromium', '--headless=new', '--no-sandbox', '--disable-extensions', '--disable-background-networking',
                        '--mute-audio', '--renderer-process-limit=1', f'--user-data-dir={t}/p', '--virtual-time-budget=120000', '--dump-dom', url],
                       capture_output=True, timeout=1500)
m = re.search(rb'<pre id="bake">([^<]+)</pre>', r.stdout)
if not m:
    sys.exit((r.stderr[-1500:]).decode(errors='replace'))
rows = json.loads(html.unescape(m[1].decode()))
lim = 30 * 60 / hz
print(f'{"clip":18} {"hand mm":>8} {"at s":>7} {"obj mm":>7} {"at s":>7} {"case x range":>12} {"loop seam pos/vel mm":>21}   ({hz} Hz, limit {lim:.0f} mm)')
for c in rows:
    flag = ' <-- over' if c['hand'] > lim or c['obj'] > lim else ''
    seam = f"{c['seam']:.3f} / {c['seamV']:.3f}" if 'seam' in c else ''
    print(f"{c['name']:18} {c['hand']:8.1f} {c['handAt']:7.2f} {c['obj']:7.1f} {c['objAt']:7.2f} {c.get('lat', 0):12.2f} {seam:>21}{flag}")
print('max hand', max(c['hand'] for c in rows), 'max object', max(c['obj'] for c in rows))
if len(sys.argv) > 2:
    json.dump(rows, open(sys.argv[2], 'w'), indent=1)
