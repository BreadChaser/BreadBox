#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -DGBA_BOXART_TABLE='"assets/romfs/covers/gba-codes.txt"' -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Isystems/gba -Wall -Wextra -fsanitize=address,undefined tests/gba_covers/check.c systems/gba/gba_boxart.c app/ui/boxart.c app/ui/safefile.c $(pkg-config --cflags --libs minizip) -o "$tmp/check"
python3 - "$tmp/archive.zip" <<'PY'
import sys,zipfile
with zipfile.ZipFile(sys.argv[1],'w') as z:z.writestr('nested/Kirby & The Amazing Mirror (USA).gba',b'test')
PY
# The assertion fixture takes its ZIP path through the environment.
GBA_TEST_ZIP="$tmp/archive.zip" "$tmp/check"
