#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
 -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/core/deps/libretro-common/include" \
 "$root/app/ui/bb_controls.c" "$root/tests/bb_controls_rows/check.c" -o "$tmp/check"
"$tmp/check"
