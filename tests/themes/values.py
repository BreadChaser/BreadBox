#!/usr/bin/env python3
"""Pin every resolved theme colour of both app builds (tests/themes/values.json).

  values.py            compare theme.c with the pinned values
  values.py --record   rewrite values.json after an intended palette change

The fields come from the theme_t declaration in theme.h, so a moved or renamed
role shows up as a key change and a changed colour as a value change: a
structural cleanup must leave every value where it was."""
import json, pathlib, re, subprocess, sys, tempfile
root = pathlib.Path(__file__).resolve().parents[2]
src = root / 'app/ui'


def structs(*headers):
    """Every typedef struct of the headers: name -> [(type, member, array length)]."""
    out = {}
    for header in headers:
        text = re.sub(r'//.*|/\*[\s\S]*?\*/', '', header)
        text = re.sub(r'#[^\n]*', '', text)
        for body, name in re.findall(r'typedef struct\s*\{([^{}]*)\}\s*(\w+)\s*;', text):
            out[name] = []
            for kind, names in re.findall(r'\b(\w+)\s+([^;]+);', body):
                for member in names.split(','):
                    m = re.match(r'\s*\**\s*(\w+)\s*(?:\[(\d+)\])?', member)
                    out[name].append((kind, m[1], int(m[2] or 0)))
    return out


def keys(types, name, prefix=''):
    for kind, member, count in types[name]:
        path = prefix + member
        if kind in types:
            yield from keys(types, kind, path + '.')
        elif kind in ('u32', 'int', 'uint32_t'):
            yield from ([path] if not count else ['%s[%d]' % (path, k) for k in range(count)])


def preprocessed(name, app_defs):
    """The header as the app build sees it (its members)."""
    text = (src / name).read_text().replace('#include <3ds.h>', '').replace('#include "game_palette.h"', '')
    return subprocess.run(['cc', '-E', '-P', *app_defs, '-x', 'c', '-'], input=text, capture_output=True,
                          text=True, check=True).stdout


def dump(app_defs):
    types = structs(preprocessed('game_palette.h', app_defs), preprocessed('theme.h', app_defs))
    lines = ['#include <stdio.h>', '#include "app_config.h"', '#include "theme.c"', 'psx3d_config g_cfg;',
             'static int first;',
             'static void c(const char *k, unsigned v) { printf("%s\\"%s\\":%u", first ? "" : ",", k, v); first = 0; }',
             'int main(void) { printf("{"); for (int i = 0; i < theme_count(); i++) {',
             ' const theme_t *t = theme_get(i); printf("%s\\"%s\\":{", i ? "," : "", t->id); first = 1;']
    lines += [' c("%s", (unsigned)t->%s);' % (key, key) for key in keys(types, 'theme_t')]
    lines.append(' printf("}"); } printf("}\\n"); return 0; }')
    with tempfile.TemporaryDirectory(prefix='psx3d-theme-values-') as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / 'dump.c').write_text('\n'.join(lines))
        inc = ['-I', str(root / 'tests/app/host'), '-I', str(src), '-I', str(root / 'include/breadbox'), '-I', str(root / 'platform/3ds'), '-I', str(root / 'core/deps/libretro-common/include')]
        subprocess.run(['cc', '-std=gnu11', '-w', *app_defs, *inc, '-o', str(tmp / 'dump'), str(tmp / 'dump.c')], check=True)
        return json.loads(subprocess.run([str(tmp / 'dump')], capture_output=True, text=True, check=True).stdout)


actual = {'breadbox': dump([])}
pinned = pathlib.Path(__file__).with_name('values.json')
if '--record' in sys.argv:
    ordered = {app: {tid: dict(sorted(v.items())) for tid, v in themes.items()} for app, themes in actual.items()}
    pinned.write_text(json.dumps(ordered, indent=1) + '\n')
    sys.exit(0)
expect = json.loads(pinned.read_text())
problems = []
for app in sorted(set(expect) | set(actual)):
    a, e = actual.get(app, {}), expect.get(app, {})
    if list(a) != list(e):
        problems.append('%s: themes %s, pinned %s' % (app, list(a), list(e)))
    for tid in a:
        for key in sorted(set(a[tid]) | set(e.get(tid, {}))):
            if a[tid].get(key) != e.get(tid, {}).get(key):
                problems.append('%s %s %s: 0x%08x, pinned 0x%08x' % (app, tid, key, a[tid].get(key, 0), e.get(tid, {}).get(key, 0)))
for p in problems[:40]:
    print('FAIL', p)
if problems:
    sys.exit('%d theme values differ from tests/themes/values.json (--record after an intended change)' % len(problems))
print('theme values PASS: %s' % ', '.join('%s %d themes x %d values' % (app, len(v), len(next(iter(v.values())))) for app, v in actual.items()))
