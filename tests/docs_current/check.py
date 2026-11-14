#!/usr/bin/env python3
"""Documentation must not describe retired targets or an old system ABI."""
import re
from pathlib import Path
from urllib.parse import unquote, urlsplit
root = Path(__file__).resolve().parents[2]
abi = re.search(r'#define\s+BB_SYSTEM_ABI\s+(\d+)', (root / 'include/breadbox/bb_system.h').read_text()).group(1)
layer = (root / 'docs/dev/system-layer.md').read_text()
assert f'`BB_SYSTEM_ABI` is **{abi}**' in layer, f'system-layer.md must name ABI {abi}'
assert f'(ABI {abi})' in layer
# Current build docs: no APP switch, no PSX3D build target.
retired = re.compile(r'make (?:-j\d+ )?APP=|\bmake both\b|APP=psx3d bash|psx3d/PSX3D\.(?:3dsx|elf|cia)')
for name in ('README.md', 'docs/building.md', 'docs/dev/new-system.md'):
    text = (root / name).read_text()
    # building.md names the rejected switch once, on purpose.
    text = text.replace('`APP=psx3d` is rejected', '')
    m = retired.search(text)
    assert not m, f'{name}: retired build instruction {m.group(0)!r}'
readme = (root / 'README.md').read_text()
assert 'real hardware qualification is pending' not in readme
assert 'built from the same source tree as PSX3D' not in readme
assert 'Game Boy Advance has its own emulator and renderer' in readme, 'README must say GBA has its own core and renderer'

# Every relative Markdown link in tracked docs must point to a checked-in target.
import subprocess
tracked = subprocess.check_output(['git', 'ls-files', '*.md'], cwd=root, text=True).splitlines()
links = re.compile(r'!?\[[^]]*\]\(([^)]+)\)')
dead = []
for name in tracked:
    path = root / name
    for target in links.findall(path.read_text(errors='replace')):
        target = target.strip().split()[0].strip('<>')
        parsed = urlsplit(target)
        if parsed.scheme or target.startswith('#'):
            continue
        dest = (path.parent / unquote(parsed.path)).resolve()
        if not dest.exists():
            dead.append(f'{name}: {target}')
assert not dead, 'broken relative Markdown links:\n' + '\n'.join(dead)
print('docs describe the current build and ABI and relative links PASS', abi)
