#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-format-truncation -fsanitize=address,undefined -g -Itests/app/host -Icore/deps/libretro-common/include -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/bb_library/check.c app/ui/safefile.c -o "$tmp/check"
"$tmp/check"
python3 tests/bb_library/swap.py
