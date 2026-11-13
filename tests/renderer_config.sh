#!/bin/sh
# Host regression for the real frontend config/options code, without 3DS SDK.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/3ds.h" <<'HEADER'
#pragma once
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef uint64_t u64;
HEADER
cat > "$work/citro2d.h" <<'HEADER'
#pragma once
typedef struct C3D_RenderTarget C3D_RenderTarget;
typedef struct C3D_Tex C3D_Tex;
typedef struct { unsigned short width, height; float left, top, right, bottom; } Tex3DS_SubTexture;
typedef void *C2D_Font;
HEADER
${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-format-truncation \
  -I "$work" -I "$repo/app/ui" -I "$repo/include/breadbox" -I "$repo/platform/3ds" -I "$repo/core/deps/libretro-common/include" \
  "$repo/tests/renderer_config.c" "$repo/app/ui/config.c" \
  "$repo/systems/ps1/options.c" "$repo/systems/ps1/gamecfg.c" "$repo/systems/ps1/presets.c" \
  -DPSX3D_DEFAULTS="\"$repo/assets/romfs/presets/defaults.ini\"" \
  "$repo/systems/ps1/clip.c" "$repo/app/ui/theme.c" "$repo/app/ui/safefile.c" "$repo/tools/input-replay/mock-core.c" -lz -o "$work/test"
mkdir -p "$work/sdmc:/3ds/BreadBox/games"
cd "$work"
./test
