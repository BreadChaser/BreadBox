#!/usr/bin/env bash
# System switches leave the heap whole: the production small-block pool under
# the BreadBox link flags, and the source contracts of the switch fix.
set -euo pipefail
cd "$(dirname "$0")/../.."
out=$(mktemp -d);trap 'rm -rf "$out"' EXIT
cc -std=gnu11 -g -Wall -Wextra -Werror -fsanitize=address,undefined -pthread \
 -Itests/system_switch/host -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/system_switch/smallheap.c app/ui/bb_smallheap.c \
 -Wl,--wrap=malloc,--wrap=free,--wrap=calloc,--wrap=realloc -o "$out/smallheap"
"$out/smallheap"
python3 tests/system_switch/contract.py
