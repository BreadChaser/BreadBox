"""Every row of the Settings shell's pages fits its card row (label never cut, value never cut), on the
Azahar-calibrated width model of tests/ui_fit/strings.py at the 21 px floor (its stand-in font is wider than the 3DS
font, so what fits here fits on the console), with card_row_fit's arithmetic (ui.c). The Systems page's rows are the
registered systems' names (systems/*/sys_*.c). usage: fit.py MODEL_RESULTS"""
import re, sys
from pathlib import Path
root = Path(__file__).resolve().parents[2]
NARROW, SLIM, WIDE, HUGE = set("il.,:;'|!I "), set('frtj()/-"1'), set('ABCDEFGHJKLNOPQRSTUVXYZmw+'), set('MW')
def width(s, px=21):   # tests/ui_fit/strings.py's model without its extra 5 %: the stand-in font as measured, plus 4 %
    w = 0
    for ch in s:
        w += 6 if ch in NARROW else 7 if ch in SLIM else 16 if ch in HUGE or ord(ch) > 127 else 13 if ch in WIDE else 10
    return w * px / 21
RIGHT, TEXT_X, NUDGE, ARROW = 9 + 302 - 12, 48 + 6, 2, 3.5
def fits(label, value, arrows):
    vx = RIGHT
    if value:
        vroom = (RIGHT - TEXT_X) - min(width(label), (RIGHT - TEXT_X) * .5) - 8 - ((ARROW * 3 + 4 + ARROW * 2) if arrows else 0)
        if width(value) > vroom: return False, 'value'
        vx = RIGHT - (ARROW * 3 if arrows else 0) - width(value) - 8
    return (width(label) <= vx - TEXT_X - NUDGE), 'label'
rows = [l.split('\t')[1:] for l in Path(sys.argv[1]).read_text().splitlines() if l.startswith('FITROW\t')]
# the Systems page shows a name, or its short name where the name would be cut (settings_ui.c system_label)
src = ''.join(p.read_text() for p in sorted(root.glob('systems/*/sys_*.c')))
names = [n if fits(n, '>', False)[0] else short for n, short in re.findall(r'\.name="([^"]+)",\s*\.short_name="([^"]+)"', src)]
assert len(names) >= 2, names
rows += [['-', 'systems', n, '>', '0'] for n in names]
bad = [(r, why) for r in rows for ok, why in [fits(r[2], r[3], r[4] == '1')] if not ok]
for r, why in bad: print('CUT', why, r)
assert not bad
print('Settings fit PASS: %d shell rows (top, Customize Old/New, Hands, Collection, About) and %d system names fit their card rows' % (len(rows) - len(names), len(names)))
