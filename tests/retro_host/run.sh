#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined -g \
 -Isystems/common -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Icore/deps/libretro-common/include \
 tests/retro_host/check.c systems/common/retro_host.c -lm -o "$out/check"
"$out/check"

python3 tests/retro_host/ctr_lifecycle.py

# The shared rewind engine (GBA) with its real worker thread on a host pthread.
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -pthread \
 -Itests/app/host -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Isystems/common -Icore/deps/libretro-common/include \
 tests/retro_host/rewind.c -o "$out/rewind"
"$out/rewind"
