#!/usr/bin/env bash
# Save slot postcards: the service reads (check.c) and the board's layout and
# d-pad/touch reachability (layout.py), both from the production sources.
set -euo pipefail
cd "$(dirname "$0")/../.."
out=$(mktemp -d);trap 'rm -rf "$out"' EXIT
cc -std=gnu11 -g -Wall -Wextra -Werror -Wno-misleading-indentation \
 -DSAVE_WRITER_TEST -fsanitize=address,undefined -pthread \
 -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Itests/gba_saves tests/save_slots/check.c \
 app/ui/bb_save.c app/ui/bb_final.c app/ui/save_writer.c app/ui/safefile.c app/ui/history.c -lz -o "$out/check"
mkdir "$out/run";"$out/check" "$out/run"
python3 tests/save_slots/layout.py
