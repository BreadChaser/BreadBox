#!/usr/bin/env python3
"""Host test of tools/architecture/include-graph.py on small fake trees, then the real one."""
import subprocess, sys, tempfile
from pathlib import Path
root = Path(__file__).resolve().parents[2]
tool = root / 'tools/architecture/include-graph.py'
tmp = tempfile.TemporaryDirectory(prefix='include-graph-')

def tree(files):
    d = Path(tempfile.mkdtemp(dir=tmp.name))
    base = {'include/breadbox/ok.h': '', 'app/ui/shell.h': '', 'platform/3ds/plat.h': '',
            'systems/common/common.h': '', 'app/ui/shell.c': '#include "shell.h"\n#include "plat.h"\n#include "ok.h"\n'}
    for rel, text in {**base, **files}.items():
        (d / rel).parent.mkdir(parents=True, exist_ok=True)
        (d / rel).write_text(text)
    return d

def run(d):
    r = subprocess.run([sys.executable, str(tool), str(d)], capture_output=True, text=True)
    return r.returncode, r.stdout

cases = [
    ('system uses shared headers', {'systems/x/a.c': '#include "ok.h"\n#include "common.h"\n#include "own.h"\n#include "gen.h"\n',
                                    'systems/x/own.h': ''}, 0),
    ('system includes a shell header', {'systems/x/a.c': '#include "shell.h"\n'}, 1),
    ('system includes a platform header', {'systems/x/a.c': '#include "plat.h"\n'}, 1),
    ('system reaches the shell by a relative path', {'systems/x/a.c': '#include "../../app/ui/shell.h"\n'}, 1),
    ('shared header includes a shell header', {'include/breadbox/bad.h': '#include "shell.h"\n'}, 1),
    ('system .inc includes a shell header', {'systems/x/components/a.inc': '#include "shell.h"\n'}, 1),
    ('vendored code is not checked', {'systems/nes/core/a.c': '#include "shell.h"\n'}, 0),
    ('angle brackets by a relative path', {'systems/x/a.c': '#include <../../app/ui/shell.h>\n'}, 1),
    ('no space after include', {'systems/x/a.c': '#include"../../app/ui/shell.h"\n'}, 1),
    ('angle brackets in a system .inc', {'systems/x/components/a.inc': '#include <../../app/ui/shell.h>\n'}, 1),
    ('angle brackets, shell-only name', {'systems/x/a.c': '#include <plat.h>\n'}, 1),
    ('SDK and shared headers in angle brackets', {'systems/x/a.c': '#include <3ds.h>\n#include <ok.h>\n#include <string.h>\n'}, 0),
]
empty = Path(tempfile.mkdtemp(dir=tmp.name))
(empty / 'src').mkdir()
if run(empty)[0] != 1:
    print('FAIL an empty tree must not pass'); sys.exit(1)
failed = 0
for name, files, want in cases:
    rc, out = run(tree(files))
    if rc != want:
        failed += 1; print('FAIL', name, rc, out)
rc, out = run(root)
if rc:
    failed += 1; print('FAIL real tree', out)
print(f'include graph: {len(cases) + 1} checks, {failed} failed')
sys.exit(1 if failed else 0)
