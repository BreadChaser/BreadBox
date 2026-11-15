#!/usr/bin/env python3
"""J-05: every line the Updates card can show fits its one foot line at the text floor, with no
codes. A rough glyph-width model (wide capitals/m/w 1.35, narrow i/l/t/punctuation 0.55,
others 1.0) calibrated on emulator stills of the card at 0.7: "No connection. Check Wi-Fi"
(23.0) fits; "Bad download. Try tomorrow" (23.4) was cut. Lines must stay at or under 23.0."""
import re, sys
from pathlib import Path
src = (Path(__file__).resolve().parents[2] / 'app/services/update.c').read_text()
table = re.search(r'update_status_text\[\]=\{(.*?)\};', src, re.S).group(1)
body = src[src.index('static const char *update_settings_status(void)\n{'):]
body = body[:body.index('\n}\n')]
why = src[src.index('static const char *update_unavailable(void)\n{'):]
why = why[:why.index('\n}\n')]
progress = src[src.index('static bool update_progress_text('):]
progress = progress[:progress.index('\n}\n')]
lines = re.findall(r'"((?:[^"\\]|\\.)*)"', table) + re.findall(r'return "((?:[^"\\]|\\.)*)"', body + why) + \
    [re.sub(r'%u%%', '100%', t) for t in re.findall(r'"((?:[^"\\]|\\.)*)"', progress)]
wide=set('mwMWDOQGCNHUABKRV');narrow=set("iltfjr.,' I:;-!|")
# Exact twentieth units keep the calibrated boundary identical across Python
# versions (sum() gained compensated floating-point summation in Python 3.12).
width=lambda t: sum(27 if c in wide else 11 if c in narrow else 20 for c in t)
assert width("No connection. Check Wi-Fi") == 460
assert width("Bad download. Try tomorrow") == 467
bad = [l for l in lines if width(l) > 460 or re.search(r'\((network|hash|manifest|install|SD card)\)|\bAM\b|SHA|CIA|Last check', l)]
for l in bad:
    print('too wide or technical:', repr(l), round(width(l) / 20, 1))
print('update lines: %d, %s' % (len(lines), 'FAIL' if bad else 'PASS'))
sys.exit(1 if bad or len(lines) < 18 else 0)
