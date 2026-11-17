#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
# The generated core table is compiled unchanged; its common.h only supplies u16.
printf '#include <stdint.h>\ntypedef uint16_t u16;\n' > "$tmp/common.h"
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
 -I"$root/include/breadbox" -I"$root/app/ui" -I"$root/platform/3ds" -I"$root/systems/gba" \
 -include "$tmp/common.h" -DCOMMON_H \
 "$root/tests/gba_color/check.c" "$root/systems/gba/gba_color.c" "$root/systems/gba/gba_settings.c" \
 "$root/systems/gba/gba_saveid.c" "$root/systems/gba/gba_cart.c" "$root/app/ui/safefile.c" \
 "$root/gba-core/gba_cc_lut.c" -lminizip -lz -lm -o "$tmp/check"
(cd "$tmp" && ./check)
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
 -I"$root/tests/gba_color/host" -I"$root/systems/gba" -I"$root/include/breadbox" \
 -I"$root/core/deps/libretro-common/include" -include "$tmp/common.h" -DCOMMON_H \
 "$root/tests/gba_color/presentation.c" "$root/systems/gba/gba_picture.c" \
 "$root/systems/gba/gba_color.c" "$root/gba-core/gba_cc_lut.c" -lm -o "$tmp/presentation"
(cd "$tmp" && ./presentation)
