"""Stop on the first differing production-controller observation.

Every controller behaves the same in the three looks, except two on purpose: the home screen (the Room walks its
room's map and glides to a place; the list looks move between their tiles as they stand on screen and open Continue
and Collection on A, tests/looks/check.c home_move_checks) and the game list (the Room's shelf runs left to right;
in the list looks' A-Z list Left / Right step letters, as L / R do). The list looks size their bar cells to their
words (the Room's cells are equal), so Room-to-list comparisons leave the bar cells' geometry out; PSX3D and Simple
are always identical."""
from pathlib import Path
import re, sys
BAR_CELL = re.compile(r',?\[-\d+,-?[\d.]+,-?[\d.]+,-?[\d.]+,-?[\d.]+\]')
root = Path(sys.argv[1])
LIST_ONLY = {'home', 'shelf'}


def compare(controller, base, look):
    expected = (root/f'{controller}-{base}.trace').read_text().splitlines()
    actual = (root/f'{controller}-{look}.trace').read_text().splitlines()
    if base == 'day':
        expected = [BAR_CELL.sub('', l) for l in expected]; actual = [BAR_CELL.sub('', l) for l in actual]
    assert expected, controller
    for step in range(max(len(expected), len(actual))):
        a = expected[step] if step < len(expected) else '<ended>'
        b = actual[step] if step < len(actual) else '<ended>'
        if a != b:
            sys.exit(f'{controller}, {look}, first divergence at step {step}:\n{base}: {a}\n{look}: {b}')
    return len(expected)


for controller in ('home', 'settings', 'shelf', 'dialogs', 'pause', 'cards', 'welcome', 'share', 'ps1_pause', 'features', 'browser', 'settings_systems', 'ps1_slots'):
    pairs = [('psx3d', 'simple')] if controller in LIST_ONLY else [('day', 'psx3d'), ('day', 'simple')]
    n = 0
    for base, look in pairs:
        n = compare(controller, base, look)
    which = 'PSX3D and Simple' if controller in LIST_ONLY else 'three looks'
    print(f'PASS: {controller}: {n} observations, {which}, zero differences')
