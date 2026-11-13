#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d);trap 'rm -rf "$tmp"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/arena/check.c -o "$tmp/check"
"$tmp/check"
cc -O1 -DINLINE=inline -ffunction-sections -fdata-sections -Wl,--gc-sections \
 -fsanitize=address,undefined -Igba-core -Igba-core/libretro -Igba-core/libretro/libretro-common/include \
 tests/arena/battery.c gba-core/gba_memory.c -lm -o "$tmp/battery"
"$tmp/battery"
python3 tests/arena/entry-guard.py

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Itests/app/host/skip -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Itests/arena/fakes -Icore/frontend/3ds tests/arena/sdk.c -o "$tmp/sdk"
"$tmp/sdk"
echo "PASS production CTR arena SVC boundary"
