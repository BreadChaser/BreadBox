#!/bin/bash
# The PlayStation system reset (config_reset_defaults) must not touch app-owned values or the shared speed hotkeys.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -w -fsanitize=address,undefined -g \
  -I"$root/tests/app/host" -I"$root/core/deps/libretro-common/include" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" \
  -DPSX3D_TEST_PRESETS="\"$root/assets/romfs/presets\"" $(pkg-config --cflags freetype2) \
  "$root/tests/config_reset/check.c" "$root/systems/ps1/clip.c" "$root/app/ui/safefile.c" "$root/tools/input-replay/mock-core.c" \
  $(pkg-config --libs freetype2) -lz -lm -o "$tmp/check"
(cd "$tmp" && ./check)
