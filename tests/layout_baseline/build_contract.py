#!/usr/bin/env python3
"""The real build driver must retain caller job tokens and remove owned system products."""
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'tools/build'))
from jobs import make_parallelism

assert make_parallelism({'BB_BUILD_JOBS': '2'}) == (['-j2'], ())
assert make_parallelism({'MAKELEVEL': '1', 'MAKEFLAGS': ''}) == ([], ())
assert make_parallelism({'MAKEFLAGS': '--jobserver-auth=fifo:/tmp/test'}) == ([], ())
for invalid in ('0', '-1', 'all', '2;false'):
    try:
        make_parallelism({'BB_BUILD_JOBS': invalid})
        raise AssertionError('invalid jobs accepted')
    except ValueError:
        pass
# Actual recursive make invocation through the Python production driver helper.
# GNU make rejects unavailable descriptors; preserving a flag alone is insufficient.
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    child = td / 'child.mk'
    child.write_text('all:\n\t@echo "$(MAKEFLAGS)"\n')
    runner = td / 'run.py'
    runner.write_text('import os,sys,subprocess\nsys.path.insert(0,sys.argv[1])\n'
                      'from jobs import make_parallelism\n'
                      'args,fds=make_parallelism(os.environ)\n'
                      'assert not args\n'
                      'subprocess.run(["make","-f",sys.argv[2],*args],pass_fds=fds,check=True)\n')
    parent = td / 'parent.mk'
    parent.write_text(f'all:\n\t+{sys.executable} {runner} {ROOT / "tools/build"} {child}\n')
    result = subprocess.run(['make', '-j2', '-f', str(parent)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert '-j2' in result.stdout and 'jobserver' in result.stdout, result.stdout
    assert 'jobserver unavailable' not in result.stderr and 'resetting jobserver' not in result.stderr, result.stderr
    assert "'-j4'" not in (ROOT / 'tools/build/core-blob.py').read_text()
# Dry run the real clean target; execution/identity is separately checked after a
# complete target build, so a host test cannot delete another test's build inputs.
result = subprocess.run(['make', '-n', 'clean'], cwd=ROOT, capture_output=True, text=True)
assert result.returncode == 0, result.stdout + result.stderr
assert 'rm -rf build/systems' in result.stdout, result.stdout
ci = (ROOT / 'tools/build/ci.sh').read_text()
assert '$(nproc)' not in ci and 'BB_BUILD_JOBS' in ci
assert 'HOST_TEST_JOBS=${HOST_TEST_JOBS:-}' in ci
host = (ROOT / 'tools/build/host-tests.sh').read_text()
assert 'jobs=${HOST_TEST_JOBS:-$default_jobs}' in host
assert 'HOST_TEST_ALLOW_SKIPS' not in host
assert 'run baseline-gate' not in host and 'run build-contract' in host
assert 'tests/layout_baseline/check.py' in ci
# The growth fixture copies include/, which holds the title-version contract Share uses.
layout = (ROOT / 'tools/layout/check.sh').read_text()
assert ' app platform include assets ' in layout
assert (ROOT / 'include/breadbox/title_version.h').is_file()
assert '#include "title_version.h"' in (ROOT / 'app/services/share.c').read_text()
print('caller jobserver/standalone limits and complete clean contract PASS')
# Public notices keep their authors/licenses while naming real current paths.
building = (ROOT / 'docs/building.md').read_text()
assert '(16 + 16 * major + minor) * 1024 + patch * 16 + 15' in building
assert 'Settings > About > Welcome' in (ROOT / 'docs/dev/code-map.md').read_text()
room = (ROOT / 'assets/romfs/room/CREDITS.md').read_text()
assert 'tools/room/lamplight/' in room and (ROOT / 'tools/room/lamplight').is_dir()
assert 'room3d/blender/' not in room and 'see build.py' not in room
assert 'scribbletoad' in room and 'CC BY 4.0' in room and 'Changes made' in room
art = (ROOT / 'tools/art/CREDITS.md').read_text()
assert 'not for reuse outside PSX3D' not in art
assert 'Dreamstale' in art and 'Flaticon license terms' in art and 'not** covered by BreadBox' in art
history = (ROOT / 'tools/layout/accepted-2650.json').read_text()  # reviewer credits: tests/repo_hygiene
assert '627-case probe' in history and 'no state format change' in history
print('current version/build/navigation paths and retained asset/qualification notices PASS')

# Extraction must work in the pinned Python too, without newer tar filter APIs.
import io, tarfile
sys.path.insert(0, str(ROOT / 'tools/layout'))
import reference
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    for name, kind in [('source/file', tarfile.REGTYPE), ('source/../escape', tarfile.REGTYPE),
                       ('/absolute', tarfile.REGTYPE), ('source/link', tarfile.SYMTYPE)]:
        archive = td / 'source.tar'
        with tarfile.open(archive, 'w') as tar:
            item = tarfile.TarInfo(name); item.type = kind
            item.size = 1 if kind == tarfile.REGTYPE else 0
            tar.addfile(item, io.BytesIO(b'x') if item.size else None)
        if name == 'source/file':
            target = td / 'out'; target.mkdir()
            reference.unpack(archive, target)
            assert (target / 'file').read_bytes() == b'x'
        else:
            try:
                reference.unpack(archive, td / 'out')
                raise AssertionError('unsafe archive member accepted')
            except ValueError:
                pass
print('pinned-Python public archive extraction and unsafe-member controls PASS')

# The public recipe asks Git at the archive root, where a discovery ceiling
# alone does not prevent inheriting the containing clone's version string.
with tempfile.TemporaryDirectory() as td:
    parent = Path(td)
    subprocess.run(['git', 'init', '-q', str(parent)], check=True)
    subprocess.run(['git', '-C', str(parent), '-c', 'user.name=Fixture',
                    '-c', 'user.email=fixture@example.invalid', 'commit', '-q',
                    '--allow-empty', '-m', 'Parent repository'], check=True)
    target = parent / 'reference'
    (target / 'build/3ds').mkdir(parents=True)
    command = ['git', '-C', str(target / 'build/3ds/../..'), 'describe', '--always']
    old_env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(target))
    old_env.pop('GIT_DIR', None)
    inherited = subprocess.run(command, env=old_env, capture_output=True, text=True)
    assert inherited.returncode == 0, inherited.stderr
    meta = {'source_date_epoch': 123, 'commit': '39148a3c0e2a84ec82ce4ff95a14e9369c1162bd'}
    env = reference.build_environment(meta, target)
    isolated = subprocess.run(command, env=env, capture_output=True, text=True)
    assert isolated.returncode != 0, isolated.stdout
    assert env['PSX3D_DESCRIBE'] == meta['commit'][:12]
    assert env['SOURCE_DATE_EPOCH'] == '123'
    assert env['LC_ALL'] == 'C'
print('public reference ignores containing clone metadata; old ceiling control reproduces leak PASS')

# Run production setup with a read-only empty home and no cache, as on a fresh
# runner: groups use the runner's own TMPDIR/XDG_CACHE_HOME and leave nothing behind.
# Exercises the IM6 dispatcher where installed, the GNU strcasestr fixture and
# the suites that once wrote temp folders under ~/.cache.
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    home = td / 'home'
    home.mkdir(mode=0o500)
    env = {k: v for k, v in os.environ.items() if k not in ('XDG_CACHE_HOME', 'TMPDIR')}
    env.update(HOME=str(home), HOST_TEST_OUT=str(td / 'logs'))
    try:
        for suite in ('art-smdh24', 'welcome', 'ui-strings', 'ps1-back'):
            result = subprocess.run(['bash', 'tools/build/host-tests.sh', suite], cwd=ROOT,
                                    env=env, capture_output=True, text=True)
            assert result.returncode == 0, result.stdout + result.stderr
            assert '1 passed, 0 failed' in result.stdout, result.stdout
            parallel = subprocess.run(['bash', 'tools/build/host-tests.sh', 'art-smdh24'], cwd=ROOT,
                                      env=dict(env, HOST_TEST_JOBS='2'), capture_output=True, text=True)
            assert parallel.returncode == 0, parallel.stdout + parallel.stderr
            assert '1 passed, 0 failed, 0 skipped' in parallel.stdout, parallel.stdout
        assert not list(home.iterdir())
        assert not (td / 'logs' / 'scratch').exists() or not any((td / 'logs' / 'scratch').iterdir())
    finally:
        home.chmod(0o700)
    none = subprocess.run(['bash', 'tools/build/host-tests.sh', 'no-such-group'], cwd=ROOT, env=env,
                          capture_output=True, text=True)
    assert none.returncode == 2 and 'no host test group matches' in none.stderr, none.stdout + none.stderr
# Host tests keep temp files under the group's TMPDIR: no fixed /tmp paths.
needles = ('"' + '/tmp/', 'mktemp ' + '/tmp')   # split so this file does not match itself
fixed_tmp = sorted(str(f.relative_to(ROOT)) for f in (ROOT / 'tests').rglob('*')
                   if f.suffix in ('.c', '.cc', '.h', '.sh', '.py') and f.is_file()
                   and any(n in f.read_text(errors='replace') for n in needles))
assert fixed_tmp == ['tests/gpu_thread_sim.py'], fixed_tmp   # a manual tool, not a host-tests group
container = (ROOT / 'tools/build/container.sh').read_text()
assert 'for var in BB_BUILD_JOBS BASELINE_BUILD_REFERENCE BASELINE_JOBS HOST_TEST_JOBS' in container
assert '"${passenv[@]/#/--env=}"' in container and '--setenv "$var" "${!var}"' in container
print('read-only empty-home host setup, ImageMagick compatibility and GNU fixture declarations PASS')

meta = {'build_root': '/work/artifacts/layout-reference/ref'}
reference.require_build_root(meta, Path(meta['build_root']))
try:
    reference.require_build_root(meta, Path('/foreign/reference'))
    raise AssertionError('unqualified assertion-string paths accepted')
except ValueError as error:
    assert 'pinned container' in str(error)
print('reference rebuild path is explicit; relocated prebuilt artifacts remain valid PASS')

# DEVKITARM selects both tools, even if its directory itself contains "nm".
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    sdk = td / 'nm-toolchain'
    (sdk / 'bin').mkdir(parents=True)
    for tool, output in [('nm', '00000000 T hot'), ('objdump', '')]:
        path = sdk / ('bin/arm-none-eabi-' + tool)
        path.write_text('#!/bin/sh\necho "' + output + '"\n')
        path.chmod(0o755)
    (td / 'audio.o').touch()
    command = ('import sys; sys.path.insert(0,sys.argv[1]); from pin_contract import frontend_names; '
               'assert frontend_names(sys.argv[2],"audio","")=={"hot"}')
    subprocess.run([sys.executable, '-c', command, str(ROOT / 'tools/layout'), str(td)],
                   env=dict(os.environ, DEVKITARM=str(sdk)), check=True)
print('frontend pin discovery honors the selected SDK for nm and objdump PASS')

# Equal-address nm rows are locale-sensitive; identity must canonicalize their
# order without dropping any symbol, byte, size or address. Use a minimal real
# ELF32 image so this regression remains independent of target build products.
import struct
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    sdk = td / 'sdk/bin'; sdk.mkdir(parents=True)
    nm = sdk / 'arm-none-eabi-nm'
    nm.write_text('#!/bin/sh\nif [ "$LC_ALL" = C ]; then\n'
                  'printf "00100000 T _a\\n00100000 T b\\n"\nelse\n'
                  'printf "00100000 T b\\n00100000 T _a\\n"\nfi\n')
    nm.chmod(0o755)
    names = b'\0.text\0.shstrtab\0'
    offset = 52 + 3 * 40
    header = struct.pack('<16sHHIIIIIHHHHHH', b'\x7fELF\x01\x01\x01'+b'\0'*9,
                         2, 40, 1, 0x100000, 0, 52, 0, 52, 0, 0, 40, 3, 2)
    text = struct.pack('<10I', 1, 1, 2, 0x100000, offset+len(names), 4, 0, 0, 4, 0)
    strings = struct.pack('<10I', 7, 3, 0, 0, offset, len(names), 0, 0, 1, 0)
    elf = td / 'fixture.elf'
    elf.write_bytes(header + bytes(40) + text + strings + names + b'code')
    command = ('import sys; from pathlib import Path; sys.path.insert(0,sys.argv[1]); '
               'import reference; print(reference.identity(Path(sys.argv[2])))')
    identities = []
    for locale in ('C', 'en_US.UTF-8'):
        identities.append(subprocess.check_output([sys.executable, '-c', command,
                          str(ROOT / 'tools/layout'), str(elf)], text=True,
                          env=dict(os.environ, DEVKITARM=str(td/'sdk'), LC_ALL=locale)))
    assert identities[0] == identities[1], identities
print('ELF identity retains all symbols with locale-independent nm ordering PASS')

# Source inventory makes newly introduced timestamp users fail loudly.
import importlib.util
spec = importlib.util.spec_from_file_location('source_epoch', ROOT / 'tools/build/source-epoch.py')
epoch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(epoch)
users = set()
for directory in ('core', 'gba-core', 'systems', 'app', 'platform', 'include'):
    for source in (ROOT / directory).rglob('*'):
        if source.suffix in ('.c', '.cpp', '.cc', '.cxx', '.h', '.hpp', '.hxx', '.inc', '.inl', '.S', '.s') and source.is_file():
            if re.search(r'\b__(?:DATE|TIME)__\b', source.read_text(errors='replace')):
                users.add(str(source.relative_to(ROOT)))
assert users == set(epoch.TIMESTAMP_OBJECTS), (users, epoch.TIMESTAMP_OBJECTS)
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    (td / 'Makefile').write_text('clean:\n\trm -rf build core\n')
    objects = [td / path for path in epoch.TIMESTAMP_OBJECTS.values()]
    preserved = td / 'build/3ds/build-breadbox/unrelated.o'
    for obj in [*objects, preserved]:
        obj.parent.mkdir(parents=True, exist_ok=True); obj.write_bytes(b'old timestamp')
    epoch.prepare(td, '1')
    assert all(not obj.exists() for obj in objects) and preserved.exists()
    for obj in objects: obj.write_bytes(b'current timestamp')
    epoch.prepare(td, '1')
    assert all(obj.exists() for obj in objects) and preserved.exists()
    epoch.prepare(td, '2')
    assert all(not obj.exists() for obj in objects) and preserved.exists()
    assert (td / 'build/systems/.source-date-epoch').read_text() == '2\n'
for flag in 'nqt': assert epoch.dry_run(flag + ' -j3 --jobserver-auth=3,4')
assert not epoch.dry_run(' -j3 --jobserver-auth=3,4')
for flags in (' -Oline', ' -Otarget', ' -Onone', ' -Oline -j3', ' -j3 -Otarget'): assert not epoch.dry_run(flags), flags
assert epoch.dry_run('n -Oline -j3')
assert 'core: source-epoch-check' in (ROOT / 'Makefile').read_text()
print('timestamp source inventory, targeted epoch invalidation and unchanged/dry-run preservation PASS')
