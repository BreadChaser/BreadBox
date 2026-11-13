#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
bb_history_tmp=$(mktemp -d)
trap 'rm -rf "$bb_history_tmp"' EXIT
cc -std=gnu11 -g -Wall -Wextra -Werror -DHIST_BUDGET=12000 -DSAFEFILE_TEST -fsanitize=address,undefined -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/app/save/history.c app/ui/history.c app/ui/safefile.c -o "$bb_history_tmp/check"
"$bb_history_tmp/check"
