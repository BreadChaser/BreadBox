#!/usr/bin/env python3
"""Per-system conformance (docs/dev/new-system.md), for every systems/<id>/.

  check.py [ID...]

Structural checks plus compiled schema and real-core host/save behavioral tests.
PS1 is explicitly partial: no portable native PS1 adapter is available; see README.md.
New systems fail closed until a runtime driver is registered.
"""
import pathlib, re, shutil, subprocess, sys, tempfile

root = pathlib.Path(__file__).resolve().parents[2]
abi = re.search(r'#define BB_SYSTEM_ABI (\d+)', (root / 'include/breadbox/bb_system.h').read_text())[1]
CORE_KEYS = ('DIR', 'MAKE', 'ARCHIVE', 'PREFIX')
JIT_KEYS = ('JIT_HEADER', 'JIT_EXPR')


def strip(text):
    return re.sub(r'//[^\n]*|/\*[\s\S]*?\*/', '', text)


def check(tree, sid, scaffold=False):
    problems, folder = [], tree / 'systems' / sid
    adapter = folder / ('sys_%s.c' % sid)
    if not adapter.exists():
        return ['%s: no sys_%s.c' % (sid, sid)]
    src = strip(adapter.read_text())
    m = re.search(r'const\s+bb_system_t\s+bb_system_%s\s*=\s*\{(.*?)\};' % sid, src, re.S)
    if not m:
        problems.append('%s: sys_%s.c does not define const bb_system_t bb_system_%s' % (sid, sid, sid))
    else:
        body = m[1]
        if not re.search(r'\.id\s*=\s*"%s"' % sid, body):
            problems.append('%s: descriptor .id is not "%s"' % (sid, sid))
        for member in ('abi', 'name', 'short_name', 'extensions', 'run', 'probe', 'identify'):
            if not re.search(r'\.%s\s*=\s*(?!NULL\b|0\b)[^,}\s]+' % member, body):
                problems.append('%s: descriptor has no .%s' % (sid, member))
        if not scaffold:   # the scaffold leaves the date for its author (new-system.py TODO)
            for member in ('release_year', 'release_month'):
                if not re.search(r'\.%s\s*=\s*[1-9]' % member, body):
                    problems.append('%s: descriptor has no .%s (first release worldwide)' % (sid, member))
        if not re.search(r'\.abi\s*=\s*BB_SYSTEM_ABI\b', body):
            problems.append('%s: .abi must be BB_SYSTEM_ABI' % sid)
    for f in sorted(folder.glob('*.[ch]')):
        for n, line in enumerate(strip(f.read_text()).splitlines(), 1):
            if re.search(r'\(\s*[\w\s*]+\(\s*\*\s*\)\s*\([^)]*\)\s*\)\s*&?\w', line):
                problems.append('%s/%s:%d: function-pointer cast' % (sid, f.name, n))
    core = folder / 'core.mk'
    if core.exists():
        keys = dict(re.findall(r'^\s*%s_CORE_(\w+)\s*:?=\s*(.*?)\s*$' % sid, core.read_text(), re.M))
        problems += ['%s: core.mk has no %s_CORE_%s' % (sid, sid, k) for k in CORE_KEYS if k not in keys]
        if any(k in keys for k in JIT_KEYS) and not all(k in keys for k in JIT_KEYS):
            problems.append('%s: core.mk JIT keys need both %s' % (sid, ' and '.join(JIT_KEYS)))
        if 'DIR' in keys and not (tree / keys['DIR']).is_dir():
            problems.append('%s: core folder %s does not exist' % (sid, keys['DIR']))
    if scaffold:
        r = subprocess.run(['cc', '-std=gnu11', '-fsyntax-only', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(tree / 'app/ui'),'-I' + str(tree / 'include/breadbox'),'-I' + str(tree / 'platform/3ds'), '-I' + str(root / 'systems/common'),
                            '-I' + str(root / 'core/deps/libretro-common/include'), str(adapter)], capture_output=True, text=True)
        if r.returncode:
            problems.append('%s: sys_%s.c does not compile against bb_system.h:\n%s' % (sid, sid, r.stderr[-1500:]))
    else:
        systems = ' '.join(re.findall(r'^SYSTEMS\s*(?::|\+)?=\s*(.*)$', '\n'.join(p.read_text() for p in (tree / 'apps').glob('*.mk')), re.M))
        if sid not in systems.split():
            problems.append('%s: not in SYSTEMS of any apps/*.mk' % sid)
        extra = folder / 'tests' / 'conformance.py'
        if extra.exists() and subprocess.run([sys.executable, str(extra)]).returncode:
            problems.append('%s: systems/%s/tests/conformance.py failed' % (sid, sid))
    return problems


def shell_includes(tree):
    owned = {h.name: h.parent.name for h in (tree / 'systems').glob('*/*.h')}
    dirs = [tree / d for d in ('app', 'platform/3ds', 'include/breadbox')]
    shell = {h.name for d in dirs for h in d.rglob('*.h')}
    out = []
    for f in sorted(p for d in dirs for p in d.rglob('*')):
        if f.suffix in ('.c', '.h', '.inc'):
            for name in re.findall(r'#\s*include\s*"([^"]+)"', f.read_text()):
                base = pathlib.PurePath(name).name
                if base in owned and base not in shell:
                    out.append('shell %s includes %s from systems/%s' % (f.relative_to(tree), name, owned[base]))
    return out


ids = sys.argv[1:] or sorted(p.name for p in (root / 'systems').iterdir() if p.is_dir() and p.name != 'common')
problems = shell_includes(root)
for sid in ids:
    problems += check(root, sid)
    if sid == 'gba':
        for suite in ('gba_cart', 'gba_identity'):
            if subprocess.run(['bash',str(root / 'tests' / suite / 'run.sh')]).returncode:
                problems.append(sid + ': ' + suite + ' failed')
    if sid in ('gba', 'nes'):
        for driver in ('descriptor.py', 'runtime.py'):
            if subprocess.run([sys.executable, str(root / 'tests/system_conformance' / driver), sid]).returncode:
                problems.append(sid + ': ' + driver + ' failed')
    elif sid == 'ps1':
        print('ps1 PARTIAL: synthetic ARM HLE state replay; legacy shell/AV/card/Continue driver missing (README.md)', flush=True)
        if subprocess.run(['bash', str(root / 'tests/core/run.sh'), 'determinism']).returncode:
            problems.append('ps1: synthetic ARM HLE state replay failed (requires ARM Linux GCC and qemu-arm)')
    else:
        problems.append(sid + ': no behavioral runtime driver registered')
# The scaffold must keep matching the interface: generate one and check it.
with tempfile.TemporaryDirectory(prefix='bb-conformance-') as tmp:
    tmp = pathlib.Path(tmp)
    (tmp / 'psx3d').mkdir(parents=True)   # the scaffold's --core folder
    (tmp / 'include/breadbox').mkdir(parents=True)
    shutil.copy(root / 'include/breadbox/bb_system.h', tmp / 'include/breadbox/bb_system.h')
    # Compile the scaffold against the real host contract; only SDK key constants
    # and the app root are faked for this native syntax check.
    (tmp / 'include/breadbox/app_policy.h').write_text('#include "bb_system.h"\n'
        '#define APP_SD_ROOT "sdmc:/3ds/Test"\n'
        'enum { KEY_A=1,KEY_B=2,KEY_START=4,KEY_SELECT=8,KEY_UP=16,KEY_DOWN=32,KEY_LEFT=64,KEY_RIGHT=128 };\n')
    subprocess.run([sys.executable, str(root / 'tools/new-system.py'), 'xmpl', 'Example', 'EX', '--ext', 'ex,zip',
                    '--core', 'psx3d', '--root', str(tmp)], check=True, capture_output=True)
    problems += check(tmp, 'xmpl', scaffold=True)
for p in problems:
    print('FAIL', p)
if problems:
    sys.exit('%d conformance problems' % len(problems))
print('system conformance implemented gates PASS: %s (ABI %s); behavioral: %s; partial: %s' %
      (', '.join(ids), abi, ', '.join(s for s in ids if s in ('gba','nes')) or 'none',
       'ps1 (synthetic HLE core only)' if 'ps1' in ids else 'none'))
