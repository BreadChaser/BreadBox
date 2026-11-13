#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/gba" \
  $(pkg-config --cflags minizip) "$root/systems/gba/gba_cart.c" \
  "$root/tests/gba_cart/check.c" $(pkg-config --libs minizip) -lz -Wl,--wrap=fread -o "$tmp/check"
(cd "$tmp" && ./check)
