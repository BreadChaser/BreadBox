#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
bin=$(mktemp /tmp/gba-bitmap-effects.XXXXXX)
trap 'rm -f "$bin"' EXIT
for format in FRONTEND_SUPPORTS_RGB565 USE_XBGR1555_FORMAT; do
 c++ -DINLINE=inline -D"$format" -O1 -g -fsanitize=address,undefined -I gba-core -I gba-core/libretro/libretro-common/include tests/gba_bitmap_effects/check.cc -o "$bin"
 "$bin"
done
