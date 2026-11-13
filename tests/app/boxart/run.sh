#!/bin/bash
# Host test of box art matching and the memory of missing pictures
# (app/ui/boxart.c, run for real on the shipped name table), with
# AddressSanitizer and UndefinedBehaviorSanitizer.
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I"$repo_root/tests/app/host" -I"$repo_root/app/ui" -I"$repo_root/include/breadbox" -I"$repo_root/platform/3ds" \
  "$repo_root/tests/app/boxart/check.c" "$repo_root/app/ui/boxart.c" \
  "$repo_root/app/ui/safefile.c" -o "$tmp/check"
cd "$tmp"
ASAN_OPTIONS=detect_leaks=1 "$tmp/check" "$repo_root/assets/romfs/covers" "$@"
