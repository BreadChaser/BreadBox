#!/usr/bin/env python3
"""Remove the always-true APP_BREADBOX guards (BreadBox is the only app).

Keeps the APP_BREADBOX side of every conditional, drops the #else/#ifndef
(PSX3D-only) side, and rewrites mixed expressions such as
`defined(APP_BREADBOX) && defined(__3DS__)` to `defined(__3DS__)`.
Directives without APP_BREADBOX are copied unchanged.

Run from the repository root:  python3 tools/cleanup/unifdef_app.py [--check] [files...]
Without file arguments it processes every tracked C source/header outside the
vendored cores and frozen test fixtures. Re-running it is a no-op.
"""
import os, re, subprocess, sys

SYM = 'APP_BREADBOX'
EXCLUDE = ('core/', 'gba-core/', 'systems/nes/core/', 'psx3d/vendor/',
           'tests/settings/fixtures/')          # frozen golden copy of v1-r2 code
EXTS = ('.c', '.h', '.inc')

DIRECTIVE = re.compile(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$')

# --- tiny expression simplifier ----------------------------------------------------------
TOK = re.compile(r'\s*(defined\s*\(\s*\w+\s*\)|defined\s+\w+|&&|\|\||!(?!=)|\(|\)|[^\s()&|!]+(?:\s*(?:==|!=|>=|<=|>|<)\s*[^\s()&|]+)?)')

class Unknown:
    def __init__(self, text): self.text = text

def tokenize(s):
    out, i = [], 0
    s = s.strip()
    while i < len(s):
        m = TOK.match(s, i)
        if not m or m.end() == i:
            raise ValueError('cannot tokenize: ' + s)
        out.append(m.group(1).strip()); i = m.end()
    return out

def parse(tokens):
    pos = [0]
    def peek(): return tokens[pos[0]] if pos[0] < len(tokens) else None
    def take(): t = tokens[pos[0]]; pos[0] += 1; return t
    def atom():
        t = take()
        if t == '(':
            v = orx()
            if take() != ')': raise ValueError('paren')
            return v if not isinstance(v, Unknown) else Unknown('(' + v.text + ')')
        if t == '!':
            v = atom()
            if isinstance(v, bool): return not v
            return Unknown('!' + v.text)
        m = re.fullmatch(r'defined\s*\(?\s*(\w+)\s*\)?', t)
        if m and m.group(1) == SYM: return True
        if t == SYM: return True
        return Unknown(t)
    def andx():
        v = atom()
        while peek() == '&&':
            take(); w = atom()
            if v is False or w is False: v = False
            elif v is True: v = w
            elif w is True: pass
            else: v = Unknown(v.text + ' && ' + w.text)
        return v
    def orx():
        v = andx()
        while peek() == '||':
            take(); w = andx()
            if v is True or w is True: v = True
            elif v is False: v = w
            elif w is False: pass
            else: v = Unknown(v.text + ' || ' + w.text)
        return v
    v = orx()
    if pos[0] != len(tokens): raise ValueError('trailing tokens: %r' % tokens)
    return v

def strip_comment(expr):
    expr = re.sub(r'/\*.*?\*/', ' ', expr)
    return expr.split('//', 1)[0].strip()

def evaluate(kind, rest):
    """True/False when decided, a string for a rewritten #if expression, None if untouched."""
    if not re.search(r'\b%s\b' % SYM, rest): return None
    expr = strip_comment(rest)
    if kind == 'ifdef':  return True if expr == SYM else None
    if kind == 'ifndef': return False if expr == SYM else None
    v = parse(tokenize(expr))
    return v if isinstance(v, bool) else v.text

# --- conditional walker --------------------------------------------------------------------
def process(text, path):
    lines = text.split('\n')
    # join backslash-continued directive lines
    logical = []
    i = 0
    while i < len(lines):
        l = lines[i]; group = [l]
        if DIRECTIVE.match(l):
            while group[-1].endswith('\\') and i + 1 < len(lines):
                i += 1; group.append(lines[i])
        logical.append(group); i += 1
    out, stack, changed = [], [], False
    in_comment = False
    def skipping(): return any(f['mode'] == 'skip' for f in stack)
    for group in logical:
        line = group[0]
        joined = ' '.join(g.rstrip('\\') for g in group)
        m = None if in_comment else DIRECTIVE.match(line)
        if not m:
            if not skipping(): out.extend(group)
            else: changed = True
            # track block comments so '#else' inside a comment is not a directive
            for g in group:
                s = re.sub(r'"(\\.|[^"\\])*"', '""', g)
                while True:
                    if in_comment:
                        j = s.find('*/')
                        if j < 0: break
                        in_comment = False; s = s[j+2:]
                    else:
                        s2 = s.split('//', 1)[0]
                        j = s2.find('/*')
                        if j < 0: break
                        in_comment = True; s = s2[j+2:]
            continue
        kind, rest = m.group(1), DIRECTIVE.match(joined).group(2)
        if kind in ('if', 'ifdef', 'ifndef'):
            if skipping():
                stack.append({'mode': 'skip', 'opened': False, 'taken': True, 'dead': True}); continue
            v = evaluate(kind, rest)
            if v is None:
                stack.append({'mode': 'keep', 'opened': True, 'taken': False, 'plain': True}); out.extend(group)
            elif v is True:
                stack.append({'mode': 'keep', 'opened': False, 'taken': True}); changed = True
            elif v is False:
                stack.append({'mode': 'skip', 'opened': False, 'taken': False}); changed = True
            else:
                stack.append({'mode': 'keep', 'opened': True, 'taken': False})
                out.append(re.match(r'^(\s*#\s*)', line).group(1) + 'if ' + v); changed = True
            continue
        if not stack: raise SystemExit('%s: unbalanced #%s' % (path, kind))
        f = stack[-1]
        if f.get('dead'):
            if kind == 'endif': stack.pop()
            continue
        if kind == 'elif':
            if f.get('plain') and not re.search(r'\b%s\b' % SYM, rest):
                out.extend(group); continue
            f.pop('plain', None)
            if f['taken']: f['mode'] = 'skip'; changed = True; continue
            v = evaluate('if', rest)
            if v is None: v = strip_comment(rest)
            prefix = re.match(r'^(\s*#\s*)', line).group(1)
            if v is True:
                if f['opened']: out.append(prefix + 'else')
                f['mode'] = 'keep'; f['taken'] = True
            elif v is False:
                f['mode'] = 'skip'
            else:
                out.append(prefix + ('elif ' if f['opened'] else 'if ') + v)
                f['opened'] = True; f['mode'] = 'keep'
            changed = True; continue
        if kind == 'else':
            if f.get('plain'): out.extend(group); continue
            if f['taken']: f['mode'] = 'skip'; changed = True; continue
            if f['opened']: out.extend(group)
            else: changed = True
            f['mode'] = 'keep'; f['taken'] = True; continue
        if kind == 'endif':
            stack.pop()
            if f['opened']: out.extend(group)
            else: changed = True
            continue
    if stack: raise SystemExit('%s: unterminated conditional' % path)
    return '\n'.join(out), changed

ASSERT = re.compile(r'(?<![\w])assert\s*\(')

def keep_assert_lines(old, new):
    """assert() prints __LINE__: lines above a runtime assert keep their numbers.
    Removed lines in front of the file's last assert become empty lines; later ones go."""
    import difflib
    code = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m[0]), old, flags=re.S)
    lines_code = code.split('\n')
    hits = [i for i, l in enumerate(lines_code) if ASSERT.search(l.split('//', 1)[0])]
    if not hits: return new
    last = hits[-1]
    a, b = old.split('\n'), new.split('\n')
    out = []
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
        if tag == 'equal': out.extend(b[j1:j2]); continue
        out.extend(b[j1:j2])
        lost = (i2 - i1) - (j2 - j1)          # lines this edit removed
        if lost > 0 and i1 < last:
            out.extend([''] * min(lost, last - i1))
    return '\n'.join(out)

def tracked_files():
    names = subprocess.check_output(['git', 'ls-files', '-z'], text=True).split('\0')
    return [n for n in names if n.endswith(EXTS) and not n.startswith(EXCLUDE)]

# Test and tool build lines: -DAPP_BREADBOX and "#define APP_BREADBOX" no longer select
# anything. The settings suites keep theirs: they compile the frozen v1-r2 fixture.
FLAG_KEEP = ('tests/settings/', 'tests/settings_b/', 'tests/gba_library/scope.c')   # these build the frozen v1-r2 fixture's BreadBox side
FLAG_RULES = [
    (re.compile(r"\['-DAPP_BREADBOX'\]"), '[]'),                 # a python list holding only the flag
    (re.compile(r",\s*'-DAPP_BREADBOX'(?=\s*[\])])"), ''),        # last item of a python list
    (re.compile(r"'-DAPP_BREADBOX',\s*"), ''),                  # python argument lists
    (re.compile(r"(?<=[\s'])-DAPP_BREADBOX(?=[\s'\\])\s?"), ''),   # shell lines
    (re.compile(r'^[ \t]*#define APP_BREADBOX(?: 1)?[ \t]*\n', re.M), ''),   # C, also C inside python strings
    (re.compile(r"'#define APP_BREADBOX\\n'\+"), ''),           # a python string prefix
]

def clean_flags(check):
    names = subprocess.check_output(['git', 'ls-files', '-z', 'tests', 'tools'], text=True).split('\0')
    touched = []
    for p in names:
        if not p or p.startswith(FLAG_KEEP + EXCLUDE) or p.endswith('.md') or not os.path.isfile(p): continue
        if p.startswith('tools/cleanup/'): continue   # the cleanup's own scripts and patches
        try: src = open(p, encoding='utf-8').read()
        except UnicodeDecodeError: continue
        if 'APP_BREADBOX' not in src: continue
        new = src
        for rx, rep in FLAG_RULES: new = rx.sub(rep, new)
        if new != src:
            touched.append(p)
            if not check: open(p, 'w', encoding='utf-8', newline='').write(new)
    return touched

# PSX3D fallbacks that sat after an "#ifdef APP_BREADBOX ... return; #endif" block: still
# compiled, never reached. (file, function, last statement BreadBox runs): everything after
# that statement up to the function's closing brace goes; a final bare "return;" goes too.
DEAD_TAILS = [
    ('app/ui/uisound.c', 'music_reload', 'music_reload_later(true);'),
    ('systems/ps1/library.inc', 'library_count', 'return visible_count;'),
]
# Kept on purpose (same kind of unreachable PSX3D tail after the BreadBox "return;"), because
# removing them changes the binary although behavior is the same: playlog.c adopt_key() (GCC
# splits the function differently) and share.c share_about_run() (the file's switch tables are
# numbered differently, CSWTCH.N, which moves every layout-gate fingerprint that names them).
# They go in a change that is allowed to move code.

def blank_code(s):
    rx = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|//[^\n]*|/\*[\s\S]*?\*/')
    return rx.sub(lambda m: re.sub(r'[^\n]', ' ', m[0]), s)

def drop_dead_tails(check):
    touched = []
    for path, func, last in DEAD_TAILS:
        if not os.path.exists(path): print('dead tail: %s is gone' % path); continue
        src = open(path).read(); code = blank_code(src)
        m = re.search(r'^[^\n;]*\b%s\s*\([^;{]*\)\s*\{' % re.escape(func), code, re.M)
        if not m: print('dead tail: no %s() in %s' % (func, path)); continue
        depth, i = 0, m.end() - 1
        for i in range(m.end() - 1, len(code)):
            depth += (code[i] == '{') - (code[i] == '}')
            if depth == 0: break
        body_end = src.rfind('\n', 0, i) + 1          # start of the closing-brace line
        hit = re.compile(r'^[ \t]*%s[ \t]*\n' % re.escape(last), re.M).search(src, m.end(), i)
        if not hit: print('dead tail: %r is not a statement line of %s()' % (last, func)); continue
        cut = hit.end()
        tail = src[cut:body_end]
        if re.fullmatch(r'\s*', tail): continue        # already clean
        new = src[:cut] + src[body_end:]
        touched.append(path)
        if not check: open(path, 'w').write(new)
    return touched

# Tests that cut source text at the old guards, or built the PSX3D variant too
# (file, exact old text, new text). Missing text is reported, never guessed.
TEST_FIXUPS = [
    ('tests/main_lifecycle/check.py', "'\\n#ifdef APP_BREADBOX\\n    if(bb_update_committed())'",
     "'\\n    if(bb_update_committed())'"),
    ('tests/share_room/check.py', "loop=fn('share_about_run').split('#ifdef APP_BREADBOX',1)[1].split('#endif',1)[0]",
     "loop=fn('share_about_run');loop=loop[loop.index('{')+1:loop.index('\\n\\treturn;\\n')+len('\\n\\treturn;\\n')]"),
    ('tests/share_room/check.py', "s.index('#endif\\n// Runs until B')", "s.index('// Runs until B')"),
    ('tests/parity/backends.py', '.replace("#ifdef APP_BREADBOX", "").replace("#endif", "")', ''),
    ('tests/themes/run.py', " # Both application builds: BreadBox adds its neutral Light and Dark themes.\n for app in ([],[]):",
     " # The BreadBox themes: the rooms, then Light and Dark.\n for app in ([],):"),
    ('tests/themes/values.py', "actual = {'psx3d': dump([]), 'breadbox': dump([])}", "actual = {'breadbox': dump([])}"),
    ('tests/themes/values.py', '(its #ifdef APP_BREADBOX members)', '(its members)'),
    ('tests/bb_disc/run.sh', "# Standalone PSX3D must neither implement nor reference the CHD probe/API.\n"
     "cc -std=c99 -Wall -Wextra -Werror -c \"$root/app/ui/bb_disc.c\" -o \"$tmp/standalone.o\"\n"
     "if nm \"$tmp/standalone.o\" | rg 'bb_disc|chd_'; then exit 1; fi\n", ''),
    ('tests/bb_disc/README.md', ' It also checks compilation without APP_BREADBOX.', ''),
    # the one PSX3D-only exit check (its folder-browser loop) is gone
    ('tests/main_lifecycle/check.py', "assert s.count('if (MAIN_STOP()) break;') == 14", "assert s.count('if (MAIN_STOP()) break;') == 13"),
    ('tests/room/libui-publication.py', "s.index('#endif',s.index('int libui_run('))]",
     "s.index('\\t\\tif (dev_tick(false)) {',s.index('int libui_run('))]"),
    ('tests/system_iface/continue.py', "assert '#else\\n\\tcont_probe();\\n#endif' in entry # standalone PSX3D only\n", ''),
    ('tests/system_iface/continue.py', "assert home.count('cont_probe();')==2 # entry and Settings, both non-BreadBox",
     "assert home.count('cont_probe();')==0 # BreadBox never probes on the menu thread"),
    ('tests/system_iface/continue.py', "assert '#else\\n            cont_probe();\\n#endif' in home\n", ''),
    # the index test builds the PS1 scanner without the CHD reader and links a stub instead
    ('systems/ps1/sys_ps1_scan.c', '#ifndef BB_INDEX_TEST\n#include "bb_disc.h"\n#endif\n', '#include "bb_disc.h"\n'),
    # fix/save-ownership's Continue-controller test (absent elsewhere: optional)
    ('tests/app/save/run.sh', "end=source.index('\\n#endif',start)", "end=source.index('\\n\\n#define NO_SLOT',start)", 'optional'),
    ('docs/ui-components.md', ' Standalone\nPSX3D retains its original layouts through `APP_BREADBOX` conditional paths.', ''),
]

def test_fixups(check):
    touched = []
    for path, old, new, *flags in TEST_FIXUPS:
        quiet = 'optional' in flags   # a test only some branches have
        if not os.path.exists(path):
            if not quiet: print('fixup: %s is gone' % path)
            continue
        t = open(path).read()
        if old not in t:
            if new and new not in t and not quiet: print('fixup: text not found in %s: %s' % (path, old[:60]))
            continue
        touched.append(path)
        if not check: open(path, 'w').write(t.replace(old, new, 1))
    # the pinned theme values of the PSX3D build go with it
    vj = 'tests/themes/values.json'
    if os.path.exists(vj):
        import json
        d = json.load(open(vj))
        if 'psx3d' in d:
            touched.append(vj)
            if not check:
                d.pop('psx3d'); open(vj, 'w').write(json.dumps(d, indent=1) + '\n')
    return touched

def main(argv):
    check = '--check' in argv
    files = [a for a in argv if not a.startswith('--')] or tracked_files()
    touched = []
    for p in files:
        try: src = open(p, encoding='utf-8').read()
        except UnicodeDecodeError: src = open(p, encoding='latin-1').read()
        if not re.search(r'\b%s\b' % SYM, src): continue
        new, changed = process(src, p)
        if new != src and not p.startswith('tests/'): new = keep_assert_lines(src, new)
        if new != src:
            touched.append(p)
            if not check:
                with open(p, 'w', encoding='utf-8', newline='') as fh: fh.write(new)
    left = [p for p in files if re.search(r'^\s*#\s*(if|ifdef|ifndef|elif).*\b%s\b' % SYM,
                                          open(p, encoding='utf-8', errors='replace').read(), re.M)]
    if not [a for a in argv if not a.startswith('--')]:
        touched += drop_dead_tails(check)
        touched += clean_flags(check)
        touched += test_fixups(check)
    if not check and not [a for a in argv if not a.startswith('--')]:
        # guard baselines follow the change, shrink-only (tools/cleanup/ratchets.py)
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import ratchets
        changed_now = subprocess.check_output(['git', 'diff', '--name-only', 'HEAD'], text=True).split()
        ratchets.shrink_ownership(); ratchets.lower_shell_baseline(); ratchets.rehash_surfaces(changed_now)
    print('%s %d files' % ('would change' if check else 'changed', len(touched)))
    for p in touched: print('  ' + p)
    if left and not check:
        print('guards still present (unexpected):'); [print('  ' + p) for p in left]; return 1
    return 1 if (check and touched) else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
