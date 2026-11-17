#!/usr/bin/env python3
"""Dependency-ownership check for SHARED code (app/, platform/3ds, include/breadbox, systems/common).

Shared code must not own system policy: no private system or core headers, no
PS1 library/content types, no PCSX setting keys, no PS1 disc/card/BIOS policy.
Those belong in systems/<id>/ or behind bb_system.h. (tools/architecture/
shell-guard.py counts system ids and file extensions; this check covers the
types, keys and includes that guard cannot see, including what leaks through
psx3d.h.)

Fail-closed rules:
  * every shared file is held to ZERO forbidden references unless it has an
    entry in ownership-exceptions.json, so new code is checked by default;
  * an entry FREEZES the approved legacy references: the normalized source
    line of each one, per category. A reference whose line is not approved is a
    violation; an approved line that no longer exists is an error too, so the
    entry must shrink in the same change and a cleanup cannot buy room for a
    new reference (--freeze rewrites the lines, never the reasons);
  * an entry needs a reason, known categories and an existing file;
  * words inside a user-facing prose string literal (four or more words, no
    `_`, `%` or backslash: credits, help text, attribution such as "BIOS") are
    not policy; a bare "bios" or a path/key literal still counts;
  * an empty scan (missing directories, no files) or a malformed exception list
    is a failure, never a pass.
Exit 0 only when every check passes. Without --strict violations are still
printed but the exit code stays 0 (local exploration).
Usage: ownership-guard.py [--strict] [--root DIR] [--exceptions FILE] [-v] [--freeze]
"""
import collections, json, re, sys
from pathlib import Path

CATEGORIES = {
    'private-include': 'include of a system-private or core-private header',
    'ps1-content-type': 'PS1 library/content type (game_t, disc list)',
    'pcsx-key': 'PCSX-ReARMed core setting key',
    'ps1-storage-policy': 'PS1 BIOS, memory card or disc policy',
    'ps1-state-policy': 'PS1 save-state folder or "last game state" naming (the save service owns slot paths)',
}
PATTERNS = {
    'ps1-content-type': re.compile(r'\bgame_t\b|\bndiscs\b|\bdiscs\s*\['),
    'pcsx-key': re.compile(r'\bpcsx_rearmed_\w+'),
    'ps1-state-policy': re.compile(r'\bPSX3D_STATE_DIR\b|\blast_game_state\b|\bP3TH\b'),
    'ps1-storage-policy': re.compile(r'\bPSX3D_BIOS_DIR\b|\bmemcard\w*|\bMcd\w*|\bCdrom\w*|\bbios\b', re.I),
}
INCLUDE = re.compile(r'^\s*#\s*include\s*["<]([^">]+)[">]', re.M)
# Directory segments that mark a core-private include path.
PRIVATE_SEGMENTS = {'systems', 'core', 'gba-core', 'libpcsxcore'}
SHARED_GLOBS = (('app', ('.c', '.h', '.inc')), ('platform/3ds', ('.c', '.h', '.inc')),
                ('include/breadbox', ('.c', '.h', '.inc')), ('systems/common', ('.c', '.h', '.inc')))


def strip_comments(text):
    """Blank out comments (keeping newlines and string literals)."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        two = text[i:i + 2]
        if two == '//':
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i)); i = j
        elif two == '/*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', text[i:j])); i = j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1]); i = j + 1
        else:
            out.append(c); i += 1
    return ''.join(out)


def private_headers(root):
    """Headers an adapter owns: systems/<id>/*.h (not its vendored core tree)."""
    names = set()
    systems = root / 'systems'
    if systems.is_dir():
        for d in systems.iterdir():
            if d.is_dir() and d.name != 'common':
                names.update(p.name for p in d.glob('*.h'))
    # A shared header of the same name wins: the include resolves to that one.
    return names - {p.name for p in shared_files(root) if p.suffix == '.h'}


def shared_files(root):
    files = []
    for rel, exts in SHARED_GLOBS:
        d = root / rel
        if d.is_dir():
            files += [p for p in sorted(d.rglob('*')) if p.is_file() and p.suffix in exts]
    return files


PROSE_CATEGORIES = {'ps1-storage-policy'}   # English words that read as policy only in code
STRING = re.compile(r'"(?:[^"\\\n]|\\.)*"')


def prose_spans(line):
    """Character ranges of the string literals on this line that are user-facing prose."""
    spans = []
    for m in STRING.finditer(line):
        body = m.group(0)[1:-1]
        if len(body.split()) >= 4 and not re.search(r'[_%\\]', body):
            spans.append((m.start(), m.end()))
    return spans


def scan(root):
    """{relpath: {category: [(line number, normalized line), ...]}} for every shared file."""
    private = private_headers(root)
    result = {}
    for path in shared_files(root):
        text = strip_comments(path.read_text(errors='replace'))
        hits = {}

        def add(cat, number, line):
            hits.setdefault(cat, []).append((number, ' '.join(line.split())))
        lines = text.splitlines()
        for m in INCLUDE.finditer(text):
            target = m.group(1)
            parts = set(Path(target).parts[:-1])
            if Path(target).name in private or parts & PRIVATE_SEGMENTS:
                number = text.count('\n', 0, m.start()) + 1
                add('private-include', number, lines[number - 1])
        for number, line in enumerate(lines, 1):
            for cat, pat in PATTERNS.items():
                spans = prose_spans(line) if cat in PROSE_CATEGORIES else []
                if any(not any(a <= m.start() < b for a, b in spans) for m in pat.finditer(line)):
                    add(cat, number, line)
        result[str(path.relative_to(root))] = hits
    return result


def load_exceptions(path, errors):
    try:
        data = json.loads(Path(path).read_text())
        entries = data['exceptions']
        assert isinstance(entries, dict)
    except Exception as e:  # unreadable or malformed list: fail closed
        errors.append(f'exception list {path} unusable: {e}')
        return {}
    for name, entry in entries.items():
        if not isinstance(entry, dict) or not str(entry.get('reason', '')).strip():
            errors.append(f'{name}: exception needs a non-empty "reason"')
        approved = entry.get('approved') if isinstance(entry, dict) else None
        if not isinstance(approved, dict) or not approved:
            errors.append(f'{name}: exception needs an "approved" map of category -> source lines')
            continue
        for cat, lines in approved.items():
            if cat not in CATEGORIES:
                errors.append(f'{name}: unknown category {cat!r}')
            elif not isinstance(lines, list) or not lines or not all(isinstance(l, str) and l.strip() for l in lines):
                errors.append(f'{name}: approved lines for {cat} must be a non-empty list of source lines')
    return entries


def approved_lines(entry, cat):
    approved = entry.get('approved', {}) if isinstance(entry, dict) and isinstance(entry.get('approved'), dict) else {}
    lines = approved.get(cat, [])
    return [' '.join(l.split()) for l in lines] if isinstance(lines, list) else []


def check(root, exceptions_file, verbose=False):
    errors, notes = [], []
    found = scan(root)
    if len(found) < 10:
        errors.append(f'only {len(found)} shared files found under {root}: refusing to pass an empty scan')
    entries = load_exceptions(exceptions_file, errors)
    for name in entries:
        if name not in found:
            errors.append(f'{name}: exception for a file that is not shared code (stale or misspelled)')
    for name, hits in sorted(found.items()):
        entry = entries.get(name)
        for cat in sorted(set(hits) | set(entry.get('approved', {}) if isinstance(entry, dict) and isinstance(entry.get('approved'), dict) else {})):
            remaining = collections.Counter(approved_lines(entry, cat))
            for number, text in hits.get(cat, []):
                if remaining[text] > 0:
                    remaining[text] -= 1
                else:
                    errors.append(f'{name}:{number}: {cat} reference not approved ({CATEGORIES.get(cat, cat)}): {text[:110]}')
            for text, count in sorted(remaining.items()):
                if count > 0:
                    errors.append(f'{name}: approved {cat} line is gone, delete it from the exception (the list only shrinks): {text[:110]}')
    if verbose:
        for name, hits in sorted(found.items()):
            for cat, lines in sorted(hits.items()):
                print(f'  {name}: {cat} x{len(lines)}')
    return errors, notes, found


def freeze(root, exceptions_file):
    """Rewrite the approved lines of the existing entries from the current tree (reasons stay)."""
    data = json.loads(Path(exceptions_file).read_text())
    found = scan(root)
    for name, entry in data['exceptions'].items():
        hits = found.get(name, {})
        entry.pop('allow', None)
        entry['approved'] = {cat: [t for _, t in lines] for cat, lines in sorted(hits.items())}
        if not entry['approved']:
            del data['exceptions'][name]
    Path(exceptions_file).write_text(json.dumps(data, indent=1) + '\n')


def main(argv):
    strict = '--strict' in argv
    root = Path(__file__).resolve().parents[2]
    exceptions = None
    if '--root' in argv:
        root = Path(argv[argv.index('--root') + 1]).resolve()
    if '--exceptions' in argv:
        exceptions = Path(argv[argv.index('--exceptions') + 1])
    if exceptions is None:
        exceptions = Path(__file__).with_name('ownership-exceptions.json')
    if '--freeze' in argv:
        freeze(root, exceptions)
        print('froze', exceptions)
    errors, notes, found = check(root, exceptions, '-v' in argv)
    for note in notes:
        print('NOTE', note)
    for error in errors:
        print('FAIL', error)
    total = sum(len(v) for hits in found.values() for v in hits.values())
    print(f'ownership guard: {len(found)} shared files, {total} references, {len(errors)} violations'
          f' ({"strict" if strict else "warn"} mode)')
    return 1 if strict and errors else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
