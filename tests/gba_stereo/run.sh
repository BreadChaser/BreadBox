#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
bin=$(mktemp "${TMPDIR:-/tmp}/gba-stereo.XXXXXX")
trap 'rm -f "$bin"' EXIT
c++ -DINLINE=inline -DFRONTEND_SUPPORTS_RGB565 -O1 -g -fsanitize=address,undefined -Wno-unused-parameter -I gba-core -I gba-core/libretro/libretro-common/include tests/gba_stereo/check.cc -o "$bin"
"$bin"
