"""Reviewed per-function change manifest shared by check-pinned.py (gate "frontend") and
check-system-pinned.py (gate "system"): tools/layout/accepted-changes.json.

An intentional change to a pinned function is accepted only through an entry that records,
for that one function: why, what evidence supports it, and the exact before/after
(size + normalized code fingerprint, hotcode.fingerprint). The entry matches only that
pair: if the baseline function is not the recorded `base`, or the candidate drifts from
the recorded `new`, or the function did not change at all, the gate fails. Page offsets
stay pinned (the base page) unless the entry names `new_page_offset` with a `page_reason`.
Print a stub for a changed function with tools/layout/accept-change.py."""
import json, os, re
from pathlib import Path

HERE = Path(__file__).resolve().parent
GATES = ('frontend', 'system')


def manifest_path():
    return Path(os.environ.get('ACCEPTED_CHANGES') or HERE / 'accepted-changes.json')


def _text(v):
    return isinstance(v, str) and bool(v.strip())


def load(gate, errors):
    """{name: entry} of this gate; validation problems are appended to errors."""
    try:
        data = json.loads(manifest_path().read_text())
        functions = data['functions']
        assert isinstance(functions, dict)
    except Exception as e:
        errors.append(f'accepted-changes manifest {manifest_path()} unusable: {e}')
        return {}
    mine = {}
    for entry_key, e in functions.items():
        name = e.get('symbol', entry_key) if isinstance(e,dict) else entry_key
        if not isinstance(name,str) or not re.fullmatch(r'[A-Za-z_.$][A-Za-z0-9_.$]*',name):
            errors.append(f'accepted-changes: {entry_key}: invalid symbol');continue
        if not isinstance(e, dict) or e.get('gate') not in GATES:
            errors.append(f'accepted-changes: {name}: needs "gate": one of {GATES}')
            continue
        if e.get('change', 'code') not in ('code', 'page-only'):
            errors.append(f'accepted-changes: {name}: unknown change kind')
        if e.get('change') == 'page-only' and ('new_page_offset' not in e or not _text(e.get('page_reason'))):
            errors.append(f'accepted-changes: {name}: page-only needs an explicit offset and page_reason')
        for key in ('reason', 'evidence'):
            if not _text(e.get(key)):
                errors.append(f'accepted-changes: {name}: needs a non-empty "{key}"')
        for key in ('reason', 'page_reason'):
            if isinstance(e.get(key), str) and re.search(r'\{[^{}]*\}', e[key]):
                errors.append(f'accepted-changes: {name}: unresolved placeholder in "{key}"')
        for side in ('base', 'new'):
            s = e.get(side)
            if not (isinstance(s, dict) and isinstance(s.get('size'), int) and s['size'] > 0
                    and isinstance(s.get('fingerprint'), str) and re.fullmatch(r'[0-9a-f]{64}', s['fingerprint'])):
                errors.append(f'accepted-changes: {name}: "{side}" needs size and a sha256 fingerprint')
        if 'new_page_offset' in e and not (isinstance(e['new_page_offset'], int) and 0 <= e['new_page_offset'] < 4096
                                           and e['new_page_offset'] % 4 == 0 and _text(e.get('page_reason'))):
            errors.append(f'accepted-changes: {name}: new_page_offset needs a 4-aligned page offset and a "page_reason"')
        if e.get('gate') == gate:
            if name in mine:errors.append(f'accepted-changes: duplicate {gate} symbol {name}')
            mine[name] = e
    return mine


def judge(name, entry, base, new, new_pages):
    """base/new = (address, size, fingerprint). Returns None when the entry accepts exactly
    this change, else the reason it does not."""
    if (base[1], base[2]) != (entry['base']['size'], entry['base']['fingerprint']):
        return f'{name}: the baseline function is not the recorded "base" (size {base[1]}); the baseline moved, re-review the entry'
    if (new[1], new[2]) != (entry['new']['size'], entry['new']['fingerprint']):
        return f'{name}: candidate differs from the accepted change (size {new[1]}, fingerprint {new[2][:12]}, accepted {entry["new"]["size"]}/{entry["new"]["fingerprint"][:12]}): re-review it'
    unchanged = base[1:] == new[1:]
    if entry.get('change') == 'page-only':
        if not unchanged:
            return f'{name}: page-only entry cannot accept code or size changes'
        if base[0] & 4095 == new[0] & 4095:
            return f'{name}: stale page-only entry: the page offset did not change'
        if 'new_page_offset' not in entry or not _text(entry.get('page_reason')):
            return f'{name}: page-only entry needs explicit offset and page_reason'
    elif unchanged:
        return f'{name}: accepted change but the function is unchanged: delete the entry'
    want = entry.get('new_page_offset', base[0] & 4095)
    if want not in new_pages:
        return f'{name}: page offset {sorted(new_pages)} but the accepted change expects {want:#x}'
    return None
