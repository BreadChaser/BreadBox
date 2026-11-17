"""Stop on the first differing production-controller observation."""
from pathlib import Path
import sys
root = Path(sys.argv[1])
for controller in ('home', 'settings', 'shelf', 'dialogs', 'pause', 'cards', 'welcome', 'share', 'ps1_pause', 'features', 'browser', 'settings_systems', 'ps1_slots'):
    expected = (root/f'{controller}-day.trace').read_text().splitlines()
    assert expected, controller
    for look in ('psx3d', 'simple'):
        actual = (root/f'{controller}-{look}.trace').read_text().splitlines()
        for step in range(max(len(expected), len(actual))):
            a = expected[step] if step < len(expected) else '<ended>'
            b = actual[step] if step < len(actual) else '<ended>'
            if a != b:
                sys.exit(f'{controller}, {look}, first divergence at step {step}:\nRoom: {a}\n{look}: {b}')
    print(f'PASS: {controller}: {len(expected)} observations, three looks, zero differences')
