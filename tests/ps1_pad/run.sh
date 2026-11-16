#!/bin/bash
# The core's PS1 pad code on the host: ID/mode bytes a game reads (check.c).
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
flags=(-std=gnu11 -Wall -Wno-sign-compare -fsanitize=address,undefined -g
 -ffunction-sections -fdata-sections -DDRC_DISABLE -DPSX3D_PS1_OVERCLOCK_PRESETS -Itests/app/host
 -Iapp/ui -Iinclude/breadbox -Icore/libpcsxcore -Icore/include -Isystems/ps1
 -Icore/deps/libretro-common/include)
cc "${flags[@]}" -c systems/ps1/presets.c -o "$tmp/presets.o"
cc "${flags[@]}" -c core/libpcsxcore/pad.c -o "$tmp/pad.o"
cc "${flags[@]}" -c core/libpcsxcore/r3000a.c -o "$tmp/cpu.o"
cc "${flags[@]}" tests/ps1_pad/check.c tests/ps1_pad/reset.c "$tmp/pad.o" "$tmp/cpu.o" "$tmp/presets.o" \
 -Wl,--gc-sections -o "$tmp/check"
"$tmp/check"

# Omitting the post-reset notification must reproduce the lost mark.
cc "${flags[@]}" -DPS1_TEST_NO_RESET_NOTICE tests/ps1_pad/check.c tests/ps1_pad/reset.c "$tmp/pad.o" "$tmp/cpu.o" "$tmp/presets.o" \
 -Wl,--gc-sections -o "$tmp/unsettled"
python3 - "$tmp/unsettled" <<'PY'
import resource, subprocess, sys
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
result = subprocess.run([sys.argv[1]], capture_output=True, text=True)
assert result.returncode != 0, 'reset without settlement unexpectedly passed'
assert 'FAIL Reset game, rewind off: poll 17 read ID 73, want 41' in result.stderr, result.stderr
print('ps1-pad: unsettled deferred reset reproduces automatic analog enable')
PY
