#!/usr/bin/env python3
"""The room's place cards before A (owner 2026-11-12, app/ui/place_cards.c): Collection's counts per system, Settings'
version / updates / room style, Quit's play today and last game.

check.c runs the production logic (counts from a fake index: two systems, one system only, 0 games known and
not yet known, the first scan's partial counts, huge numbers, more systems than lines; play-time spelling; the
card gets no line until something is known) and prints every line it makes. Here each printed line, and the
shipped strings (every system's name from systems/*/sys_*.c, the version from version.h, every update state from
update.c's bb_update_glance, the room styles from theme.c), must fit the room card's info row at real metrics:
label + gap + value inside the ruled width at the text floor (shelf_scene.c info_row), with the conservative font
model of tests/ui_fit/strings.py, never shrunk below the floor or cut. The last game's name is the one line that may
be cut (a long title; it has the whole line). A negative control (a too-long label) must fail.
"""
import ast, re, subprocess, sys, tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
src = (root / 'tests/ui_fit/strings.py').read_text()
ns = {}
for node in ast.parse(src).body:   # the shared width model, without running that suite
    if isinstance(node, ast.Assign) and any(getattr(t, 'id', '') in ('NARROW', 'SLIM', 'WIDE', 'HUGE') for t in getattr(node.targets[0], 'elts', [])):
        exec(compile(ast.Module([node], []), 'strings.py', 'exec'), ns)
    if isinstance(node, ast.FunctionDef) and node.name in ('width', 'style'):
        exec(compile(ast.Module([node], []), 'strings.py', 'exec'), ns)
ns['root'], ns['re'] = root, re
width, style = ns['width'], ns['style']
FLOOR = style('UI_TEXT_FLOOR_PX')
BOX = style('UI_ROOM_TEXT_R') - (style('UI_ROOM_MARGIN_X') + style('UI_PAPER_MARGIN_GAP'))
GAP = style('UI_ROOM_GAP')


def shipped():
    args = []
    for f in sorted((root / 'systems').glob('*/sys_*.c')):
        m = re.search(r'\.name="([^"]+)",\.short_name="([^"]+)"', f.read_text())
        if m: args += ['system', m[1], m[2], '']
    version = re.search(r'#define BB_VERSION "([^"]+)"', (root / 'apps/breadbox/version.h').read_text())[1]
    upd = (root / 'app/services/update.c').read_text()
    body = upd[upd.index('const char *bb_update_glance(void)'):]
    body = body[:body.index('\n}\n')]
    states = re.findall(r'"([^"]+)"', body)
    styles = re.findall(r'\.name = "([^"]+)"', (root / 'app/ui/theme.c').read_text())
    assert len(states) >= 4 and len(styles) >= 2, (states, styles)
    for v in (version, '0.9.0', '10.10.10'):
        for s in states + ['']:
            for st in styles:
                args += ['settings', v, s, st]
    return args, len(states), styles


def build(d, extra=''):
    exe = Path(d) / 'place_cards'
    cmd = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g', '-I' + str(root / 'include/breadbox'),
           '-I' + str(root / 'app/ui'),
           str(root / 'tests/place_cards/check.c'), str(root / 'app/ui/place_cards.c'), '-o', str(exe)]
    subprocess.run(cmd, check=True)
    return exe


def check(lines, last_games):
    bad = []
    for card, label, value in lines:
        if card == 'quit' and label in last_games:
            continue   # the last game's name: its own line, cut with "..." when a title is longer
        lw, vw = width(label, FLOOR), width(value, FLOOR) if value else 0
        need = lw + (GAP + vw if value else 0)
        if need > BOX:
            bad.append(f'{card}: "{label}" ... "{value}" needs {need:.0f} px of {BOX:.0f}')
    return bad


with tempfile.TemporaryDirectory() as d:
    args, nstates, styles = shipped()
    assert args.count('system') >= 2, 'no system names found'
    out = subprocess.run([build(d)] + args, check=True, capture_output=True, text=True).stdout
    lines = [l.split('\t') for l in out.splitlines()]
    bad = check(lines, {'Crash Bandicoot', 'Supercalifragilisticexpialidociousnamewithoutspaces'})
    if check([('settings', 'Room style of a much longer name here', 'Night')], set()) == []:
        bad.append('negative control: a too-long label passed')
    for b in bad: print('FAIL', b)
    if bad: sys.exit(1)
    print(f'Place cards PASS: {len(lines)} lines (counts per system, total and favorites incl. 65,535 per system; settings for '
          f'{nstates} update states x {len(styles)} room styles x 3 versions; play today) fit the room card at the floor; '
          'nothing drawn until the scan knows; one system has no total')
