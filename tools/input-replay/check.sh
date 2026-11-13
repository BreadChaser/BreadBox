#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
build=${BUILD_DIR:-${TMPDIR:-/tmp}/breadbox-input-replay-host-tests}
mkdir -p "$build"
cc -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$repo/app/ui" -I "$repo/include/breadbox" -I "$repo/platform/3ds" -I "$repo/core/deps/libretro-common/include" \
  "$repo/tools/input-replay/codec-test.c" "$repo/tools/input-replay/mock-core.c" \
  "$repo/systems/ps1/clip.c" -lz -o "$build/codec-test"
(cd "$(mktemp -d "$build/run-XXXXXX")" && "$build/codec-test")
