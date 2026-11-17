#!/usr/bin/env python3
"""covers_match.c on the host, alone and without any 3DS header: 32 well-known PS1 titles
(titles.tsv: regions, discs, revisions, punctuation, a leading article, near-duplicates) must give
exactly the listed thumbnail file name and URL escape; they and a few hard names also go through
the cache paths and the hash, checked against Python oracles."""
import os, subprocess, tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[3]
table = [l.split('\t') for l in Path(__file__).with_name('titles.tsv').read_text().splitlines() if l and not l.startswith('#')]
assert len(table) == 32 and all(len(r) == 3 for r in table)
titles = [r[0] for r in table]
expected = {r[0]: (r[1].encode(), r[2].encode()) for r in table}
assert len(set(titles)) == len(titles) and len({r[1] for r in table}) == len(titles)
hard = ['Final Fantasy VII: Disc 1/3', 'What? Why <Not> "Quoted" | Back\\slash*`', 'Pokémon Stadium', 'Ōkami & Friends',
        'a' * 400, '\u00e9' * 200]   # names cut to 255 bytes; the second needs a %XX for every byte
SPECIAL = '&*/:`<>?\\|"'


def thumb(s, cap=256):
    b = s.encode()[:cap - 1]
    return bytes(ord('_') if c in SPECIAL.encode() else c for c in b)


def escape(b, cap=768):
    out = bytearray()
    for c in b:
        if len(out) >= cap - 4:
            break
        if chr(c).isascii() and (chr(c).isalnum() or chr(c) in '-_.~'):
            out.append(c)
        else:
            out += b'%%%02X' % c
    return bytes(out)


def fnv(b):
    h = 1469598103934665603
    for c in b:
        h = ((h ^ c) * 1099511628211) & (2 ** 64 - 1)
    return h


driver = r'''
#include <stdio.h>
#include <string.h>
#include "bb_system.h"
#include "covers_match.h"
static const bb_system_t gba = { .id = "gba" };
const bb_system_t *bb_system_by_id(const char *id) { return !strcmp(id, "gba") ? &gba : NULL; }
int main(void)
{
	char key[1024];
	printf("dir\t%s\n", COVER_DIR);
	while (fgets(key, sizeof(key), stdin)) {
		key[strcspn(key, "\n")] = 0;
		char name[256], esc[768], path[600], gba_key[1100], gba_path[600], other[1100], untouched[16] = "untouched";
		covers_thumb_name(key, name, sizeof(name));
		covers_url_escape(name, esc, sizeof(esc));
		covers_cache_path(key, ".cv2", path, sizeof(path));
		snprintf(gba_key, sizeof(gba_key), "gba/%s", key);
		covers_cache_path(gba_key, ".png", gba_path, sizeof(gba_path));
		snprintf(other, sizeof(other), "zzz/%s", key);
		covers_cache_path(other, ".cv2", untouched, sizeof(untouched));   // an unknown system: nothing written
		printf("%s\t%s\t%s\t%s\t%s\t%016llx\n", name, esc, path, gba_path, untouched, (unsigned long long)covers_name_hash(key));
	}
	char small[24] = "x";
	covers_cache_path("Final Fantasy VII (USA) (Disc 1)", ".cv2", small, sizeof(small));   // no room: empty, never cut
	printf("small\t%s\n", small);
	char tiny[4];
	covers_thumb_name("a:b:c", tiny, sizeof(tiny));
	printf("tiny\t%s\n", tiny);
	char cut[8];
	covers_url_escape("a&b c", cut, sizeof(cut));
	printf("cut\t%s\n", cut);
	return 0;
}
'''
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as td:
    td = Path(td)
    (td / 'no3ds').mkdir()
    for h in ('3ds.h', 'citro2d.h', 'citro3d.h'):
        (td / 'no3ds' / h).write_text('#error "covers_match.c must build without 3DS headers"\n')
    (td / 'driver.c').write_text(driver)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I', str(td / 'no3ds'), '-I', str(root / 'app/ui'), '-I', str(root / 'include/breadbox'),
                    str(td / 'driver.c'), str(root / 'app/ui/covers_match.c'), '-o', str(td / 'check')], check=True)
    keys = titles + hard
    out = subprocess.run([str(td / 'check')], input=('\n'.join(keys) + '\n').encode(), capture_output=True, check=True).stdout.split(b'\n')
dir_ = out[0].split(b'\t')[1]
assert dir_.endswith(b'/covers'), dir_
hashes = set()
for key, row in zip(keys, out[1:]):
    name, esc, path, gba_path, untouched, h = row.split(b'\t')
    want = thumb(key)
    assert name == want and not any(c in name for c in SPECIAL.encode()), (key, name)
    assert esc == escape(want), (key, esc)
    assert path == (dir_ + b'/' + want + b'.cv2' if len(want) + 4 < 280 else b''), (key, path)
    gba = thumb(key)   # the part after "gba/" is sanitized the same way, in the system's own folder
    assert gba_path == (dir_ + b'/gba/' + gba + b'.png' if len(gba) + 4 < 280 else b''), (key, gba_path)
    assert untouched == b'untouched', (key, untouched)
    assert int(h, 16) == fnv(key.encode()), key
    if key in expected:
        assert (name, esc) == expected[key], (key, name, esc)
    hashes.add(h)
assert len(hashes) == len(keys), 'two cover keys share a hash'
assert out[len(keys) + 1] == b'small\t', out[len(keys) + 1]
assert out[len(keys) + 2] == b'tiny\ta_b', out[len(keys) + 2]
assert out[len(keys) + 3] == b'cut\ta%26', out[len(keys) + 3]   # an escape that does not fit stops between whole %XX
print(f'cover names PASS: {len(titles)} titles give their listed file names and escapes, {len(hard)} hard names; cache paths, '
      'hash, truncation and a too-small path buffer match the oracles; no collisions; built without 3DS headers')
