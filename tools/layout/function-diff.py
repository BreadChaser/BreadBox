#!/usr/bin/env python3
"""function-diff.py BASE.elf NEW.elf: every function whose code differs between two builds.

Unlike the layout gates, which look only at pinned frame functions and the core, this
compares every sized function (nm t/T) in both images. Functions are matched by name; a
name that occurs several times (static functions in different files) is matched in
address order. A function differs when it is added, removed, changes size, or has an
instruction word that is not a relocated reference to the same symbol (hotcode.words()).
Moving to another address alone is not a difference. Build both ELFs at the same path
with the same SOURCE_DATE_EPOCH, PSX3D_COMMIT and PSX3D_DESCRIBE. Run in the container.

Prints one line per differing function, then a summary; exits 1 when any differ."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hotcode  # noqa: E402


def differences(base, new):
    images = hotcode.Images(base, new)
    a, b = hotcode.defs(base), hotcode.defs(new)
    rows, compared = [], 0
    for name in sorted(set(a) | set(b)):
        left, right = sorted(a.get(name, [])), sorted(b.get(name, []))
        for i in range(max(len(left), len(right))):
            label = name if max(len(left), len(right)) == 1 else f'{name}#{i}'
            if i >= len(left):
                rows.append(('added', label, f'{right[i][1]} bytes'))
                continue
            if i >= len(right):
                rows.append(('removed', label, f'{left[i][1]} bytes'))
                continue
            (aa, asize), (bb, bsize) = left[i], right[i]
            compared += 1
            if asize != bsize:
                rows.append(('size', label, f'{asize} -> {bsize} bytes'))
                continue
            refs, changed = images.words(aa, bb, asize)
            if changed:
                rows.append(('code', label, f'{len(changed)} changed words of {asize // 4}'))
    return compared, rows


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    compared, rows = differences(sys.argv[1], sys.argv[2])
    for kind, name, detail in rows:
        print(f'{kind:8s} {name}  {detail}')
    print(f'{compared} functions in both images, {len(rows)} differ')
    sys.exit(1 if rows else 0)


if __name__ == '__main__':
    main()
