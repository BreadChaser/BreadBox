#!/usr/bin/env bash
# Twelve save slots: the number <-> file mapping (bb_slots.h) and the save service's use of it.
set -euo pipefail
cd "$(dirname "$0")/../.."
out=$(mktemp -d);trap 'rm -rf "$out"' EXIT
cc -std=gnu11 -g -Wall -Wextra -Werror -Wno-misleading-indentation \
 -DSAVE_WRITER_TEST -fsanitize=address,undefined -pthread \
 -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Itests/gba_saves tests/slot_map/check.c \
 app/ui/bb_save.c app/ui/bb_final.c app/ui/save_writer.c app/ui/safefile.c app/ui/history.c -lz -o "$out/check"
mkdir "$out/run";"$out/check" "$out/run"
