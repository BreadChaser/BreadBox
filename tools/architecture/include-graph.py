#!/usr/bin/env python3
"""include-graph.py [ROOT]: check that the include paths keep the layers apart.

build/3ds/Makefile puts include/breadbox and systems/common on every object's
path, and app/ui and platform/3ds only on the shell's objects (app/, platform/3ds).
This resolves every #include "..." and #include <...> in first-party code the way
the compiler does (a quoted name in the including file's folder first, then the
layer's paths; an angle-bracket name on the paths only) and fails when systems/
code or an include/breadbox header reaches a file in app/ or platform/, directly
or by a relative path. Includes that resolve nowhere (generated headers, the SDK)
are left alone. Finding no source to check is a failure: a moved tree must not
turn the check off."""
import os
import re
import sys
from pathlib import Path

INCLUDE = re.compile(r'^\s*#\s*include\s*(?:"([^"]+)"|<([^>]+)>)', re.M)
SOURCES = ('.c', '.h', '.inc', '.def')
SHARED = ('include/breadbox', 'systems/common')       # every object
SHELL = ('app/ui', 'platform/3ds')                     # shell objects only
VENDORED = ('systems/nes/core/',)


def paths_for(rel):
    """The search path after the file's own folder, as the Makefile sets it."""
    shell = rel.startswith(('app/', 'platform/'))
    return SHARED + (SHELL if shell else ())


def resolve(root, rel, name, quoted):
    for d in ([str(Path(rel).parent)] if quoted else []) + list(paths_for(rel)):
        p = root / d / name
        if p.is_file():
            return os.path.relpath(os.path.normpath(p), root)
    return None


def checked(rel):
    return rel.startswith(('systems/', 'include/breadbox/')) and not rel.startswith(VENDORED)


def violations(root):
    root = Path(root)
    out = []
    seen = 0
    for d in ('systems', 'include/breadbox'):
        for p in sorted((root / d).rglob('*')):
            rel = p.relative_to(root).as_posix()
            if p.suffix not in SOURCES or not p.is_file() or not checked(rel):
                continue
            seen += 1
            text = p.read_text(errors='replace')
            for m in INCLUDE.finditer(text):
                name = m.group(1) or m.group(2)
                hit = resolve(root, rel, name, m.group(1) is not None)
                line = text.count('\n', 0, m.start()) + 1
                if hit and hit.startswith(('app/', 'platform/')):
                    out.append(f'{rel}:{line}: includes {hit}')
                elif hit is None and any((root / s / name).is_file() for s in SHELL):
                    out.append(f'{rel}:{line}: "{name}" is only on the shell\'s path')
    if not seen:
        out.append('no system or shared source found to check')
    return out


def main(argv):
    root = Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parents[2]
    bad = violations(root)
    for v in bad:
        print('FAIL', v)
    print(f'include graph: {len(bad)} system/shared includes into the shell')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
