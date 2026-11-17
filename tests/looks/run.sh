#!/bin/sh
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
python3 "$root/tests/looks/hit_geometry.py" > "$tmp/hits.inc"
cc -DBB_UI_TRACE_HOST -DSHELF_HIT_TEST=\"$tmp/hits.inc\" -std=gnu11 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=all \
 -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/core/deps/libretro-common/include" \
 "$root/tests/looks/check.c" -lm -o "$tmp/check"
"$tmp/check"

cc -std=gnu11 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=all \
 -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" \
 -I"$root/core/deps/libretro-common/include" "$root/tests/looks/fonts.c" -o "$tmp/fonts"
"$tmp/fonts"
