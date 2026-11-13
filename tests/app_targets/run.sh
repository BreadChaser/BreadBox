#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I"$repo/app/ui" -I"$repo/include/breadbox" -I"$repo/platform/3ds" "$repo/tests/app_targets/check.c" \
  "$repo/app/ui/history.c" "$repo/app/ui/safefile.c" -o "$work/check"
(cd "$work" && ./check)
# BreadBox is the only product: no PSX3D app target, metadata or APP switch may return.
if [[ -e $repo/apps/psx3d.mk || -e $repo/psx3d/meta ]]; then
  echo 'PSX3D app target files must stay removed' >&2; exit 1
fi
if make -s -C "$repo" -n APP=psx3d all >/dev/null 2>&1; then
  echo 'APP=psx3d must be rejected' >&2; exit 1
fi
# A future hard-coded app root must fail the shared host gate.
if grep -nE '"(sdmc:)?/3ds/PSX3D' "$repo"/app/*/*.c "$repo"/platform/3ds/*.c; then
  echo 'App roots belong in app_policy.h' >&2; exit 1
fi
