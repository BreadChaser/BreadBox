#!/usr/bin/env python3
"""Exercise the actual CI baseline entry with verified and deliberately broken inputs."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/layout'))
import compare


def run(*args, **overrides):
    env = dict(os.environ)
    for key in ('BREADBOX_BASELINE_ELF', 'BASELINE_META', 'ACCEPTED_CHANGES'):
        env.pop(key, None)
    env['BB_LAYOUT_TEST'] = '1'
    env.update({key: str(value) for key, value in overrides.items()})
    return subprocess.run(['bash', 'tools/build/ci.sh', *args], cwd=ROOT, env=env, capture_output=True, text=True)


def fails(result, message):
    assert result.returncode != 0, result.stdout + result.stderr
    assert message in result.stdout + result.stderr, result.stdout + result.stderr


meta = json.loads((ROOT / 'tools/layout/baseline.json').read_text())
new = ROOT / 'build/3ds/BreadBox.elf'
assert new.is_file(), 'build first: CI baseline controls need the real ELF and objects'
# Resolve the real independently verified reference, then explicitly prevent builds
# in every negative case. No mock comparator or fake make can make these tests green.
result = subprocess.run([sys.executable, '-I', 'tools/layout/reference.py'], cwd=ROOT, capture_output=True, text=True)
assert result.returncode == 0, result.stderr
base = Path(result.stdout.strip())
(ROOT / 'artifacts').mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='baseline-controls-', dir=ROOT / 'artifacts') as tmp:
    tmp = Path(tmp)
    empty = tmp / 'empty-cache'
    fails(run('layout', BASELINE_BUILD_REFERENCE=0, LAYOUT_REFERENCE_OUT=empty), 'accepted reference missing')
    fails(run('baseline', BREADBOX_BASELINE_ELF=tmp / 'absent.elf'), 'does not exist')
    foreign = tmp / 'foreign.elf'
    foreign.write_bytes(new.read_bytes())
    image = compare.load(str(new))
    address = image['syms']['video_present_screens'][0]
    raw = bytearray(new.read_bytes())
    for va, offset, size, _ in image['secs']:
        if va <= address < va + size:
            raw[offset + address - va] ^= 1
            break
    foreign.write_bytes(raw)
    fails(run('baseline', BREADBOX_BASELINE_ELF=foreign), 'not the qualified public reference')
    for key in ('identity_sha256', 'commit', 'qualification_sha256'):
        altered = copy.deepcopy(meta)
        altered['public_reference'][key] = '0' * len(altered['public_reference'][key])
        path = tmp / (key + '.json')
        path.write_text(json.dumps(altered))
        messages = {'identity_sha256': 'not the qualified public reference',
                    'commit': 'reference acceptance/source identity changed',
                    'qualification_sha256': 'build-2650 qualification manifest identity changed'}
        fails(run('baseline', BASELINE_META=path, BREADBOX_BASELINE_ELF=base), messages[key])
        fails(run('baseline', BASELINE_META=path, BB_LAYOUT_TEST=0), 'BASELINE_META requires BB_LAYOUT_TEST=1')
    # Wrong download digest is rejected before extraction/build; no network used.
    cache = tmp / 'bad-archive'
    cache.mkdir()
    (cache / 'source.tar.gz').write_bytes(b'foreign source')
    fails(run('baseline', LAYOUT_REFERENCE_OUT=cache), 'source archive has wrong digest')
    # The public artifact is valid, but the candidate's same-size opcode flip is red
    # through exactly the CI entry used by the layout stage.
    (tmp / 'build-breadbox').symlink_to(ROOT / 'build/3ds/build-breadbox', target_is_directory=True)
    fails(run('layout', str(foreign), BREADBOX_BASELINE_ELF=base), 'FAIL code video_present_screens')
    # The whole-image function diff sees the same flip and nothing else.
    tool = [sys.executable, str(ROOT / 'tools/layout/function-diff.py')]
    same = subprocess.run(tool + [str(new), str(new)], capture_output=True, text=True)
    assert same.returncode == 0 and same.stdout.strip().endswith(' 0 differ'), same.stdout + same.stderr
    flipped = subprocess.run(tool + [str(new), str(foreign)], capture_output=True, text=True)
    assert flipped.returncode == 1 and len(flipped.stdout.splitlines()) == 2, flipped.stdout
    assert flipped.stdout.startswith('code     video_present_screens  1 changed words of '), flipped.stdout
    assert flipped.stdout.strip().endswith(' 1 differ'), flipped.stdout
    # Removing a required pin from the ELF must fail, independent of object names.
    missing = tmp / 'missing.elf'
    missing.write_bytes(new.read_bytes().replace(b'\0video_present_screens\0', b'\0video_present_screenz\0'))
    fails(run('layout', str(missing), BREADBOX_BASELINE_ELF=base), 'video_present_screens is in the baseline but gone from NEW')
    result = run('baseline', BREADBOX_BASELINE_ELF=base)
    assert result.returncode == 0, result.stdout + result.stderr
    assert 'verified public 0.9.0 reference' in result.stdout
print('CI baseline: verified public reference passes; missing/foreign/stale/digest/opcode/pin controls fail PASS')
