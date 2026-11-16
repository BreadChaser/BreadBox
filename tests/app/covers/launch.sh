#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
python3 - "$tmp/launch-entry.inc" <<'PYCODE'
from pathlib import Path
import sys
main=Path('app/ui/main.c').read_text()
ps1=Path('systems/ps1/sys_ps1.c').read_text()
emu=Path('systems/ps1/emu.c').read_text()
Path(sys.argv[1]).write_text(main[main.index('static bool start_game('):main.index('static u64 boot_t0;')]+
 next(line for line in Path('systems/ps1/emu.h').read_text().splitlines() if line.startswith('extern void covers_stop_prepared('))+'\n'+
 emu[emu.index('bool emu_run_game('):emu.index('#include "components/emu.inc"')]+
 ps1[ps1.index('static ADAPTER bb_run_result run('):ps1.index('static ADAPTER bool state(')])
PYCODE
# Only platform ports are substituted; the drain, release and session are production files.
cat > "$tmp/ports.h" <<'HDR'
#include <stdbool.h>
#include "bb_system.h"
bool covers_stop_wait(void);
void covers_resume(void),bb_save_settle(void),bb_last_picture_release(void),bb_index_shutdown(void);
void video_gpu_idle(void),covers_release_gpu(void),music_unload(void),icons_release(void),shelf_release(void);
void room_scene_release(void),room_hands_release(void),room_scene_tv_maps_release(void);
bool home_release_cache(void);
void bb_mem_probe(const char *,const bb_system_t *);
#define PSX3D_COLD
HDR
for header in covers bb_save bb_index last_picture platform_video uisound icons shelf room bb_memprobe; do
 printf '#include "ports.h"\n' > "$tmp/$header.h"
done
cc -std=gnu11 -I"$tmp" -Itests/app/host/skip -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Wall -Wextra -Wno-unused-function -Wno-unused-variable \
 -g -fsanitize=address,undefined -c platform/3ds/bb_menu_release.c -o "$tmp/release.o"
cc -std=gnu11 -idirafter "$tmp" -Itests/app/host/skip -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Wall -Wextra -Wno-unused-function -Wno-unused-variable \
 -g -fsanitize=address,undefined tests/app/covers/launch.c app/ui/bb_session.c app/ui/bb_order.c "$tmp/release.o" \
 app/ui/covers_png.c app/ui/covers_pack.c app/ui/boxart.c app/ui/safefile.c \
 $(pkg-config --cflags --libs libpng libcurl) -lm -Wl,--wrap=curl_multi_poll -o "$tmp/check"
(cd "$tmp" && ./check)
