#!/bin/sh
# The browser/first-run formats come from the registry, not from a PS1 list (app/ui/breadbox_formats.c).
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
for nes in 0 1; do
 ${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -DWITH_NES=$nes \
  -ffunction-sections -fdata-sections -Wl,--gc-sections -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" \
  "$root/app/ui/breadbox_formats.c" "$root/app/ui/bb_session.c" "$root/app/ui/bb_order.c" \
  "$root/tests/bb_formats/check.c" -o "$tmp/check$nes"
 "$tmp/check$nes"
done
