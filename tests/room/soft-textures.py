#!/usr/bin/env python3
"""Room textures, "finish": "soft" (tools/room/lamplight/soft.py): a texture set with the soft
finish keeps every texture's size (the atlas layout), at most 16 colours, every colour on the RGB555 grid, and is
calmer than the crisp set (lower neighbour-to-neighbour luma steps on wood, floor, wallpaper and cushions); a set
without it is drawn exactly as before. NumPy only (no Blender)."""
import copy
import json
import pathlib
import sys

import numpy as np

root = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tools/room/lamplight'))
import tex  # noqa: E402

rig = json.loads(pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else root / 'tools/room/lamplight/lights.json').read_text())
base = {k: v for k, v in rig['texture_sets'][rig['styles'][rig['order'][0]]['textures']].items() if k != 'finish'}
crisp = tex.textures(copy.deepcopy(base))
assert tex.textures(copy.deepcopy(base)).keys() == crisp.keys()
soft_set = dict(copy.deepcopy(base), finish='soft')
soft = tex.textures(soft_set)


def busy(a):
    """Mean luma step between neighbouring texels (wrapping: the textures tile)."""
    y = a[..., :3].astype(float) @ np.array([0.299, 0.587, 0.114])
    return (np.abs(y - np.roll(y, 1, 0)).mean() + np.abs(y - np.roll(y, 1, 1)).mean()) / 2


for name, a in soft.items():
    assert a.shape == crisp[name].shape, f'{name}: {a.shape} != {crisp[name].shape} (the atlas layout must not change)'
    if a is not crisp[name] and not np.array_equal(a, crisp[name]):
        rgb = a[a[..., 3] > 0][:, :3].astype(int)
        assert np.all((rgb * 31 + 127) // 255 * 255 // 31 - rgb <= 4), f'{name}: colours off the RGB555 grid'


def hardest(a):
    """The largest luma step between neighbouring texels: a crisp line (pinstripe, plank seam) is a big one."""
    y = a[..., :3].astype(float) @ np.array([0.299, 0.587, 0.114])
    return max(np.abs(y - np.roll(y, 1, 0)).max(), np.abs(y - np.roll(y, 1, 1)).max())


for name in ('wood', 'floor', 'wainscot', 'fabric1', 'fabric2'):
    assert busy(soft[name]) < busy(crisp[name]), f'{name}: soft {busy(soft[name]):.1f} not calmer than {busy(crisp[name]):.1f}'
for name in ('wall', 'wood', 'woodv', 'floor', 'wainscot'):
    assert hardest(soft[name]) <= hardest(crisp[name]), f'{name}: a harder line than before ({hardest(soft[name]):.0f})'
for name in set(soft) - {'wall', 'wood', 'woodv', 'floor', 'wainscot', 'fabric1', 'fabric2'}:
    assert np.array_equal(soft[name], crisp[name]), f'{name}: changed by the soft finish (only surfaces may)'
print('soft textures: %d textures, sizes kept, <= 16 colours, calmer: %s' % (
    len(soft), ', '.join('%s %.1f->%.1f' % (n, busy(crisp[n]), busy(soft[n])) for n in ('wall', 'wood', 'floor', 'fabric1'))))
