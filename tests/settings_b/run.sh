#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
export PSX3D_TITLE_FONT="$root/tests/app/host/Michroma-Regular.ttf"
export PSX3D_BODY_FONT="$root/tests/fonts/DejaVuSans.ttf"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
python3 "$root/tests/settings_b/share-top.py" > "$tmp/share-top.inc"
cc -DSHARE_TOP_TEST=\"$tmp/share-top.inc\" -std=gnu11 -w -fsanitize=address,undefined -g -ffunction-sections -fdata-sections -Wl,--gc-sections \
 -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/ps1" -I"$root/core/deps/libretro-common/include" \
 -DPSX3D_TEST_PRESETS="\"$root/assets/romfs/presets\"" $(pkg-config --cflags freetype2) \
 "$root/tests/settings_b/model.c" "$root/systems/ps1/clip.c" "$root/app/ui/safefile.c" "$root/tools/input-replay/mock-core.c" \
 $(pkg-config --libs freetype2) -lz -lm -o "$tmp/check"
(cd "$tmp" && ./check) | tee "$tmp/model-results"
cc -std=gnu11 -w -fsanitize=address,undefined -g -ffunction-sections -fdata-sections -Wl,--gc-sections \
 -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/ps1" -I"$root/core/deps/libretro-common/include" \
 -DPSX3D_TEST_PRESETS="\"$root/tests/settings/fixtures/presets\"" -DPSX3D_DEFAULTS="\"$root/assets/romfs/presets/defaults.ini\"" $(pkg-config --cflags freetype2) \
 "$root/tests/settings_b/ps1.c" "$root/systems/ps1/clip.c" "$root/app/ui/safefile.c" "$root/tools/input-replay/mock-core.c" \
 $(pkg-config --libs freetype2) -lz -lm -o "$tmp/check"
(cd "$tmp" && ./check)
cc -std=gnu11 -w -fsanitize=address,undefined -g -ffunction-sections -fdata-sections -Wl,--gc-sections -DAPP_BREADBOX \
 -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/gba" -I"$root/core/deps/libretro-common/include" \
 "$root/tests/settings_b/gba.c" "$root/systems/gba/gba_settings.c" "$root/systems/gba/gba_color.c" "$root/systems/gba/gba_settings_ui.c" "$root/systems/gba/gba_touch.c" "$root/app/ui/ui_play_controls.c" -DUI_PLAY_HOST \
 "$root/systems/common/control_schemas.c" "$root/app/ui/bb_controls.c" "$root/app/ui/bb_profile.c" "$root/app/ui/bb_cheats.c" "$root/app/ui/safefile.c" -lm -o "$tmp/check"
(cd "$tmp" && ./check) > "$tmp/gba-results"
cat "$tmp/gba-results"
cc -std=gnu11 -w -fsanitize=address,undefined -g -ffunction-sections -fdata-sections -Wl,--gc-sections \
 -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/ps1" -I"$root/core/deps/libretro-common/include" \
 -DPSX3D_TEST_PRESETS="\"$root/tests/settings/fixtures/presets\"" -DPSX3D_DEFAULTS="\"$root/assets/romfs/presets/defaults.ini\"" $(pkg-config --cflags freetype2) \
 "$root/tests/settings_b/candidate-ledger.c" "$root/systems/ps1/clip.c" "$root/app/ui/safefile.c" "$root/tools/input-replay/mock-core.c" \
 $(pkg-config --libs freetype2) -lz -lm -o "$tmp/ledger"
(cd "$tmp" && ./ledger) > "$tmp/ps1-rows.tsv"
python3 "$root/tests/settings_b/fit.py" "$tmp/model-results"
python3 "$root/tests/settings_b/inventory.py" "$root/tests/settings_b/v1-r2-ps1-rows.tsv" "$tmp/ps1-rows.tsv" "$tmp/gba-results" "$tmp/model-results"
python3 "$root/tests/settings_b/ledger.py"
