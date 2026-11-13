#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
out=$(mktemp -d);trap 'rm -rf "$out"' EXIT
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -Itests/sd_bench/host -Iapp/ui -Iinclude/breadbox \
 tests/sd_bench/check.c platform/3ds/sd_bench.c -o "$out/check"
(cd "$out" && ./check)
