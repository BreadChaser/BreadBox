#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for metadata in present absent; do
    flags=
    if [ "$metadata" = absent ]; then flags=-DNO_METADATA; fi
    cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
      $flags -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" \
      "$root/app/ui/bb_disc.c" "$root/tests/bb_disc/check.c" -o "$tmp/check"
    (cd "$tmp" && ./check)
done

python3 "$root/tests/bb_disc/formats.py"
