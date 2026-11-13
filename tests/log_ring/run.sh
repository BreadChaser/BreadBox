#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
out=$(mktemp -d);trap 'rm -rf "$out"' EXIT
cc -std=gnu11 -g -Wall -Wextra -Werror -DLOG_RING_TEST -fsanitize=address,undefined -pthread \
 -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/log_ring/check.c app/ui/log_ring.c -o "$out/check"
"$out/check"
# the same with the thread sanitizer: no data race between the writers and the reader
cc -std=gnu11 -g -O1 -Wall -Wextra -Werror -DLOG_RING_TEST -fsanitize=thread -pthread \
 -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/log_ring/check.c app/ui/log_ring.c -o "$out/check-tsan"
"$out/check-tsan"
