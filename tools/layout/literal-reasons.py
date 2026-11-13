#!/usr/bin/env python3
"""Refresh changed-body literal evidence without assuming pool-slot identity.

This edits reason text only. Fingerprints must still come from accept-change.py.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re


def values(assembly):
    return Counter(int(x, 16) for x in re.findall(
        r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+\.word\s+(0x[0-9a-f]+)', assembly, re.M))


def describe(before, after):
    def listing(items):
        return ', '.join(f'{v:#010x}' + (f' x{n}' if n > 1 else '')
                         for v, n in sorted(items.items())) or 'none'
    return (' Literal-pool values (slots may reorder; no offset correspondence implied): '
            f'{sum((before & after).values())} words retained by value; '
            f'removed {listing(before - after)}; added {listing(after - before)}.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('assembly_dir', type=Path)
    args = parser.parse_args()
    data = json.loads(args.manifest.read_text())
    changed = []
    for key, entry in data['functions'].items():
        if entry.get('diff_class') != 'body':
            continue
        stem = key.replace(':', '-')
        before, after = (values((args.assembly_dir / f'{stem}.{side}.asm').read_text())
                         for side in ('base', 'new'))
        reason = re.split(r' Fresh RC3 literal-pool comparison| Literal-pool values',
                          entry['reason'])[0]
        entry['reason'] = reason + describe(before, after)
        changed.append(key)
    args.manifest.write_text(json.dumps(data, indent=1) + '\n')
    print(f'{len(changed)} changed-body reasons refreshed by value; no fingerprints changed')


if __name__ == '__main__':
    main()
