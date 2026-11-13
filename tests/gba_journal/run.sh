#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
bin=$(mktemp /tmp/gba-journal.XXXXXX)
trap 'rm -f "$bin"' EXIT
cc -DINLINE=inline -DFRONTEND_SUPPORTS_RGB565 -O1 -g -fsanitize=address,undefined -I gba-core -I gba-core/libretro/libretro-common/include tests/gba_journal/check.c -o "$bin"
"$bin"
