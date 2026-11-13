#!/usr/bin/env python3
"""Rename value-named UI_METRIC_<n> style tokens to names that say what they are.

Driven by tools/cleanup/ui-metric-names.tsv (tab separated):
    old            new                     scope                  meaning
    UI_METRIC_400  UI_TOP_SCREEN_WIDTH     *                      top screen width in pixels
    UI_METRIC_240  UI_PROGRESS_BAR_WIDTH   app/ui/ui.c:ui_progress_draw   ...
scope is `*` (every use), a file (`app/ui/components/home.inc`) or a file and the
function the use is in (`file:function`). The most specific matching row wins.

Values never change: a new name gets `#define NEW <old's exact value>` in
include/breadbox/ui_style.h next to the old token, unless it already exists there
with exactly that value (the script refuses a name defined with another value).
An old token with no uses left is removed from ui_style.h.

Run from the repository root:  python3 tools/cleanup/rename_ui_metrics.py [--check]
Re-running is a no-op. Rows whose token or scope no longer exists are reported.
"""
import os, re, subprocess, sys

STYLE = 'include/breadbox/ui_style.h'
MAP = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ui-metric-names.tsv')
SKIP = ('core/', 'gba-core/', 'systems/nes/core/', 'psx3d/vendor/', 'research/')   # the frozen v1-r2 fixture follows too

lex = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/')
def blank(s):  # comments and strings to spaces, same length
    return lex.sub(lambda m: re.sub(r'[^\n]', ' ', m[0]), s)

def functions(code):
    """[(start, end, name)] of top-level brace blocks that are function bodies."""
    out, depth, start, name = [], 0, None, None
    for i, c in enumerate(code):
        if c == '{':
            if depth == 0:
                head = code[max(0, i - 400):i]
                m = re.search(r'(\w+)\s*\([^;{}]*\)\s*(?:__attribute__\s*\(\([^)]*\)\)\s*)?$', head)
                start, name = i, (m.group(1) if m else None)
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0 and start is not None:
                if name: out.append((start, i, name))
                start = None
    return out

def read_map():
    rows = []
    for n, line in enumerate(open(MAP), 1):
        if not line.strip() or line.startswith('#'): continue
        f = line.rstrip('\n').split('\t')
        if len(f) < 4: sys.exit('%s:%d: need old, new, scope, meaning (tab separated)' % (MAP, n))
        old, new, scope = f[0].strip(), f[1].strip(), f[2].strip()
        if not re.fullmatch(r'UI_[A-Z0-9_]+', new): sys.exit('%s:%d: bad name %s' % (MAP, n, new))
        rows.append((old, new, scope, f[3].strip()))
    return rows

def pick(rows, old, path, func):
    best, rank = None, -1
    for r in rows:
        if r[0] != old: continue
        s = r[2]
        k = 2 if s == '%s:%s' % (path, func) else 1 if s == path else 0 if s == '*' else -1
        if k > rank: best, rank = r, k
    return best

def main(argv):
    check = '--check' in argv
    rows = read_map()
    style = open(STYLE).read()
    values = dict(re.findall(r'^#define\s+(UI_\w+)\s+(.*?)\s*$', style, re.M))
    for old, new, scope, _ in rows:
        if old not in values: print('note: %s is no longer in ui_style.h (row %s -> %s)' % (old, old, new))
    files = [f for f in subprocess.check_output(['git', 'ls-files', '-z'], text=True).split('\0')
             if f.endswith(('.c', '.h', '.inc')) and not f.startswith(SKIP) and f != STYLE and os.path.exists(f)]
    olds = {r[0] for r in rows}
    tok = re.compile(r'\b(%s)\b' % '|'.join(sorted(map(re.escape, olds), key=len, reverse=True))) if olds else None
    changed, used_new, left = {}, {}, set()
    seen_scope = set()
    for f in files:
        src = open(f, errors='replace').read()
        if not tok or not tok.search(src): continue
        code = blank(src); funcs = functions(code)
        out, last = [], 0
        for m in tok.finditer(code):
            func = next((n for a, b, n in funcs if a < m.start() < b), None)
            r = pick(rows, m.group(1), f, func)
            if not r: left.add(m.group(1)); continue
            seen_scope.add((r[0], r[2]))
            out.append(src[last:m.start()]); out.append(r[1]); last = m.end()
            used_new[r[1]] = values.get(r[0])
        if out:
            out.append(src[last:]); changed[f] = ''.join(out)
    for old, new, scope, _ in rows:
        if (old, scope) not in seen_scope and old in values:
            print('note: no use of %s matches scope %s' % (old, scope))
    # every other use of a mapped token keeps it alive
    for f in files:
        if f in changed: continue
        src = blank(open(f, errors='replace').read())
        left |= set(re.findall(r'\bUI_METRIC_\w+\b', src))
    for f, t in changed.items():
        left |= set(re.findall(r'\bUI_METRIC_\w+\b', blank(t)))
    # ui_style.h: add new names next to their old token, drop old tokens with no use left
    lines = style.split('\n'); out = []
    have = dict(values)
    adds = {}
    for new, val in used_new.items():
        if val is None: continue
        if new in have:
            if have[new] != val: sys.exit('%s exists with value %s, the map needs %s' % (new, have[new], val))
            continue
        old = next(r[0] for r in rows if r[1] == new)
        adds.setdefault(old, []).append('#define %s %s' % (new, val)); have[new] = val
    for l in lines:
        m = re.match(r'#define\s+(UI_METRIC_\w+)\s', l)
        if m and m.group(1) in adds: out.extend(sorted(set(adds[m.group(1)])))
        if m and m.group(1) in olds and m.group(1) not in left:
            continue
        out.append(l)
    new_style = '\n'.join(out)
    # a new name used by the other new defines' values is fine; values are literal tokens
    n = sum(1 for f in changed) + (new_style != style)
    if check:
        print('would change %d files' % n); return 1 if n else 0
    for f, t in changed.items(): open(f, 'w').write(t)
    if new_style != style: open(STYLE, 'w').write(new_style)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import ratchets   # reviewed-surface hashes follow the renamed files only
    ratchets.rehash_surfaces(list(changed) + [STYLE])
    print('renamed in %d files; ui_style.h %s' % (len(changed), 'updated' if new_style != style else 'unchanged'))
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
