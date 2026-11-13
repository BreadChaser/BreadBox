#!/usr/bin/env bash
# Text stands on the ruled lines with any font (check.c), and ruled text is placed only through the shared tokens.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined -g \
 -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" \
 -I"$root/core/deps/libretro-common/include" \
 "$root/tests/text_baseline/check.c" -lm -o "$tmp/check"
"$tmp/check" "$root/assets/romfs/font/title.bcfnt"
# One routine: the fonts' baselines come from ui_font.c, and no drawing code uses a baseline number of its own.
cd "$root"
grep -q '^#define UI_FONT_BASELINE ui_face_baseline(UI_FACE_SYSTEM)$' include/breadbox/ui_style.h
grep -q '^#define UI_TITLE_FONT_BASELINE ui_face_baseline(UI_FACE_TITLE)$' include/breadbox/ui_style.h
if grep -rnE 'UI_(TITLE_)?FONT_BASELINE' app systems platform --include=*.c --include=*.inc --include=*.h; then
	echo 'FAIL: ruled text goes through UI_PAPER_TOP / UI_PAPER_TITLE_TOP / UI_TEXT_CENTRED_TOP, not the baseline itself'; exit 1
fi
echo 'text_baseline sources PASS: ruled text placed only through the shared tokens'
