#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Itests/app/host -Icore/deps/libretro-common/include -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/library_letters/check.c -o "$tmp/check"
"$tmp/check"
