#!/usr/bin/env python3
"""Split include/breadbox/psx3d.h into one header per module and remove it.

The map tools/cleanup/psx3d-header-map.txt assigns every declaration to a header.
Declarations keep their text, comments and order; each new header includes what
its own declarations use. Every file that included psx3d.h (or got it through
another header) then includes the headers whose names it uses.

Run from the repository root:  python3 tools/cleanup/split_psx3d_h.py [--dry-run]
Stops without writing anything when a declaration has no rule in the map.
Re-running after the split is a no-op (psx3d.h is gone).
"""
import os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import psx3dh_chunks as P

HEADER = 'include/breadbox/psx3d.h'
OUT_DIR = 'include/breadbox'
MAP = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'psx3d-header-map.txt')
SKIP = ('core/', 'gba-core/', 'systems/nes/core/', 'psx3d/vendor/')
INCLUDE_DIRS = ['include/breadbox', 'app/ui', 'app/settings', 'app/services', 'platform/3ds',
                'systems/common', 'systems/ps1', 'systems/gba', 'tests/app/host']
# Headers psx3d.h included: a user of psx3d.h may rely on them (and on what they include).
BASE = ['"app_policy.h"', '"libretro.h"', '"safefile.h"', '"history.h"', '"theme.h"', '"icons.h"',
        '"ui_format.h"', '"bb_settings.h"', '"ui_style.h"']
# libc headers in psx3d.h's tree are listed too, so snprintf means <stdio.h>, not <citro2d.h>
SYSTEM_ORDER = ['<3ds.h>', '<citro2d.h>', '<limits.h>', '<math.h>', '<stdarg.h>', '<stdbool.h>', '<stddef.h>',
                '<stdint.h>', '<stdio.h>', '<string.h>', '<strings.h>', '<time.h>', '<sys/types.h>']
CC = os.environ.get('CC_3DS', '/opt/devkitpro/devkitARM/bin/arm-none-eabi-gcc')
CFLAGS = ['-std=gnu11', '-D__3DS__', '-march=armv6k', '-mfloat-abi=hard', '-mtp=soft',
          '-Iinclude/breadbox', '-Iapp/ui', '-Iplatform/3ds', '-Isystems/common', '-Icore/deps/libretro-common/include',
          '-I/opt/devkitpro/libctru/include', '-I/opt/devkitpro/portlibs/3ds/include']

def tree_names(inc):
    """Every name a translation unit sees after #include inc (the preprocessed text)."""
    src = subprocess.run([CC, *CFLAGS, '-E', '-P', '-dD', '-x', 'c', '-'], input='#include %s\n' % inc,
                         capture_output=True, text=True)
    if src.returncode: raise SystemExit('cannot preprocess %s: %s' % (inc, src.stderr[:300]))
    return rough_names_text(src.stdout, keep_words=inc.startswith('<'))

def idents(text):
    return set(re.findall(r'\b[A-Za-z_]\w*\b', P.strip(text)))

def rough_names_text(text, keep_words=False):
    """Names a header text declares (macros, types, enum constants, functions, variables)."""
    t = P.strip(text)
    names = set(re.findall(r'^\s*#\s*define\s+(\w+)', t, re.M))
    flat = re.sub(r'^\s*#.*$', '', t, flags=re.M)
    depth, start, stmts = 0, 0, []
    for m in re.finditer(r'[{};]', flat):
        ch = m.group(0)
        if ch == '{': depth += 1
        elif ch == '}': depth -= 1
        elif depth == 0: stmts.append(flat[start:m.end()]); start = m.end()
        if ch == '}' and depth == 0 and re.search(r'\)\s*\{', flat[start:m.end()]):
            stmts.append(flat[start:m.end()]); start = m.end()   # a function body (static inline)
    for s in stmts:
        s = re.sub(r'__attribute__\s*\(\((?:[^()]|\([^()]*\))*\)\)', ' ', s)
        for body in re.findall(r'enum\s*\w*\s*\{([^}]*)\}', s):
            names |= {re.match(r'\s*(\w+)', e).group(1) for e in body.split(',') if e.strip()}
        names |= set(re.findall(r'(?:struct|union|enum)\s+(\w+)', s))
        top = s   # drop brace bodies from the inside out, leaving {} at the top level
        while True:
            nxt = re.sub(r'\{[^{}]*\}', '\x00', top)
            if nxt == top: break
            top = nxt
        top = top.replace('\x00', '{}')
        m = re.search(r'(\w+)\s*(\[[^\]]*\])*\s*;\s*$', top)
        if m and not re.search(r'\)\s*;\s*$', top): names.add(m.group(1))
        m = re.search(r'\(\s*\*\s*(\w+)\s*\)\s*\(', top)
        if m: names.add(m.group(1))
        m = re.match(r'[^()]*?\b(\w+)\s*\(', top)
        if m: names.add(m.group(1))
    # plain lowercase words (top, true) are field and parameter names far more often,
    # unless the header declares them as a function (theme())
    funcs = set(re.findall(r'\b([a-z]+)\s*\([^;{}]*\)\s*;', flat))
    if not keep_words:
        names = {n for n in names if not re.fullmatch(r'[a-z]+', n) or n in funcs}
        names -= {'true', 'false'}
    names -= {'if', 'while', 'for', 'return', 'sizeof', 'struct', 'enum', 'union', 'typedef', 'extern', 'static',
              'inline', 'const', 'void', 'int', 'unsigned', 'char', 'float', 'double', '__attribute__'}
    return names if keep_words else names - {'bool'}

def read_map():
    headers, cur = [], None
    for line in open(MAP):
        line = line.split('#', 1)[0].rstrip() if not line.startswith('[') else line.rstrip()
        if not line.strip(): continue
        m = re.match(r'\[(\S+)\]\s*(.*)', line)
        if m:
            cur = {'name': m.group(1), 'about': m.group(2), 'exact': [], 'patterns': []}
            headers.append(cur); continue
        for w in line.split():
            (cur['exact'] if w.startswith('=') else cur['patterns']).append(w.lstrip('='))
    return headers

def assign(headers, key):
    for h in headers:
        if key in h['exact']: return h
    for h in headers:
        if any(re.fullmatch(p, key) for p in h['patterns']): return h
    return None

def tracked():
    out = subprocess.check_output(['git', 'ls-files', '-z'], text=True).split('\0')
    return [f for f in out if f and f.endswith(('.c', '.h', '.inc')) and not f.startswith(SKIP) and os.path.exists(f)]

INC = re.compile(r'^(\s*#\s*include\s*)"([^"]+)"', re.M)

def resolve(name, frm):
    cand = [os.path.normpath(os.path.join(os.path.dirname(frm), name))] + [os.path.join(d, name) for d in INCLUDE_DIRS]
    for c in cand:
        if os.path.exists(c): return c
    return None

def main(argv):
    dry = '--dry-run' in argv
    if not os.path.exists(HEADER):
        print('psx3d.h is already split'); return 0
    headers = read_map()
    text = open(HEADER).read()
    before, inner, after = P.split_guard(text)
    chunks = [c for c in P.chunks(inner)]
    missing, order = [], []
    for h in headers: h['chunks'] = []; h['names'] = set()
    prev_h, blank = None, False
    for i, (comment, body) in enumerate(chunks):
        if not body or not ''.join(body).strip():
            blank = True; continue
        ns = P.names(body)
        if ns[0].startswith('#include'):
            blank = False; continue
        h = assign(headers, ns[0])
        if h is None:
            missing.append(ns[0]); continue
        sep = bool(h['chunks']) and (blank or h is not prev_h)
        h['chunks'].append((sep, comment + body)); h['names'] |= set(ns)
        prev_h, blank = h, False
    if missing:
        print('no rule in %s for: %s' % (MAP, ' '.join(missing))); return 2
    headers = [h for h in headers if h['chunks']]
    by_inc = {'"%s"' % h['name']: h for h in headers}
    # what each header psx3d.h included provides: its own part of its include tree (a name
    # goes to the innermost of these headers that brings it, so u32 means <3ds.h>, not theme.h)
    trees = {inc: tree_names(inc) for inc in SYSTEM_ORDER + BASE}
    system_all = set().union(*(trees[i] for i in SYSTEM_ORDER))
    base_names = {}
    for inc in SYSTEM_ORDER:
        others = set().union(*(trees[j] for j in SYSTEM_ORDER if j != inc and trees[j] <= trees[inc] and j != inc))
        base_names[inc] = trees[inc] - others
    for inc in BASE:
        inner = set().union(*(trees[j] for j in BASE if j != inc and trees[j] < trees[inc]))
        base_names[inc] = trees[inc] - system_all - inner
    base_names = {k: {n for n in v if not re.fullmatch(r'_+\w*', n)} for k, v in base_names.items()}

    def provides(inc):
        if inc in by_inc: return by_inc[inc]['names']
        return base_names.get(inc, set())

    def uses(text_ids, skip=()):
        return [inc for inc in list(by_inc) + BASE + SYSTEM_ORDER if inc not in skip and text_ids & provides(inc)]

    # each new header's own includes
    for h in headers:
        body = '\n'.join('\n'.join(c) for _, c in h['chunks'])
        # a struct member's name is not a use of anything (int theme; in psx3d_config)
        body = re.sub(r'\{[^{}]*\}', lambda m: re.sub(r'\b\w+(?=\s*(?:\[[^\]]*\]\s*)*[;,])', ' ', m[0]), P.strip(body))
        h['deps'] = uses(idents(body) - h['names'], skip=('"%s"' % h['name'],))
    deps = {inc: set(by_inc[inc]['deps']) for inc in by_inc}
    def closure(incs):   # over the new headers only
        seen, todo = set(), list(incs)
        while todo:
            x = todo.pop()
            if x in seen: continue
            seen.add(x); todo.extend(deps.get(x, ()))
        return seen
    for inc in by_inc:
        if inc in closure(deps[inc]): print('include cycle through', inc); return 2

    def ordered(incs):
        new = [i for i in by_inc if i in incs]
        base = [i for i in BASE if i in incs]
        syst = [i for i in SYSTEM_ORDER if i in incs]
        return syst + base + new

    files = tracked()
    PSX3D_INC = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"((?:[^"\n]*/)?)psx3d\.h"[^\n]*\n', re.M)
    texts = {f: open(f, errors='replace').read() for f in files}
    writes = {}
    def text(f): return writes.get(f, texts.get(f) or open(f, errors='replace').read())

    def includes(f, upto=None):
        """[(include spelling, resolved path or None)] of f, optionally only before offset upto"""
        t = text(f)[:upto] if upto is not None else text(f)
        out = []
        for m in re.finditer(r'^\s*#\s*include\s*("[^"]+"|<[^>]+>)', t, re.M):
            sp = m.group(1)
            out.append((sp, resolve(sp.strip('"'), f) if sp.startswith('"') else None))
        return out

    def reach(f, upto=None, seen=None):
        """every include spelling reachable from f's includes (before upto), by basename"""
        seen = set() if seen is None else seen
        got = set()
        for sp, path in includes(f, upto):
            key = '"%s"' % os.path.basename(sp.strip('"')) if sp.startswith('"') else sp
            got.add(key)
            if key in by_inc: got |= closure([key])
            if path and path.endswith('.h') and path not in seen:
                seen.add(path); got |= reach(path, None, seen)
        return got

    # headers that bring psx3d.h with them, directly or through another header
    def brings(path, seen=None):
        seen = set() if seen is None else seen
        if path in seen: return False
        seen.add(path)
        for sp, p2 in includes(path):
            if os.path.basename(sp.strip('"')) == 'psx3d.h': return True
            if p2 and p2.endswith('.h') and brings(p2, seen): return True
        return False
    includers = [f for f in files if f != HEADER and PSX3D_INC.search(texts[f])]
    carriers = {f for f in files if f.endswith('.h') and f != HEADER and brings(f)}

    def local_text(f, seen=None):
        """f and the .inc files it pulls in."""
        seen = seen or set()
        if f in seen: return ''
        seen.add(f)
        t = text(f)
        for m in INC.finditer(t):
            if m.group(2).endswith('.inc'):
                r = resolve(m.group(2), f)
                if r: t += '\n' + local_text(r, seen)
        return t

    def needed(f):
        return uses(idents(local_text(f)), skip=('"%s"' % os.path.basename(f),))

    # 1. every direct includer: psx3d.h becomes the headers it uses (new ones always written out;
    #    base/system ones only when nothing included above that line provides them already)
    for f in sorted(includers, key=lambda f: (not f.endswith('.h'), f)):
        t = text(f); m = PSX3D_INC.search(t); prefix = m.group(1)
        need = needed(f)
        new = [i for i in need if i in by_inc]
        have = reach(f, m.start()) | closure(new)   # only what is included above: a use may come before a later include
        keep = [i for i in need if i in by_inc or i not in have]
        lines = ''.join('#include %s\n' % (i if not (prefix and i in by_inc) else '"%s%s"' % (prefix, i.strip('"')))
                        for i in ordered(keep))
        writes[f] = t[:m.start()] + lines + t[m.end():]

    # 2. files that got psx3d.h through a header: add what they use and no longer see
    for f in files:
        if f in includers or f == HEADER: continue
        t = text(f)
        hits = [(sp, p) for sp, p in includes(f) if p in carriers]
        if not hits: continue
        first = next(mm for mm in re.finditer(r'^\s*#\s*include\s*"([^"]+)"[^\n]*\n', t, re.M) if resolve(mm.group(1), f) in carriers)
        have = reach(f, first.end())
        add = [i for i in needed(f) if i not in have]
        if add:
            writes[f] = t[:first.end()] + ''.join('#include %s\n' % i for i in ordered(add)) + t[first.end():]

    if dry:
        for h in headers:
            print('%-20s %3d decls  includes %s' % (h['name'], len(h['chunks']), ' '.join(ordered(h['deps']))))
        print('%d files would change' % len(writes)); return 0
    for h in headers:
        out = ['// ' + h['about'], '#pragma once']
        out += ['#include %s' % i for i in ordered(h['deps'])]
        out.append('')
        for sep, lines in h['chunks']:
            if sep: out.append('')
            out.extend(lines)
        path = os.path.join(OUT_DIR, h['name'])
        if os.path.exists(path): print('refusing to overwrite', path); return 2
        open(path, 'w').write('\n'.join(out) + '\n')
    for f, t in writes.items():
        open(f, 'w').write(t)
    os.remove(HEADER)
    retarget_tools(headers)
    import ratchets   # guard baselines follow, shrink-only
    touched = list(writes) + [os.path.join(OUT_DIR, h['name']) for h in headers] + [f for f, _, _ in FIXUPS]
    ratchets.shrink_ownership(); ratchets.lower_shell_baseline(); ratchets.rehash_surfaces(touched, gone={'psx3d.h'})
    print('wrote %d headers, updated %d files, removed %s' % (len(headers), len(writes), HEADER))
    return 0

# --- tools, tests and comments that name psx3d.h ------------------------------------------
# (file, old text, new text): exact replacements; a missing text is reported, never guessed.
FIXUPS = [
    ('build/3ds/core-first.ld', 'marked PSX3D_COLD (psx3d.h)', 'marked PSX3D_COLD (cold_code.h)'),
    ('tools/icons/icons.tsv', 'in psx3d.h UI_ICON_* order', 'in ui.h UI_ICON_* order'),
    ('app/ui/breadbox_picture.c', '(psx3d.h bb_last_picture_*)', '(last_picture.h bb_last_picture_*)'),
    ('systems/ps1/ps1_controls.h', 'Public compatibility entry points remain in psx3d.h.',
     'Public compatibility entry points remain in app_input.h.'),
    ('systems/ps1/savestates.c', '(psx3d.h, bb_slots.h)', '(app_policy.h, bb_slots.h)'),
    ('tests/room/transition.py', "header=(root/'include/breadbox/psx3d.h').read_text()",
     "header=(root/'include/breadbox/ui_pause.h').read_text()"),
    ('tests/bb_library/swap.py', "(root/'include/breadbox/psx3d.h')", "(root/'include/breadbox/library.h')"),
    ('tests/ui_consistency/input.py', "h=(root/'include/breadbox/psx3d.h').read_text()",
     "h=(root/'include/breadbox/ui_list.h').read_text()"),
    # theme.c and scene.c include the split headers now: drop them as psx3d.h was dropped
    ('tests/themes/run.py', "for header in ('psx3d.h','scene.h','devtools.h'):", "for header in ('scene.h','devtools.h',*SPLIT_HEADERS):"),
    ('tests/parity/backends.py', 'import subprocess,tempfile', 'import re,subprocess,tempfile'),
    ('tests/themes/values.py', "'#include \"psx3d.h\"', '#include \"theme.c\"'", "'#include \"app_config.h\"', '#include \"theme.c\"'"),
    ('tools/themes/check-mock.py', "'#include \"psx3d.h\"', '#include \"theme.c\"'", "'#include \"app_config.h\"', '#include \"theme.c\"'"),
    # the cheat-controller test swaps cheats.c's headers for a small stub
    ('tests/parity/backends.py', """s=s.replace('#include "psx3d.h"',r\'\'\'""",
     """s=re.sub(r'(#include "(?:%s)"\\n)+' % '|'.join(re.escape(h) for h in SPLIT_HEADERS), '@HOST_HEADERS@\\n', s, count=1)
s=re.sub(r'#include "(?:%s)"\\n' % '|'.join(re.escape(h) for h in SPLIT_HEADERS), '', s)
s=s.replace('@HOST_HEADERS@',r\'\'\'"""),
    # the scaffold's host headers (tests/system_conformance fakes the same name)
    ('tools/new-system.py', "'#include \\\"psx3d.h\\\"\\n#include", "'#include \\\"app_policy.h\\\"\\n#include"),
    ('tests/system_conformance/check.py', "(tmp / 'include/breadbox/psx3d.h').write_text(", "(tmp / 'include/breadbox/app_policy.h').write_text("),
]

SKIP_DIR = 'tests/app/host/skip'   # empty stand-ins: a host test that brings its own declarations puts it first

def retarget_tools(headers):
    names = [h['name'] for h in headers]
    os.makedirs(SKIP_DIR, exist_ok=True)
    for n in names + ['3ds.h', 'citro2d.h']:
        open(os.path.join(SKIP_DIR, n), 'w').write('// Empty stand-in for host tests that bring their own declarations (tests/app/host/skip).\n')
    for f, old, new in FIXUPS:
        if not os.path.exists(f): print('fixup: %s is gone' % f); continue
        t = open(f).read()
        if old not in t:
            if new not in t: print('fixup: text not found in %s: %s' % (f, old[:70]))
            continue
        t = t.replace(old, new, 1)
        if 'SPLIT_HEADERS' in t and 'SPLIT_HEADERS=' not in t:
            t = t.replace('\nimport ', '\nSPLIT_HEADERS=%r\nimport ' % names, 1) if '\nimport ' in t else 'SPLIT_HEADERS=%r\n' % names + t
        open(f, 'w').write(t)
    # tools/architecture: psx3d.h's frozen references follow their declarations
    owner = {}
    for h in headers:
        for _, lines in h['chunks']:
            for l in lines:
                code = re.sub(r'/\*.*?\*/', ' ', l)
                code = re.sub(r'//(?=(?:[^"]*"[^"]*")*[^"]*$).*', '', code)   # a // outside strings
                owner.setdefault(' '.join(code.split()), h['name'])
    ex_path = 'tools/architecture/ownership-exceptions.json'
    if os.path.exists(ex_path):
        import json, collections
        data = json.load(open(ex_path), object_pairs_hook=collections.OrderedDict)
        ex = data['exceptions']; old = ex.get(HEADER)
        if old:
            split = collections.OrderedDict()
            for cat, lines in old['approved'].items():
                for line in lines:
                    h = owner.get(' '.join(line.split()))
                    if not h: print('ownership: no new header holds %r' % line); continue
                    e = split.setdefault('%s/%s' % (OUT_DIR, h), collections.OrderedDict(
                        reason='split from psx3d.h: ' + old['reason'], approved=collections.OrderedDict()))
                    e['approved'].setdefault(cat, []).append(line)
            new = collections.OrderedDict()
            for k, v in ex.items():
                if k == HEADER: new.update(split)
                else: new[k] = v
            data['exceptions'] = new
            open(ex_path, 'w').write(json.dumps(data, indent=1) + '\n')   # ownership-guard.py's own format
    sb_path = 'tools/architecture/shell-baseline.json'
    if os.path.exists(sb_path):
        import json
        sb = json.load(open(sb_path)); out = {}
        name_owner = {n: h['name'] for h in headers for n in h['names']}
        for k, v in sb.items():
            f, tokn = k.split(':', 1)
            if f == 'psx3d.h':
                body = {h['name']: '\n'.join('\n'.join(c) for _, c in h['chunks']) for h in headers}
                hits = {hn: len(re.findall(r'\b%s\b' % re.escape(tokn), b)) for hn, b in body.items()}   # shell-guard counts comments too
                for hn, c in hits.items():
                    if c: out['%s:%s' % (hn, tokn)] = out.get('%s:%s' % (hn, tokn), 0) + c
            else:
                out[k] = out.get(k, 0) + v
        open(sb_path, 'w').write(json.dumps(out, indent=2, sort_keys=True) + '\n')

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
