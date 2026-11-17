"""No word on a list theme's action bar is ever cut, and the home tile's date fits whole.

Walks every action bar of every screen the equivalence scripts reach in PSX3D and Simple (each observation's buttons
and their cells) and measures each word at the bar's text size with the console's system-font advances
(tests/ui_fit/card_font.json, measured on an Old 3DS). The Room's bar shows icons and buttons, no words."""
from pathlib import Path
import json, re, sys
root = Path(__file__).resolve().parents[2]
style = (root / 'include/breadbox/ui_style.h').read_text()


def token(name):
    m = re.search(r'#define ' + name + r'\s+\(?([-\w.]+)', style)
    v = m.group(1).rstrip('f')
    return token(v) if re.match(r'[A-Z_]', v) else float(v)


fonts = json.loads((root / 'tests/ui_fit/card_font.json').read_text())['fonts']
console, fallback = fonts['Old3DS'], fonts['Azahar']


def width(text, size):
    return sum(console.get(c, fallback.get(c, max(fallback.values()))) for c in text) * size


caption, inset, icon = token('BAR_CAPTION'), token('UI_LOOK_FOOTER_INSET'), token('BAR_ICON_R')
screen = 320.0
out = Path(sys.argv[1])
bars = cut = 0
equal_cut = set()   # negative control: what equal cells (the old rule) would cut
for look in ('psx3d', 'simple'):
    for trace in sorted(out.glob(f'*-{look}.trace')):
        for line in trace.read_text().splitlines():
            m = re.search(r'\{"screen".*\}', line)
            if not m: continue
            ui = json.loads(m.group(0))
            # The bar sizes each cell to its word with the running font (look_bar_cells): every word fits whole when
            # the words, each with its icon's minimum and padding, fit the 320 px together at console widths.
            words = [label or ('OK' if key == 1 else '') for key, label in ui.get('buttons', [])]
            if not words: continue
            bars += 1
            need = sum(max(width(w, caption), 2 * icon) + inset for w in words)
            equal_cut.update(w for w in words if width(w, caption) > screen / len(words) - inset)
            if need > screen:
                cut += 1
                print(f'CUT {look} {trace.stem} {ui.get("screen")}: {words} need {need:.1f} px of {screen:.0f}')
# The home tile's date: the longest month and time, alone on its line beside the Continue picture.
room = token('UI_LOOK_TILE_W') - 2 * token('UI_LOOK_TILE_INSET') - token('UI_LOOK_THUMB_W') - token('UI_LOOK_THUMB_GAP')
for date in ('Sep 30 2026 23:59', 'May 28 2026 08:08', 'Nov 17 2026 20:44'):
    if width(date, token('UI_TEXT_FLOOR')) > room:
        cut += 1
        print(f'CUT home date "{date}" needs {width(date, token("UI_TEXT_FLOOR")):.1f} px, tile has {room:.1f}')
if not equal_cut: sys.exit('FAIL: negative control: equal cells cut nothing, so the walk misses the crowded bars')
print('Negative control: equal cells would cut', sorted(equal_cut))
if cut or not bars: sys.exit(f'FAIL: {cut} cut of {bars} action bars')
print(f'PASS: {bars} action bars on every list-theme screen and the home tile date fit whole at console metrics')
