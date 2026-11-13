#!/bin/bash
# Host test of the PS1 library functions BreadBox runs (systems/ps1/library.c),
# with AddressSanitizer + LeakSanitizer.
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation -fsanitize=address,undefined -g -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -I"$repo_root/tests/app/host" -I"$repo_root/core/deps/libretro-common/include" \
  -I"$repo_root/app/ui" -I"$repo_root/include/breadbox" -I"$repo_root/platform/3ds" "$repo_root/tests/app/library/check.c" \
  "$repo_root/app/ui/safefile.c" -o "$tmp/check"
ASAN_OPTIONS=detect_leaks=1 "$tmp/check"
