#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
root=$PWD
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
python3 systems/nes/tests/make-rom.py "$out/fixture.nes"
# Reviewed source list and local build rules only (no downloaded scripts).
make -s -C "$out" -f "$root/systems/nes/core/Makefile" CORE_DIR="$root/systems/nes/core" \
 CXX=c++ AR=ar 'FLAGS=-O2 -DNDEBUG -D__LIBRETRO__ -fno-rtti -fno-exceptions -DGIT_VERSION=\"26bb785\"' -j4 all
python3 tools/build/retro-bindings.py nes '' "$out/bindings.c"
# A normal host process already ran these constructors at startup.
python3 - "$out/bindings.c" <<'PYCODE'
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text();a=s.index('extern void (*__bb_nes_init_start');b=s.index('const bb_retro_core',a);s=s[:a]+s[b:];s=s.replace('.activate=activate,.deactivate=deactivate,','');p.write_text(s)
PYCODE
cc -std=c11 -O2 -g -Isystems/common -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Icore/deps/libretro-common/include \
 -c "$out/bindings.c" -o "$out/bindings.o"
cc -std=c11 -O2 -g -Isystems/common -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Icore/deps/libretro-common/include \
 -c tests/retro_host/quicknes.c -o "$out/test.o"
cc -std=c11 -O2 -g -Isystems/common -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Icore/deps/libretro-common/include \
 -c systems/common/retro_host.c -o "$out/host.o"
c++ -O2 -DNDEBUG -D__LIBRETRO__ -fno-rtti -fno-exceptions -DGIT_VERSION='"26bb785"' \
 -Isystems/nes/core -Isystems/nes/core/nes_emu -Isystems/nes/core/libretro \
 -Isystems/nes/core/libretro/libretro-common/include -c tests/retro_host/nes_cheats.cpp -o "$out/cheats.o"
c++ "$out/cheats.o" "$out/test.o" "$out/host.o" "$out/bindings.o" "$out/quicknes_libretro_ctr.a" -lm -o "$out/check"
"$out/check" "$out/fixture.nes"
