"""Exact clip metadata operands, with only the declared build version substituted."""
from pathlib import Path
import re
import subprocess
import compare

ROOT = Path(__file__).resolve().parents[2]
FORMAT = ('idle=%d\nidleptr=%d\nidledelay=%d\nrenderer=%s\nconsole=%s\n'
          'app_version={version}\ngldbg=%d\ngpu_async=%d\n')


def version(header):
    rows = re.findall(r'^\s*#define\s+BB_VERSION\s+"([0-9]+\.[0-9]+\.[0-9]+)"\s*$',
                      Path(header).read_text(), re.M)
    if len(rows) != 1:
        raise ValueError('expected one literal BB_VERSION in ' + str(header))
    return rows[0]


def header_for(elf):
    # Normal builds and the qualified public reconstruction retain their source
    # root above build/3ds. A detached prebuilt reference needs its source header.
    for parent in Path(elf).resolve().parents:
        header = parent / 'apps/breadbox/version.h'
        if header.is_file():
            return header
    raise ValueError('recording metadata needs the source version.h for ' + str(elf))


def literals(image, start, size):
    dump = subprocess.check_output([compare.TOOLS + 'objdump', '-d',
                                    '--start-address=' + str(start),
                                    '--stop-address=' + str(start + size), image.path], text=True)
    return {int(at, 16) - start for at in re.findall(
        r'^\s*([0-9a-f]+):\s+[0-9a-f]+\s+\.word\b', dump, re.M)}


def string(image, address):
    if not compare.in_rodata(image.elf, address):
        return None
    for va, off, size, _ in image.elf['secs']:
        if va <= address < va + size:
            data = image.elf['data'][off + address - va:off + min(address - va + 512, size)]
            if b'\0' not in data:
                return None
            value = bytes(data).split(b'\0', 1)[0]
            if value and all(32 <= c < 127 or c in (9, 10, 13) for c in value):
                return value
    return None


def words(images, aa, bb, size, base_header, new_header):
    """Apply the ordinary instruction comparison, then validate every clip string.

    Only the matching disassembler-marked metadata operand may substitute the
    two source headers' versions. Equal pointer words and gap identities do not
    bypass content checks here. No other function receives this rule.
    """
    refs, changed = images.words(aa, bb, size)
    changed = set(changed)
    left = literals(images.a, aa, size)
    right = literals(images.b, bb, size)
    expected_a = FORMAT.format(version=version(base_header)).encode()
    expected_b = FORMAT.format(version=version(new_header)).encode()
    metadata = []
    for offset in sorted(left | right):
        wa = compare.word(images.A, aa + offset)
        wb = compare.word(images.B, bb + offset)
        sa, sb = string(images.a, wa), string(images.b, wb)
        if sa == expected_a:
            metadata.append(offset)
            if offset in left & right and sb == expected_b:
                if offset in changed:
                    changed.remove(offset)
                    refs += 1
            else:
                changed.add(offset)
        elif sa is not None or sb is not None:
            if sa != sb:
                changed.add(offset)
    if len(metadata) != 1:
        raise ValueError('clip_start must contain exactly one expected recording metadata operand')
    return refs, sorted(changed)
