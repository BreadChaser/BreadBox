#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
out=$(mktemp -d);trap 'rm -rf "$out"' EXIT
cc -std=gnu11 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Icore/deps/libretro-common/include tests/parity/check.c \
 app/ui/bb_profile.c app/ui/bb_controls.c app/ui/bb_cheats.c app/ui/safefile.c -o "$out/check"
(cd "$out" && ./check)
python3 tests/parity/backends.py

python3 tests/parity/review.py
