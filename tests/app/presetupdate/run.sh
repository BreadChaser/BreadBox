#!/bin/bash
# Host test of the automatic preset update: manifest and version rules,
# damaged downloads, validation failure, a cut-off install and its recovery,
# and the once-a-day timing. Uses the real preset parser and tools/presets/pack.py.
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DPRESETUPDATE_TEST -include stddef.h \
  -I"$repo_root/tests/app/host" -I"$repo_root/tests/app/dev" -I"$repo_root/app/ui" -I"$repo_root/include/breadbox" -I"$repo_root/platform/3ds" \
  -I"$repo_root/core/deps/libretro-common/include" \
  "$repo_root/tests/app/presetupdate/check.c" "$repo_root/systems/ps1/presetupdate.c" \
  "$repo_root/systems/ps1/presets.c" "$repo_root/app/ui/safefile.c" -lcrypto -o "$tmp/check"
REPO="$repo_root" "$tmp/check" "$repo_root/assets/romfs/presets" "$tmp/server" "$tmp/sd"
python3 "$repo_root/tests/app/presetupdate/mutations.py"
