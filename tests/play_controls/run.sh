#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
play_tmp=$(mktemp -d)
trap 'rm -rf "$play_tmp"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DUI_PLAY_HOST \
 -Iapp/ui -Iinclude/breadbox -Isystems/gba \
 tests/play_controls/check.c tests/play_controls/storage_stub.c app/ui/ui_play_controls.c systems/gba/gba_touch.c systems/common/control_schemas.c -lm -o "$play_tmp/check"
"$play_tmp/check"

python3 tests/play_controls/frame_loop.py

python3 tests/play_controls/migration.py

python3 tests/play_controls/notice.py
