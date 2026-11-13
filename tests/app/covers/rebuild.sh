#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -Itests/app/host/skip -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Wall -Wextra -Wno-unused-function -Wno-unused-variable -g \
 -fsanitize="${COVERS_SANITIZER:-address,undefined}" -pthread \
 tests/app/covers/rebuild.c app/ui/covers_png.c app/ui/covers_pack.c \
 app/ui/boxart.c app/ui/safefile.c \
 $(pkg-config --cflags --libs libpng libcurl) -lm -Wl,--wrap=curl_multi_poll -o "$tmp/check"
(cd "$tmp" && ./check)

# The same publication gate must drain GBA borrowers, too.
cc -std=gnu11 -Itests/app/host/skip -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Wall -Wextra -Wno-unused-function -Wno-unused-variable -g \
 -fsanitize="${COVERS_SANITIZER:-address,undefined}" -pthread \
 tests/app/covers/rebuild.c app/ui/covers_png.c app/ui/covers_pack.c \
 app/ui/boxart.c systems/gba/gba_boxart.c app/ui/safefile.c \
 $(pkg-config --cflags --libs libpng libcurl minizip) -lm -Wl,--wrap=curl_multi_poll -o "$tmp/gba-check"
(cd "$tmp" && ./gba-check)
