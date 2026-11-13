#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
 -I "$repo/app/ui" -I "$repo/include/breadbox" -I "$repo/platform/3ds" "$repo/tests/widescreen/check.c" "$repo/systems/ps1/widescreen.c" -o "$work/check"
(cd "$work" && ./check)
for file in "$repo"/assets/romfs/widescreen/*.ws; do
 [[ -e "$file" ]] || continue
 name=$(basename "$file" .ws)
 "$work/check" "$repo/assets/romfs/widescreen" "${name%-*}" "${name#*-}"
done
