#!/usr/bin/env bash
# display_fit (systems/ps1/display.c) on the host, with ASan/UBSan.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp -d "${TMPDIR:-/tmp}/psx3d-display.XXXXXX")
trap 'rm -rf "$out"' EXIT
cc -std=c99 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all -g \
	-I"$here/../../../app/ui" -I"$here/../../../include/breadbox" -I"$here/../../../platform/3ds" "$here/check.c" "$here/../../../systems/ps1/display.c" -lm -o "$out/check"
"$out/check"
