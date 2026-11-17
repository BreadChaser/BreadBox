#!/usr/bin/env python3
"""Size gate: first-party C files stay at or under 1,500 lines and functions at or under 150.

An .inc file counts into the file that includes it (so renaming code to .inc does not dodge the
rule). Today's offenders sit in size-exceptions.json with their current sizes. The gate fails when
an exception grows, when a new offender appears, and when a listed exception no longer offends, so
the list can only shrink. Vendored trees are not scanned.

  python3 tools/architecture/size-gate.py            check
  python3 tools/architecture/size-gate.py --list     print every offender and its size
  python3 tools/architecture/size-gate.py --update   write the list; refuses to add or raise an entry
"""
import bisect
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gatelib

# One edit here when the top-level folders move (src/, third_party/).
ROOTS = ('app', 'systems', 'platform', 'include', 'core/plugins/gpu_pica')   # first-party code
VENDORED = ('systems/nes/core', 'gba-core', 'psx3d/vendor', 'core')           # never scanned (the rest of core/ is vendored)
EXTENSIONS = ('.c', '.cc', '.cpp', '.h', '.hpp', '.inc')
EXCEPTIONS = 'size-exceptions.json'
MAX_FILE, MAX_FUNC = 1500, 150

INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]+"([^"]+\.inc)"[^\n]*$', re.M)
BLANK = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|^[ \t]*#(?:[^\n]|\\\n)*', re.S | re.M)
ATTRS = re.compile(r'(?:__attribute__\s*\(\(.*?\)\)|\bPSX3D_\w+|\bBB_\w+)\s*$', re.S)


def scanned(rel):
    """First-party? The longest matching entry of ROOTS (yes) and VENDORED (no) decides."""
    best = max(((len(p), ours) for ours, group in ((True, ROOTS), (False, VENDORED)) for p in group
                if rel.startswith(p + '/')), default=(0, False))
    return best[1]


def expand(path, root, seen=(), included=None):
    """The file's text with every quoted .inc include replaced by that file's (expanded) text."""
    text = path.read_text(errors='replace')

    def sub(m):
        inc = (path.parent / m.group(1)).resolve()
        if not inc.is_file() or inc in seen or len(seen) > 8:
            return m.group(0)
        if included is not None:
            included.add(inc)
        return expand(inc, root, seen + (path.resolve(),), included).rstrip('\n')
    return INCLUDE.sub(sub, text)


def functions(text):
    """(name, line count) of each top-level function body; comments, strings and #lines are ignored."""
    code = BLANK.sub(lambda m: re.sub(r'[^\n]', ' ', m.group()), text)
    starts = [0] + [m.end() for m in re.finditer('\n', code)]
    line = lambda i: bisect.bisect_right(starts, i)
    stack, stmt, out = [], 0, []
    for m in re.finditer(r'[{};]', code):
        c, i = m.group(), m.start()
        top = all(k[0] == 'extern' for k in stack)
        if c == ';':
            if top:
                stmt = i + 1
        elif c == '{':
            head = code[stmt:i].strip()
            if not top:
                stack.append(('other',))
            elif head.startswith('extern') and '(' not in head:
                stack.append(('extern',))
                stmt = i + 1
            elif ATTRS.sub('', head).rstrip().endswith(')'):
                first = line(stmt + len(code[stmt:i]) - len(code[stmt:i].lstrip()))
                stack.append(('func', first, head))
            else:
                stack.append(('other',))
        elif stack:
            kind = stack.pop()
            if kind[0] == 'func':
                out.append((name_of(kind[2]), line(i) - kind[1] + 1))
            if kind[0] != 'other' and all(k[0] == 'extern' for k in stack):
                stmt = i + 1
    return out


def name_of(head):
    head = head.strip()
    while True:   # drop trailing attribute/section macros, then take the name before the parameter list
        cut = ATTRS.sub('', head).rstrip()
        if cut == head:
            break
        head = cut
    depth = 0
    for i in range(len(head) - 1, -1, -1):
        depth += {')': 1, '(': -1}.get(head[i], 0)
        if depth == 0 and head[i] == '(':
            m = re.search(r'(\w+)\s*$', head[:i])
            return m.group(1) if m else '?'
    return '?'


def measure(root):
    """({file: lines}, {file::function: lines}) for every unit that is over a limit or not."""
    root = Path(root)
    files = [p for r in ROOTS if (root / r).is_dir() for p in sorted((root / r).rglob('*'))
             if p.is_file() and p.suffix in EXTENSIONS and scanned(p.relative_to(root).as_posix())]
    included, texts = set(), {}
    for p in files:
        if p.suffix != '.inc':
            texts[p] = expand(p, root, (), included)
    for p in files:   # an .inc nobody includes is its own unit
        if p.suffix == '.inc' and p.resolve() not in included:
            texts[p] = expand(p, root, (), included)
    sizes, funcs = {}, {}
    for p, text in texts.items():
        rel = p.relative_to(root).as_posix()
        sizes[rel] = text.count('\n') + (0 if text.endswith('\n') or not text else 1)
        seen = {}
        for name, n in functions(text):
            seen[name] = seen.get(name, 0) + 1
            funcs[f'{rel}::{name}' + (f'#{seen[name]}' if seen[name] > 1 else '')] = n
    return sizes, funcs


def offenders(root):
    sizes, funcs = measure(root)
    return ({k: v for k, v in sizes.items() if v > MAX_FILE}, {k: v for k, v in funcs.items() if v > MAX_FUNC})


def compare(current, frozen, what, limit):
    problems = []
    for k, v in sorted(current.items()):
        if k not in frozen:
            problems.append(f'NEW {what} over {limit}: {k} is {v}')
        elif v > frozen[k]:
            problems.append(f'GREW {what}: {k} is {v}, frozen at {frozen[k]}')
    for k, v in sorted(frozen.items()):
        if k not in current:
            problems.append(f'STALE exception: {k} no longer offends; delete it from {EXCEPTIONS} (--update)')
    return problems


def main():
    ap = gatelib.args(__doc__.split('\n')[0])
    a = ap.parse_args()
    files, funcs = offenders(a.root)
    if a.list:
        for k, v in sorted({**files, **funcs}.items(), key=lambda kv: -kv[1]):
            print(f'{v:6d}  {k}')
    path = gatelib.data_path(a.root, EXCEPTIONS)
    frozen = json.loads(path.read_text()) if path.exists() else None
    if a.update:
        if frozen is not None:
            bad = compare(files, frozen['files'], 'file', MAX_FILE) + compare(funcs, frozen['functions'], 'function', MAX_FUNC)
            bad = [b for b in bad if not b.startswith('STALE')]
            if bad:
                print('\n'.join(bad) + '\nsize-gate: --update only removes or lowers entries', file=sys.stderr)
                return 1
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps({'files': files, 'functions': funcs}, indent=1, sort_keys=True) + '\n')
        print(f'size-gate: stored {len(files)} files and {len(funcs)} functions in {gatelib.DATA}/{EXCEPTIONS}')
        return 0
    if frozen is None:
        print(f'size-gate: {EXCEPTIONS} is missing; run with --update to create it', file=sys.stderr)
        return 1
    problems = compare(files, frozen['files'], 'file', MAX_FILE) + compare(funcs, frozen['functions'], 'function', MAX_FUNC)
    shrunk = [k for k, v in {**files, **funcs}.items() if v < {**frozen['files'], **frozen['functions']}.get(k, v)]
    print('\n'.join(problems))
    if shrunk:
        print(f'note: {len(shrunk)} exception(s) got smaller; --update locks that in')
    print(f'size-gate: {len(files)} files over {MAX_FILE} lines, {len(funcs)} functions over {MAX_FUNC}: '
          + ('FAIL' if problems else 'ok'))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
