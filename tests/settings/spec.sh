#!/usr/bin/env bash
# Spec-matches-menu test (docs/settings.md section 7): dump the real menu, then diff it.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -g -w -fsanitize=address,undefined -ffunction-sections -Wl,--gc-sections \
	-I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/core/deps/libretro-common/include" -I"$root/systems/ps1" \
	-DPSX3D_TEST_PRESETS="\"$root/tests/settings/fixtures/presets\"" -DPSX3D_PS1_OVERCLOCK_PRESETS \
	"$root/tests/settings/spec.c" "$root/tests/app/host/breadbox_stubs.c" "$root/systems/ps1/clip.c" "$root/app/ui/safefile.c" \
	"$root/tools/input-replay/mock-core.c" $(pkg-config --cflags --libs freetype2) -lz -lm -o "$tmp/spec"
(cd "$tmp" && ./spec "$root/tests/settings/fixtures/presets" "$root/assets/romfs/presets/defaults.ini") > "$tmp/menu.tsv"
cp "$tmp/menu.tsv" "${SPEC_DUMP:-/dev/null}" 2>/dev/null || true
python3 "$root/tools/settings/spec_check.py" "$tmp/menu.tsv" "${SPEC_DOC:-$root/docs/settings.md}" \
	"${SPEC_DEFAULTS:-$root/assets/romfs/presets/defaults.ini}"
