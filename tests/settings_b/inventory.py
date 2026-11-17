"""The settings inventory against the immutable v1-r2 labels (GOLDEN.md): nothing lost, nothing doubled.

Every frozen PS1 and GBA row is still in its system's list, in order, except the shared shortcuts, which moved to
their one home, Settings > Customize. Those must be on the Customize page the
production shell builds for that console (model.c prints it), and nowhere in the system's lists.
usage: inventory.py FROZEN_PS1_TSV CANDIDATE_PS1_TSV GBA_RESULTS MODEL_RESULTS"""
from pathlib import Path
import json, sys
root = Path(__file__).parent
frozen_ps1, cand_ps1, gba_out, model_out = map(Path, sys.argv[1:5])
customize = {}
for line in model_out.read_text().splitlines():
    if line.startswith('CUSTOMIZE\t'):
        _, newer, label = line.split('\t'); customize.setdefault(int(newer), []).append(label)
assert set(customize) == {0, 1}, customize
for newer, labels in customize.items():
    assert len(labels) == len(set(labels)), labels                  # nothing doubled on Customize
# PS1: (kind, frozen label) -> Customize label
PS1_MOVED = {('model', 'Bottom-screen buttons'): 'Touch buttons',   # renamed on Customize: the old label cannot fit beside its value
             ('binding', 'Fast forward'): 'FF keys', ('binding', 'Rewind'): 'Rewind keys'}
def rows(path):
    out = {}
    for line in path.read_text().splitlines():
        newer, scope, debug, kind, label = line.split('\t'); out.setdefault((int(newer), scope, debug), []).append((kind, label))
    return out
frozen, cand = rows(frozen_ps1), rows(cand_ps1)
assert set(frozen) == set(cand), (set(frozen) ^ set(cand))
moved_seen = set()
for ctx, fr in frozen.items():
    want = [r for r in fr if r not in PS1_MOVED]
    assert cand[ctx] == want, (ctx, [r for r in want if r not in cand[ctx]], [r for r in cand[ctx] if r not in want])
    for r in fr:
        if r in PS1_MOVED:
            assert PS1_MOVED[r] in customize[ctx[0]], (ctx, r)       # moved, not lost
            assert all(l != PS1_MOVED[r] for _, l in cand[ctx]), (ctx, r)   # and not doubled in the system list
            moved_seen.add((ctx[0], PS1_MOVED[r]))
# GBA: the shortcut rows (frozen 'Fast forward' and the 'Rewind' after it) moved
expected = json.loads((root / 'v1-r2-gba-rows.json').read_text())['contexts']
actual = {k: {'model': [], 'binding': []} for k in expected}
for line in gba_out.read_text().splitlines():
    if line.startswith('GBALEDGER\t'):
        _, context, kind, label = line.split('\t'); actual[context][kind].append(label)
for context, fr in expected.items():
    newer = int(context.split('/')[0]); want, after_ff = [], False
    for label in fr['model']:
        if label == 'Fast forward': moved = 'FF keys'; after_ff = True
        elif label == 'Rewind' and after_ff: moved = 'Rewind keys'
        else: want.append(label); continue
        assert moved in customize[newer], (context, label)
        assert moved not in actual[context]['model'], (context, label)
        moved_seen.add((newer, moved))
    assert actual[context]['model'] == want, (context, actual[context]['model'], want)
    assert actual[context]['binding'] == fr['binding'], context
# and Customize holds nothing else but the room's own rows
for newer, labels in customize.items():
    extra = [l for l in labels if (newer, l) not in moved_seen and l not in ('Theme', 'Hands')]
    assert not extra, (newer, extra)
print('Settings inventory PASS: %d PS1 contexts and %d GBA contexts keep every frozen row once; the shared shortcuts '
      '(%s) moved to Customize, nowhere else' % (len(frozen), len(expected), ', '.join(sorted({l for _, l in moved_seen}))))
