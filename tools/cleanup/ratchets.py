"""Bring the checked-in guard baselines in line after a cleanup, without ever loosening them.

* tools/architecture/ownership-exceptions.json: approved lines that no longer exist are
  dropped (an entry with nothing left goes); nothing is ever added.
* tools/architecture/shell-baseline.json: each count is lowered to what the tree has now;
  no count is raised and no key is added.
* tests/ui_consistency/surfaces.json: the reviewed-file hashes are re-taken only for the
  files the cleanup itself changed; every other entry keeps its recorded hash.
"""
import collections, importlib.util, json, os, subprocess, sys

def _load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
    return mod

def shrink_ownership(root='.'):
    path = os.path.join(root, 'tools/architecture/ownership-exceptions.json')
    guard = os.path.join(root, 'tools/architecture/ownership-guard.py')
    if not (os.path.exists(path) and os.path.exists(guard)): return False
    g = _load(guard, 'ownership_guard')
    from pathlib import Path
    found = g.scan(Path(root).resolve())
    data = json.load(open(path), object_pairs_hook=collections.OrderedDict)
    changed = False
    for name in list(data['exceptions']):
        entry = data['exceptions'][name]
        hits = found.get(name, {})
        for cat in list(entry.get('approved', {})):
            have = collections.Counter(t for _, t in hits.get(cat, []))   # a line may be approved twice
            keep = []
            for t in entry['approved'][cat]:
                if have[t] > 0: keep.append(t); have[t] -= 1
            if keep != entry['approved'][cat]:
                changed = True
                if keep: entry['approved'][cat] = keep
                else: del entry['approved'][cat]
        if not entry.get('approved'):
            del data['exceptions'][name]; changed = True
    if changed: open(path, 'w').write(json.dumps(data, indent=1) + '\n')
    return changed

def lower_shell_baseline(root='.'):
    path = os.path.join(root, 'tools/architecture/shell-baseline.json')
    guard = os.path.join(root, 'tools/architecture/shell-guard.py')
    if not (os.path.exists(path) and os.path.exists(guard)): return False
    before = open(path).read(); old = json.loads(before)
    try:
        subprocess.run([sys.executable, guard, '--record'], check=True, capture_output=True)
        actual = json.load(open(path))
    finally:
        open(path, 'w').write(before)
    new = {k: min(v, actual.get(k, 0)) for k, v in old.items() if actual.get(k, 0) > 0}
    if new == old: return False
    open(path, 'w').write(json.dumps(new, indent=2, sort_keys=True) + '\n')
    return True

def _source(key, root):
    f = key.split(':', 1)[0]
    for base in ('app/ui', '', 'include/breadbox', 'app/settings', 'app/services', 'platform/3ds'):
        p = os.path.normpath(os.path.join(base, f))
        if os.path.isfile(os.path.join(root, p)): return p
    for dp, _, fs in os.walk(os.path.join(root, 'systems')):
        if os.path.basename(f) in fs:
            return os.path.relpath(os.path.join(dp, os.path.basename(f)), root)
    return None

def rehash_surfaces(touched, root='.', gone=()):
    """touched: repository-relative paths the cleanup changed; gone: basenames it removed."""
    path = os.path.join(root, 'tests/ui_consistency/surfaces.json')
    check = os.path.join(root, 'tests/ui_consistency/check.py')
    if not (os.path.exists(path) and os.path.exists(check)): return False
    touched = {os.path.normpath(t) for t in touched}
    before = open(path).read(); old = json.loads(before)
    try:
        subprocess.run([sys.executable, check, '--record'], capture_output=True)
        actual = json.load(open(path))
    finally:
        open(path, 'w').write(before)
    new = {}
    for k in sorted(set(old) | set(actual)):
        mine = _source(k, root) in touched or k in gone
        if mine:
            if k in actual: new[k] = actual[k]
        elif k in old:
            new[k] = old[k]
    if new == old: return False
    open(path, 'w').write(json.dumps(new, indent=2, sort_keys=True) + '\n')
    return True
