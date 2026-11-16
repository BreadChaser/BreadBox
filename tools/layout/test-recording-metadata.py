#!/usr/bin/env python3
"""Recording versions may vary; instructions and all other text may not."""
import copy
from pathlib import Path
import struct
import sys
import tempfile
import compare
import hotcode
import recording_metadata as metadata

base, new = sys.argv[1:3]
images = hotcode.Images(base, new)
aa, size = hotcode.defs(base)['clip_start'][0]
bb, new_size = hotcode.defs(new)['clip_start'][0]
assert size == new_size
base_header = metadata.header_for(base)
new_header = metadata.ROOT / 'apps/breadbox/version.h'
refs, changed = metadata.words(images, aa, bb, size, base_header, new_header)
assert not changed, ('real recording metadata case', changed)
left, right = metadata.literals(images.a, aa, size), metadata.literals(images.b, bb, size)
expected = metadata.FORMAT.format(version=metadata.version(base_header)).encode()
operand = next(offset for offset in left if metadata.string(images.a, compare.word(images.A, aa + offset)) == expected)
other = next(offset for offset in left & right if offset != operand and
             metadata.string(images.a, compare.word(images.A, aa + offset)) is not None)


def put_word(image, at, value):
    raw = bytearray(image.elf['data'])
    for va, off, length, _ in image.elf['secs']:
        if va <= at < va + length:
            struct.pack_into('<I', raw, off + at - va, value)
            image.elf['data'] = raw
            return
    raise AssertionError('operand outside ELF')


def replace_string(image, at, text):
    # Give each replacement its own read-only range: neither identical addresses
    # nor an unchanged anonymous gap may accidentally make the control pass.
    address = max(va + length for va, _, length, _ in image.elf['secs']) + 0x1000
    raw = bytearray(image.elf['data'])
    image.elf['secs'].append((address, len(raw), len(text) + 1, '.rodata.control'))
    raw.extend(text + b'\0')
    image.elf['data'] = raw
    put_word(image, at, address)


with tempfile.TemporaryDirectory() as td:
    # Exercise different declared source versions even when CI compares NEW to
    # itself. The real public-reference pair above also runs in release checks.
    header = Path(td) / 'version.h'
    old = '0.0.1' if metadata.version(new_header) != '0.0.1' else '0.0.2'
    header.write_text('#define BB_VERSION "' + old + '"\n')
    pair = copy.deepcopy(images)
    replace_string(pair.a, aa + operand, metadata.FORMAT.format(version=old).encode())
    refs, changed = metadata.words(pair, aa, bb, size, header, new_header)
    assert not changed, ('declared version relocation', changed)

    altered = copy.deepcopy(pair)
    value = metadata.FORMAT.format(version=metadata.version(new_header)).encode().replace(b'idle=%d', b'Idle=%d', 1)
    replace_string(altered.b, bb + operand, value)
    assert operand in metadata.words(altered, aa, bb, size, header, new_header)[1], 'non-version metadata character accepted'

    altered = copy.deepcopy(pair)
    replace_string(altered.b, bb + operand, metadata.FORMAT.format(version='9.8.7').encode())
    assert operand in metadata.words(altered, aa, bb, size, header, new_header)[1], 'undeclared version accepted'

    altered = copy.deepcopy(pair)
    replace_string(altered.a, aa + other, b'other_version=0.9.0')
    replace_string(altered.b, bb + other, b'other_version=0.9.2')
    assert other in metadata.words(altered, aa, bb, size, header, new_header)[1], 'another version-shaped string accepted'

    altered = copy.deepcopy(pair)
    put_word(altered.b, bb, compare.word(altered.B, bb) ^ 1)
    assert 0 in metadata.words(altered, aa, bb, size, header, new_header)[1], 'clip_start instruction flip accepted'

    # Same-address content changes must fail too, before gap/pointer shortcuts.
    altered = copy.deepcopy(images)
    address = compare.word(altered.B, bb + operand)
    raw = bytearray(altered.B['data'])
    for va, off, length, _ in altered.B['secs']:
        if va <= address < va + length:
            raw[off + address - va] ^= 32
            break
    altered.B['data'] = raw
    assert operand in metadata.words(altered, aa, bb, size, base_header, new_header)[1]
print('recording metadata: real/declared version relocation PASS; changed text, undeclared version, other string, instruction and equal-pointer controls rejected')
