#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
python3 - "$root" <<'COUNT'
from pathlib import Path
import sys
s=(Path(sys.argv[1])/'app/ui/bb_index.c').read_text()
f=s[s.index('int bb_index_count('):s.index('/* Callers walk')]
assert 'system_counts' in f and 'items[' not in f and 'current->count' not in f
COUNT
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -g ${TEST_CFLAGS:-} \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -DBB_INDEX_TEST -DBB_INDEX_CACHE='"index"' -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" \
    $(pkg-config --cflags minizip) "${BB_INDEX_SOURCE:-$root/app/ui/bb_index.c}" \
    "$root/systems/gba/sys_gba_scan.c" "$root/systems/ps1/sys_ps1_scan.c" "$root/tests/bb_index/registry.c" "$root/app/ui/bb_session.c" "$root/app/ui/bb_order.c" "$root/systems/gba/gba_saveid.c" "$root/systems/gba/gba_cart.c" "$root/app/ui/safefile.c" "$root/tests/bb_index/check.c" \
    -pthread -Wl,--wrap=usleep -Wl,--wrap=clock_gettime $(pkg-config --libs minizip) -lz -o "$tmp/check"
python3 - "$tmp" "$root" <<'PY'
import sys,re,pathlib,zipfile
base=pathlib.Path(sys.argv[1]); source=pathlib.Path(sys.argv[2])/'systems/gba/gba_cart.c'
logo=bytes(int(x,16) for x in re.findall(r'0x[0-9a-fA-F]+',re.search(r'nintendo_logo\[156\] = \{(.*?)\};',source.read_text(),re.S)[1]))
h=bytearray(192);h[4:160]=logo;h[0xb2]=0x96;h[0xbd]=(-sum(h[0xa0:0xbd])-0x19)&255
for d in ['sdmc:/roms','sdmc:/roms/(1994) Sony PlayStation - V1.0','sdmc:/roms/(2001) Nintendo Game Boy Advance - V1.2/Games/Boktai','sdmc:/roms/(2001) Nintendo Game Boy Advance - V1.2/Games/Donkey Kong','sdmc:/roms/(2001) Nintendo Game Boy Advance - V1.2/Games/Wario','sdmc:/roms/gba','sdmc:/roms/gb','sdmc:/roms/gbc','sdmc:/roms/nes','sdmc:/roms/snes','sdmc:/roms/nds','sdmc:/roms/n64','roms/GBC','roms/PSX','roms/GBA','empty']: (base/d).mkdir(parents=True)
(base/'sdmc:/roms/gba/Place GBA ROMs here').write_text('')
for i in range(48): (base/f'sdmc:/roms/(2001) Nintendo Game Boy Advance - V1.2/Games/game{i:02}.gba').write_bytes(h)
for i in range(30):
 with zipfile.ZipFile(base/f'sdmc:/roms/(2001) Nintendo Game Boy Advance - V1.2/Games/series{i:02}.zip','w') as z:z.writestr('inside.gba',h)
for series in ['Boktai','Donkey Kong','Wario']:
 (base/f'sdmc:/roms/(2001) Nintendo Game Boy Advance - V1.2/Games/{series}/{series}.gba').write_bytes(h)
(base/'roms/GBC/mislabeled.GBA').write_bytes(h)
(base/'roms/GBA/bad.gba').write_bytes(bytes(192))
(base/'roms/GBA/native.gba').write_bytes(h)
# Sparse ROM exercises streaming identity without allocating a cartridge buffer.
with (base/'roms/GBA/native.gba').open('ab') as f:f.truncate(32*1024*1024)
with zipfile.ZipFile(base/'roms/GBC/one.ZIP','w',zipfile.ZIP_DEFLATED) as z:z.writestr('game.gba',h)
with zipfile.ZipFile(base/'roms/GBA/many.zip','w') as z:
 z.writestr('one.gba',h);z.writestr('two.gba',h)
for name in ['one.cue','one.bin']:(base/'roms/PSX'/name).write_bytes(b'')
for i in range(100):(base/'roms/GBC'/f'irrelevant{i}.gb').write_bytes(b'')
# The boundary directory (12) is opened; directory 13 is deliberately omitted.
deep=base/'deep'
for i in range(12):deep=deep/'d'
deep.mkdir(parents=True);(deep/'yes.gba').write_bytes(h)
(deep/'d').mkdir();(deep/'d/no.gba').write_bytes(h)
(base/'many').mkdir()
for i in range(8300):(base/'many'/f'{i}.bin').touch()
(base/'grow').mkdir()
for i in range(10):(base/'grow'/f'{i}.bin').touch()
PY
(cd "$tmp" && ./check)
# A separate process must reuse disk cache, with no ROM reads.
mv "$tmp/roms" "$tmp/hidden"
mkdir "$tmp/roms"
(cd "$tmp" && ./check cached)
# Version 6 intentionally invalidates pre-registry caches (one rescan).
rmdir "$tmp/roms"
mv "$tmp/hidden" "$tmp/roms"
# Validation of the intentionally empty folder persisted an empty cache.
(cd "$tmp" && ./check)
# Reject truncated, tampered, excessive-count, and traversal caches. Even a
# traversal record with a correctly recomputed checksum must be rejected.
cp "$tmp/index" "$tmp/good"
for kind in truncated checksum count traversal; do
 python3 - "$tmp" "$kind" <<'PY'
import sys,pathlib,struct
p=pathlib.Path(sys.argv[1]);b=bytearray((p/'good').read_bytes());kind=sys.argv[2]
if kind=='truncated':b=b[:40]
elif kind=='checksum':b[-1]^=1
elif kind=='count':b[8:12]=struct.pack('<I',0xffffffff)
else:
 rl=struct.unpack('<I',b[12:16])[0];start=20+rl+3
 ln=struct.unpack('<H',b[start-2:start])[0]
 b[start:start+ln]=b'roms/../'+b'x'*(ln-8)
 h=2166136261
 for c in b[20:]:h=((h^c)*16777619)&0xffffffff
 b[16:20]=struct.pack('<I',h)
(p/'index').write_bytes(b)
PY
 (cd "$tmp" && ./check)
done

# A slow alphabetically earlier disc tree: system folders are still visited by priority, and with no scan deadline
# both systems' games are all found.
mkdir -p "$tmp/slow-root/(1994) Sony PlayStation" "$tmp/slow-root/(2001) Nintendo Game Boy Advance"
cp "$tmp/roms/GBA/native.gba" "$tmp/slow-root/(2001) Nintendo Game Boy Advance/late.gba"
for n in $(seq 1 50); do touch "$tmp/slow-root/(1994) Sony PlayStation/$n.cue"; done
cat > "$tmp/slow.c" <<'C'
#include "bb_index.h"
#include <assert.h>
#include <unistd.h>
int main(void) {
 bb_index_start("slow-root",true);
 while(bb_index_busy())usleep(10000);
 /* no deadline any more: the old 250 ms budget flag below is ignored, every game is found */
 assert(bb_index_gba_count()==1);
 assert(bb_index_ps1_count()==50);
 assert(bb_index_status()==BB_INDEX_COMPLETE);
 bb_index_shutdown();
}
C
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -g ${TEST_CFLAGS:-} \
 -ffunction-sections -fdata-sections -Wl,--gc-sections -DBB_INDEX_TEST -DBB_INDEX_SCAN_MS=250 -DBB_INDEX_CACHE='"slow-index"' -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" \
 $(pkg-config --cflags minizip) "${BB_INDEX_SOURCE:-$root/app/ui/bb_index.c}" \
 "$root/systems/gba/sys_gba_scan.c" "$root/systems/ps1/sys_ps1_scan.c" "$root/tests/bb_index/registry.c" "$root/app/ui/bb_session.c" "$root/app/ui/bb_order.c" "$root/systems/gba/gba_saveid.c" "$root/systems/gba/gba_cart.c" "$root/app/ui/safefile.c" "$tmp/slow.c" \
 -pthread $(pkg-config --libs minizip) -lz -o "$tmp/slow"
(cd "$tmp" && ./slow)

# A persisted cache is a starting view, never a permanent directory listing.
cp "$tmp/roms/GBA/native.gba" "$tmp/roms/GBA/added.gba"
(cd "$tmp" && ./check changes 4)
rm "$tmp/roms/GBA/added.gba"
(cd "$tmp" && ./check changes 3)

# The folders waiting to be walked: trees trunk completes complete here (cold), their memory is bounded over the whole traversal,
# and a limit, an allocation failure or a cancel frees it all and keeps the cached games.
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -g ${TEST_CFLAGS:-} \
 -ffunction-sections -fdata-sections -Wl,--gc-sections -DBB_INDEX_TEST -DBB_INDEX_CACHE='"pending-index"' -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" \
 $(pkg-config --cflags minizip) "${BB_INDEX_SOURCE:-$root/app/ui/bb_index.c}" \
 "$root/systems/gba/sys_gba_scan.c" "$root/systems/ps1/sys_ps1_scan.c" "$root/tests/bb_index/registry.c" "$root/app/ui/bb_session.c" "$root/app/ui/bb_order.c" "$root/systems/gba/gba_saveid.c" "$root/systems/gba/gba_cart.c" "$root/app/ui/safefile.c" "$root/tests/bb_index/pending.c" \
 -pthread $(pkg-config --libs minizip) -lz -o "$tmp/pending"
mkdir "$tmp/branch-root"
# no thread cache: glibc then reuses freed chunks whole, the allocator state tests/bb_index/pending.c fragments on purpose
(cd "$tmp/branch-root" && GLIBC_TUNABLES=glibc.malloc.tcache_count=0 ../pending)

# Re-enumeration must preserve raw prefix identities and treat metadata read failures as errors.
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -g ${TEST_CFLAGS:-} \
 -ffunction-sections -fdata-sections -Wl,--gc-sections -DBB_INDEX_TEST -DBB_INDEX_CACHE='"reread-index"' -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" \
 $(pkg-config --cflags minizip) "${BB_INDEX_SOURCE:-$root/app/ui/bb_index.c}" \
 "$root/systems/gba/sys_gba_scan.c" "$root/systems/ps1/sys_ps1_scan.c" "$root/tests/bb_index/registry.c" "$root/app/ui/bb_session.c" "$root/app/ui/bb_order.c" "$root/systems/gba/gba_saveid.c" "$root/systems/gba/gba_cart.c" "$root/app/ui/safefile.c" "$root/tests/bb_index/reread.c" \
 -pthread -Wl,--wrap=opendir -Wl,--wrap=readdir -Wl,--wrap=closedir -Wl,--wrap=lstat $(pkg-config --libs minizip) -lz -o "$tmp/reread"
for scenario in metadata-error balanced-rename case-permutation allocation-sweep; do
 mkdir "$tmp/$scenario"
 (cd "$tmp/$scenario" && ../reread "$scenario")
done
