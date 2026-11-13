#!/bin/sh
# Host check of the bookshelf controller (room themes) under ASan/UBSan.
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$here/../..
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=all \
	-I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/gba" \
	-I"$root/core/deps/libretro-common/include" "$here/shelf.c" "$root/app/ui/bb_order.c" -lm -o "$tmp/shelf"
"$tmp/shelf"
cc -std=gnu11 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
	-I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" "$here/order.c" "$root/app/ui/bb_order.c" -o "$tmp/order"
"$tmp/order"

python3 "$here/cells.py"

python3 "$(dirname "$0")/grain.py"
python3 "$here/path-tail.py"

python3 "$here/../room/selection.py"
