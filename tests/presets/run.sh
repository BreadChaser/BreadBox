#!/usr/bin/env bash
# Preset data: file checks, then identical-to-v0.4.1 and version-precedence host tests.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 "$repo/tools/presets/check.py"
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -include stddef.h \
 -I "$repo/tests/app/host" -I "$repo/app/ui" -I "$repo/include/breadbox" -I "$repo/platform/3ds" -I "$repo/tests/presets" \
 -I "$repo/core/deps/libretro-common/include" \
 "$repo/tests/presets/identical.c" "$repo/systems/ps1/presets.c" "$repo/systems/ps1/widescreen.c" -o "$work/identical"
"$work/identical" "$repo/assets/romfs/presets" "$repo/assets/romfs/widescreen" "$repo/tests/presets/v041-ws-models.txt" "$work"

python3 "$repo/tests/presets/validation.py" "$work/identical"
