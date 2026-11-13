#!/usr/bin/env python3
"""Compare rebuilt room assets with the shipped ones (assets/romfs/room).

    python3 tools/room/check.py [--built tools/room/build/romfs/room] [--shipped assets/romfs/room]
                                [--manifest tools/room/ASSETS.sha256] [--all] [--update]

Every shipped file that was rebuilt must equal the rebuilt one byte for byte (SHA-256), and the shipped set must be
the one tools/room/ASSETS.sha256 lists. The hands' files are rebuilt only by `build.sh --hands` (Cycles, slow): a
shipped file that was not rebuilt is reported, and is a failure only with --all. Exit status 0 when nothing fails.
--update rewrites the manifest from the SHIPPED files: run it in the same commit that changes a shipped binary, after
build.sh + check.py show the rebuild matches.
"""
import argparse, hashlib, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))


def sha(p):
    return hashlib.sha256(open(p, 'rb').read()).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--built', default=os.path.join(HERE, 'build', 'romfs', 'room'))
    ap.add_argument('--shipped', default=os.path.join(REPO, 'assets', 'romfs', 'room'))
    ap.add_argument('--manifest', default=os.path.join(HERE, 'ASSETS.sha256'))
    ap.add_argument('--all', action='store_true', help='a shipped file that was not rebuilt fails too')
    ap.add_argument('--update', action='store_true')
    a = ap.parse_args()
    shipped = {f: sha(os.path.join(a.shipped, f)) for f in sorted(os.listdir(a.shipped))}
    if a.update:
        open(a.manifest, 'w').write(''.join('%s  %s\n' % (h, f) for f, h in shipped.items()))
        print('wrote', a.manifest)
        return 0
    bad = 0
    listed = {}
    if os.path.exists(a.manifest):
        for line in open(a.manifest):
            h, f = line.split()
            listed[f] = h
    for f in sorted(set(shipped) | set(listed)):
        s, l = shipped.get(f), listed.get(f)
        b = os.path.join(a.built, f)
        r = sha(b) if os.path.exists(b) else None
        fail = True
        if s is None:
            state = 'LISTED BUT NOT SHIPPED'
        elif l is not None and l != s:
            state = 'MANIFEST STALE (shipped file changed without --update)'
        elif r is None:
            state = 'not rebuilt (build.sh --hands)'
            fail = a.all
        elif r != s:
            state = 'MISMATCH rebuilt %s' % r[:12]
        elif l is None:
            state = 'match, but not in the manifest'
        else:
            state, fail = 'match', False
        bad += fail
        print('%-16s %s  %s' % (f, (s or '-' * 12)[:12], state))
    print('room assets: %s' % ('all match' if not bad else '%d problem(s)' % bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
