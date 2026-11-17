#!/usr/bin/env python3
"""Negative controls for tools/architecture/ownership-guard.py on a copy of the tree."""
import json, shutil, subprocess, sys, tempfile
from pathlib import Path
root = Path(__file__).resolve().parents[2]
guard = root / 'tools/architecture/ownership-guard.py'
exc_real = root / 'tools/architecture/ownership-exceptions.json'


def run(tree, exceptions=exc_real):
    r = subprocess.run([sys.executable, '-I', str(guard), '--strict', '--root', str(tree), '--exceptions', str(exceptions)],
                       capture_output=True, text=True)
    return r.returncode, r.stdout


def expect(label, result, ok, needle=''):
    code, out = result
    assert (code == 0) == ok and needle in out, f'{label}: rc={code}\n{out}'
    print('PASS', label)


with tempfile.TemporaryDirectory() as td:
    tree = Path(td) / 'tree'
    for d in ('app', 'platform/3ds', 'include/breadbox'):
        shutil.copytree(root / d, tree / d)
    shutil.copytree(root / 'systems/common', tree / 'systems/common')
    for d in (root / 'systems').iterdir():
        if d.is_dir() and d.name != 'common':
            (tree / 'systems' / d.name).mkdir(parents=True)
            for h in d.glob('*.h'):
                shutil.copy(h, tree / 'systems' / d.name / h.name)
    new = tree / 'app/ui/new_shared.c'

    expect('real tree passes', run(tree), True)
    expect('real repo passes', run(root), True)

    for label, text, needle in (
        ('new game_t user fails', 'static const game_t *g;\n', 'ps1-content-type'),
        ('new PCSX key fails', 'const char *k = "pcsx_rearmed_memcard1";\n', 'pcsx-key'),
        ('new BIOS policy fails', 'const char *d = "bios";\n', 'ps1-storage-policy'),
        ('new PS1 state policy fails', 'const char *d = PSX3D_STATE_DIR;\n', 'ps1-state-policy'),
        ('new private system header fails', '#include "gba_settings.h"\n', 'private-include'),
        ('new core header fails', '#include "../../core/plugins/gpulib/gpu.h"\n', 'private-include'),
        ('private PS1 header in angle brackets fails', '#include <../../systems/ps1/emu.h>\n', 'private-include'),
        ('private GBA header in angle brackets fails', '#include <../../systems/gba/gba_settings.h>\n', 'private-include'),
        ('private header with no space after include fails', '#include"../../systems/ps1/emu.h"\n', 'private-include'),
    ):
        new.write_text(text)
        expect(label, run(tree), False, needle)
    new.write_text('// game_t pcsx_rearmed_x bios in a comment are fine\nint x;\n')
    expect('comments are ignored', run(tree), True)
    new.unlink()

    # an exception freezes the approved lines: a new reference is a violation ...
    main = tree / 'app/ui/main.c'
    original = main.read_text()
    main.write_text(original + '\nstatic game_t *extra;\n')
    expect('more game_t than approved fails', run(tree), False, 'main.c')
    main.write_text(original)
    # ... and removing an approved one without shrinking the list is an error, so a cleanup
    # cannot buy room for a new reference.
    frozen = json.loads(exc_real.read_text())['exceptions']['app/ui/main.c']['approved']
    cat, line = next((c, l[0]) for c, l in frozen.items())
    gone = [l for l in original.splitlines() if ' '.join(l.split()) != line]
    assert len(gone) < len(original.splitlines()), 'approved line not found in main.c'
    main.write_text('\n'.join(gone) + '\n')
    expect('removed approved reference must leave the list', run(tree), False, 'is gone')
    main.write_text('\n'.join(gone) + '\nstatic game_t *swapped;\n')
    expect('removal does not buy room for a new reference', run(tree), False, 'not approved')
    main.write_text(original)

    # prose in a string literal is not policy; a bare word or key still is
    new.write_text('static const char *credits = "BIOS: the system uses the open BIOS by the team (GPLv2).";\n')
    expect('prose literal with BIOS passes', run(tree), True)
    new.write_text('static const char *d = "bios dir";\n')
    expect('short literal with bios fails', run(tree), False, 'ps1-storage-policy')
    new.write_text('static const char *d = "open the bios folder now";\nint memcard_slot;\n')
    expect('prose literal does not hide code on the line below', run(tree), False, 'memcard_slot')
    new.unlink()

    data = json.loads(exc_real.read_text())
    bad = Path(td) / 'bad.json'

    def variant(label, edit, needle):
        d = json.loads(json.dumps(data)); edit(d['exceptions']); bad.write_text(json.dumps(d))
        expect(label, run(tree, bad), False, needle)

    variant('exception without reason fails', lambda e: e['app/ui/main.c'].update(reason=' '), 'reason')
    variant('exception for a missing file fails', lambda e: e.update({'app/ui/nothere.c': {'reason': 'x', 'approved': {'pcsx-key': ['x']}}}), 'not shared code')
    variant('unknown category fails', lambda e: e['app/ui/main.c']['approved'].update(bogus=['x']), 'unknown category')
    bad.write_text('{not json')
    expect('malformed exception list fails', run(tree, bad), False, 'unusable')
    expect('missing exception list fails', run(tree, Path(td) / 'absent.json'), False, 'unusable')

    empty = Path(td) / 'empty'
    (empty / 'app/ui').mkdir(parents=True)
    expect('empty scan fails', run(empty), False, 'empty scan')
print('ownership guard negative controls PASS')
