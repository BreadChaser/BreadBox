#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/gba" \
 "$root/tests/gba_identity/check.c" "$root/systems/gba/gba_cart.c" "$root/systems/gba/gba_saveid.c" "$root/app/ui/safefile.c" -lminizip -lz -o "$tmp/check"
python3 - "$root" "$tmp" <<'PY'
import pathlib,sys,re
s=(pathlib.Path(sys.argv[1])/'systems/gba/gba_cart.c').read_text()
logo=bytes(int(x,16) for x in re.findall(r'0x[0-9a-fA-F]+',re.search(r'nintendo_logo\[156\] = \{(.*?)\};',s,re.S)[1]))
h=bytearray(192);h[4:160]=logo
(pathlib.Path(sys.argv[2])/'header').write_bytes(h)
PY
(cd "$tmp" && ./check)

python3 "$root/tests/gba_identity/adapter-cache.py"
