#!/usr/bin/env python3
"""Controls for the size gate and the two ratchets, run against small throwaway source trees.

Each gate must pass on a clean tree and refuse the exact thing it exists for; --update must lower
a stored value and refuse to raise one. Also runs the three gates on the real tree.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

REAL = Path(__file__).resolve().parents[2]
ARCH = REAL / 'tools/architecture'


def gate(script, root, *flags):
    r = subprocess.run([sys.executable, str(ARCH / script), '--root', str(root), *flags],
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def put(root, rel, text):
    p = Path(root) / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text)


def body(n):
    return 'int v;\n' * n


def func(name, n):
    return f'int {name}(int a)\n{{\n' + '\ta++;\n' * (n - 3) + '}\n'


def expect(cond, what, out=''):
    if not cond:
        print(f'FAIL {what}\n{out}')
        sys.exit(1)
    print(f'ok   {what}')


def size_gate(root):
    put(root, 'app/big.c', body(1501))
    put(root, 'app/ok.c', body(1350) + func('fits', 150))
    put(root, 'app/inc_user.c', '#include "inc/part.inc"\n' + body(100))
    put(root, 'app/inc/part.inc', body(1450))
    put(root, 'app/long.c', '/* } { */\nstatic const char *s = "}{";\n' + func('long_one', 151) + 'struct t { int x; } y = { 1 };\n')
    put(root, 'systems/nes/core/vendor.c', body(9000))
    put(root, 'core/other/vendor.c', body(9000))
    put(root, 'gba-core/vendor.c', body(9000))
    put(root, 'core/plugins/gpu_pica/gpu.c', body(1600))
    put(root, 'include/breadbox/a.h', 'extern "C" {\n' + func('hdr', 160) + '}\n')
    rc, out = gate('size-gate.py', root, '--list')
    expect('app/big.c' in out and 'app/inc_user.c' in out, 'files over 1,500 are found, .inc counted into its includer', out)
    expect('app/ok.c' not in out and 'fits' not in out, 'exactly 1,500 lines and 150 lines pass', out)
    expect('app/long.c::long_one' in out and '151' in out and 'long.c' not in out.replace('long.c::long_one', ''), 'one long function, braces in strings and comments ignored', out)
    expect('include/breadbox/a.h::hdr' in out, 'a function inside extern "C" is found', out)
    expect('vendor.c' not in out and 'gpu_pica/gpu.c' in out, 'vendored trees are skipped, gpu_pica is scanned', out)
    expect('app/inc/part.inc' not in out, 'an included .inc is not a unit of its own', out)
    expect(gate('size-gate.py', root)[0] == 1, 'no exceptions file: fails')
    expect(gate('size-gate.py', root, '--update')[0] == 0, '--update creates the list')
    expect(gate('size-gate.py', root)[0] == 0, 'frozen offenders pass')
    put(root, 'app/big.c', body(1502))
    rc, out = gate('size-gate.py', root)
    expect(rc == 1 and 'GREW' in out, 'a grown exception fails', out)
    expect(gate('size-gate.py', root, '--update')[0] == 1, '--update refuses to raise an entry')
    put(root, 'app/big.c', body(1501))
    put(root, 'app/new.c', body(1700))
    rc, out = gate('size-gate.py', root)
    expect(rc == 1 and 'NEW' in out and 'app/new.c' in out, 'a new offender fails', out)
    expect(gate('size-gate.py', root, '--update')[0] == 1, '--update refuses to add an offender')
    (Path(root) / 'app/new.c').unlink()
    put(root, 'app/long.c', func('long_one', 160))
    rc, out = gate('size-gate.py', root)
    expect(rc == 1 and 'GREW' in out and 'long_one' in out, 'a function that grew fails', out)
    put(root, 'app/inc/part.inc', body(1420))
    put(root, 'app/long.c', func('long_one', 151))
    rc, out = gate('size-gate.py', root)
    expect(rc == 0 and 'smaller' in out, 'an exception that shrank but still offends passes with a note', out)
    put(root, 'app/long.c', func('long_one', 120))
    rc, out = gate('size-gate.py', root)
    expect(rc == 1 and 'STALE' in out, 'an exception that no longer offends fails (the list only shrinks)', out)
    expect(gate('size-gate.py', root, '--update')[0] == 0, '--update removes it')
    expect(gate('size-gate.py', root)[0] == 0, 'clean after --update')


def count_gates(root):
    m = 'UI_METRIC_'   # built here so no source line of the repo carries a numbered name
    put(root, 'app/a.c', f'int a = {m}12 + {m}3;\nint b = {m}NAME;\n')
    put(root, 'include/breadbox/ui_style.h', f'#define {m}12 12\n#define {m}3 3\n')
    expect(gate('ui-metric-ratchet.py', root)[0] == 1, 'no count file: fails')
    expect(gate('ui-metric-ratchet.py', root, '--update')[0] == 0, 'ui-metric --update creates the count')
    expect((Path(root) / 'tools/architecture/ui-metric.count').read_text() == '2\n', 'the style header is not counted')
    expect(gate('ui-metric-ratchet.py', root)[0] == 0, 'same count passes')
    put(root, 'app/b.c', f'int c = {m}7;\n')
    rc, out = gate('ui-metric-ratchet.py', root)
    expect(rc == 1 and 'app/a.c' in out, 'a new use fails', out)
    expect(gate('ui-metric-ratchet.py', root, '--update')[0] == 1, '--update refuses to raise the count')
    (Path(root) / 'app/b.c').unlink()
    put(root, 'app/a.c', 'int a;\n')
    expect(gate('ui-metric-ratchet.py', root)[0] == 0, 'a lower count passes')
    expect(gate('ui-metric-ratchet.py', root, '--update')[0] == 0, '--update lowers it')
    expect((Path(root) / 'tools/architecture/ui-metric.count').read_text() == '0\n', 'the lowered count is stored')

    reader = 'from pathlib import Path\ns = Path("app/x.c").read_text()\nbody = s[s.index("{"):s.index("}")]\n'
    put(root, 'tests/t1/check.py', reader)
    put(root, 'tests/t2/check.py', 'import subprocess\nsubprocess.run(["make"])\n')
    put(root, 'tests/t3/check.py', 'from pathlib import Path\nx = Path("tests/data.txt").read_text().count("a")\n')
    put(root, 'tests/arch_gates/check.py', reader)
    rc, out = gate('test-text-ratchet.py', root, '--list')
    expect(out.splitlines()[0] == 'tests/t1/check.py' and 'tests/t2' not in out and 'tests/t3' not in out
           and 'arch_gates' not in out, 'only tests that read production source and slice it are counted', out)
    expect(gate('test-text-ratchet.py', root, '--update')[0] == 0, 'text ratchet --update creates the count')
    expect(gate('test-text-ratchet.py', root)[0] == 0, 'same count passes')
    put(root, 'tests/t4/check.py', reader)
    expect(gate('test-text-ratchet.py', root)[0] == 1, 'another source-reading test fails')
    expect(gate('test-text-ratchet.py', root, '--update')[0] == 1, '--update refuses to raise the count')


with tempfile.TemporaryDirectory() as tmp:
    size_gate(Path(tmp) / 'size')
    count_gates(Path(tmp) / 'counts')
for s in ('size-gate.py', 'test-text-ratchet.py', 'ui-metric-ratchet.py'):
    rc, out = gate(s, REAL)
    expect(rc == 0, f'{s} passes on the real tree', out)
print('architecture gate controls PASS')
