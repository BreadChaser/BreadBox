#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$(mktemp -d);trap 'rm -rf "$out"' EXIT
export BB_REPO_ROOT="$root"
export PSX3D_TITLE_FONT="$root/tests/app/host/Michroma-Regular.ttf" PSX3D_BODY_FONT="$root/tests/fonts/DejaVuSans.ttf"
cc -std=gnu11 -w -g -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
 -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/ps1" -I"$root/core/deps/libretro-common/include" \
 -DPSX3D_TEST_PRESETS="\"$root/assets/romfs/presets\"" $(pkg-config --cflags freetype2) \
 "$root/tests/look_equivalence/settings.c" "$root/systems/ps1/clip.c" "$root/app/ui/safefile.c" "$root/tools/input-replay/mock-core.c" \
 $(pkg-config --libs freetype2) -lz -lm -o "$out/settings"
cc -std=gnu11 -w -g -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
 -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/ps1" -I"$root/core/deps/libretro-common/include" \
 -DPSX3D_TEST_PRESETS="\"$root/assets/romfs/presets\"" $(pkg-config --cflags freetype2) \
 "$root/tests/look_equivalence/dialogs.c" "$root/systems/ps1/clip.c" "$root/app/ui/safefile.c" "$root/tools/input-replay/mock-core.c" \
 $(pkg-config --libs freetype2) -lz -lm -o "$out/dialogs"
python3 "$root/tests/looks/hit_geometry.py" > "$out/hits.inc"
cc -DSHELF_HIT_TEST=\"$out/hits.inc\" -std=gnu11 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=all \
 -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/gba" \
 -I"$root/core/deps/libretro-common/include" "$root/tests/shelf/shelf.c" "$root/app/ui/bb_order.c" -lm -o "$out/shelf"
python3 "$root/tests/look_equivalence/extract.py" app/ui/home.c room_sfx place_of tile_of room_move room_next room_tabs layout room_actions home_list_input room_input > "$out/home-controller.inc"
# Action arrays belong to the same production controller.
sed -n '/^static const ui_button_t room_at_tv\[/,/^static const ui_button_t \*room_actions/p' "$root/app/ui/home.c" | sed '$d' > "$out/home-actions.inc"
cat "$out/home-controller.inc" >> "$out/home-actions.inc";mv "$out/home-actions.inc" "$out/home-controller.inc"
cc -std=gnu11 -g -w -fsanitize=address,undefined -DBB_UI_TRACE_HOST -ffunction-sections -fdata-sections -Wl,--gc-sections \
 -I"$out" -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" \
 -I"$root/core/deps/libretro-common/include" "$root/tests/look_equivalence/home.c" "$root/app/ui/home_view.c" "$root/app/ui/room_bbr.c" -lm -o "$out/home"
sed '/^#include "components\/bb_pause.inc"/d' "$root/app/ui/bb_pause.c" > "$out/pause-controller.inc"
python3 "$root/tests/look_equivalence/extract.py" app/ui/components/postcards.inc pc_layout > "$out/postcards-controller.inc"
python3 "$root/tests/look_equivalence/extract.py" app/ui/postcards.c selectable neighbour pc_input >> "$out/postcards-controller.inc"
python3 "$root/tests/look_equivalence/extract.py" app/ui/ui_trace_views.c ui_trace_postcards > "$out/postcards-trace.inc"
python3 - "$root" "$out/gba-pause-rows.inc" <<'PYCODE'
from pathlib import Path
import sys
sys.path.insert(0,str(Path(sys.argv[1])/'tests/look_equivalence'))
from extract import function
src=(Path(sys.argv[1])/'systems/gba/sys_gba.c').read_text()
names=('solar_value','solar_change','solar_visible','ff_value','ff_change','rewind_value','rewind_visible','rewind_enabled','rewind_change')
text='\n'.join(function(src,name) for name in names)
a=src.index('static const bb_pause_row pause_rows[]');b=src.index('static bool continue_available',a)
Path(sys.argv[2]).write_text(text+'\n'+src[a:b])
PYCODE
cc -std=gnu11 -w -g -Wl,--wrap=time -DSAVE_WRITER_TEST -DBB_UI_TRACE_HOST -DBB_MENU_SCRIPT -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections -pthread \
 -I"$out" -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/ps1" -I"$root/systems/gba" -I"$root/tests/gba_saves" -I"$root/core/deps/libretro-common/include" \
 -DPSX3D_TEST_PRESETS="\"$root/assets/romfs/presets\"" $(pkg-config --cflags freetype2) \
 "$root/tests/look_equivalence/pause.c" "$root/app/ui/bb_save.c" "$root/app/ui/bb_final.c" "$root/app/ui/save_writer.c" "$root/app/ui/safefile.c" "$root/app/ui/history.c" "$root/systems/ps1/clip.c" "$root/tools/input-replay/mock-core.c" \
 $(pkg-config --libs freetype2) -lz -lm -o "$out/pause"
python3 "$root/tools/build/changelog.py" "$root/CHANGELOG.md" "$root/apps/breadbox/version.h" "$out/whatsnew_text.h"
python3 "$root/tests/look_equivalence/extract.py" app/services/update.c update_row update_bar bb_update_settings update_explicit_result > "$out/update-controller.inc"
cc -std=gnu11 -w -g -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
 -I"$out" -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/ps1" -I"$root/core/deps/libretro-common/include" \
 -DPSX3D_TEST_PRESETS="\"$root/assets/romfs/presets\"" $(pkg-config --cflags freetype2) \
 "$root/tests/look_equivalence/cards.c" "$root/systems/ps1/clip.c" "$root/app/ui/safefile.c" "$root/tools/input-replay/mock-core.c" \
 $(pkg-config --libs freetype2) -lz -lm -o "$out/cards"
cat "$root/app/ui/home_welcome.inc" > "$out/welcome-controller.inc"
cc -std=gnu11 -w -g -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
 -I"$out" -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/ps1" -I"$root/core/deps/libretro-common/include" \
 -DPSX3D_TEST_PRESETS="\"$root/assets/romfs/presets\"" $(pkg-config --cflags freetype2) \
 "$root/tests/look_equivalence/welcome.c" "$root/systems/ps1/clip.c" "$root/app/ui/safefile.c" "$root/app/ui/room_bbr.c" "$root/tools/input-replay/mock-core.c" \
 $(pkg-config --libs freetype2) -lz -lm -o "$out/welcome"
python3 - "$root" "$out/share-controller.inc" <<'PYCODE'
from pathlib import Path
import sys
sys.path.insert(0,str(Path(sys.argv[1])/'tests/look_equivalence'))
from extract import function
src=(Path(sys.argv[1])/'app/services/share.c').read_text()
start=src.index('static uint8_t qr[');end=src.index('// Runs until B')
Path(sys.argv[2]).write_text(src[start:end]+function(src,'share_about_run'))
PYCODE
cc -std=gnu11 -w -g -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
 -I"$out" -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/ps1" -I"$root/core/deps/libretro-common/include" \
 -DPSX3D_TEST_PRESETS="\"$root/assets/romfs/presets\"" $(pkg-config --cflags freetype2) \
 "$root/tests/look_equivalence/share.c" "$root/systems/ps1/clip.c" "$root/app/ui/safefile.c" "$root/app/ui/qrcode.c" "$root/tools/input-replay/mock-core.c" \
 $(pkg-config --libs freetype2) -lz -lm -o "$out/share"
python3 - "$root" "$out/ps1-menu.inc" <<'PYCODE'
from pathlib import Path
import sys
sys.path.insert(0,str(Path(sys.argv[1])/'tests/look_equivalence'))
from extract import function
src=(Path(sys.argv[1])/'systems/ps1/emu.c').read_text()
a=src.index('enum { M_RESUME');b=src.index('static void menu_draw_top',a)
text=src[a:b]
a=src.index('static const unsigned char menu_icon');b=src.index('// One card row',a)
text+=src[a:b]
for name in ('menu_label','menu_value','menu_card_row','menu_bar','menu_build','menu_scan','menu_note','menu_run'):
 text+=function(src,name)+'\n'
shared=(Path(sys.argv[1])/'app/ui/bb_pause.c').read_text()
a=shared.index('const ui_button_t bb_pause_bar');b=shared.index('#define pause_bar',a)
text=shared[a:b]+text
for start,end in (('static const char *const kind_name','static bool system_row_shown'),):
 a=shared.index(start);b=shared.index(end,a);text=shared[a:b]+text
text+='#define SAVED_SECONDS 3\n'
for name in ('bb_pause_slots_saved','bb_pause_row_value','bb_pause_hotkey'):text+=function(shared,name)+'\n'
Path(sys.argv[2]).write_text(text)
PYCODE
cc -std=gnu11 -w -g -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
 -I"$out" -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/ps1" -I"$root/core/deps/libretro-common/include" \
 -DPSX3D_TEST_PRESETS="\"$root/assets/romfs/presets\"" $(pkg-config --cflags freetype2) \
 "$root/tests/look_equivalence/ps1_pause.c" "$root/systems/ps1/clip.c" "$root/app/ui/safefile.c" "$root/tools/input-replay/mock-core.c" \
 $(pkg-config --libs freetype2) -lz -lm -o "$out/ps1_pause"
python3 - "$root" "$out/features-controller.inc" <<'PYCODE'
from pathlib import Path
import sys
src=(Path(sys.argv[1])/'app/ui/bb_feature_ui.c').read_text()
a=src.index('bb_controls bb_active_controls');b=src.index('const char *bb_cheats_keyboard',a)
Path(sys.argv[2]).write_text(src[a:b])
PYCODE
cc -std=gnu11 -w -g -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
 -I"$out" -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/ps1" -I"$root/core/deps/libretro-common/include" \
 -DPSX3D_TEST_PRESETS="\"$root/assets/romfs/presets\"" $(pkg-config --cflags freetype2) \
 "$root/tests/look_equivalence/features.c" "$root/app/ui/bb_controls.c" "$root/app/ui/bb_profile.c" "$root/systems/ps1/clip.c" "$root/app/ui/safefile.c" "$root/tools/input-replay/mock-core.c" \
 $(pkg-config --libs freetype2) -lz -lm -o "$out/features"
cc -std=gnu11 -w -g -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
 -I"$out" -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/ps1" -I"$root/core/deps/libretro-common/include" \
 -DPSX3D_TEST_PRESETS="\"$root/assets/romfs/presets\"" $(pkg-config --cflags freetype2) \
 "$root/tests/look_equivalence/browser.c" "$root/app/ui/folder_peek.c" "$root/systems/ps1/clip.c" "$root/app/ui/safefile.c" "$root/tools/input-replay/mock-core.c" \
 $(pkg-config --libs freetype2) -lz -lm -o "$out/browser"
python3 "$root/tests/look_equivalence/extract.py" app/ui/list_look.c list_scroll > "$out/list-scroll.inc"
cc -std=gnu11 -w -g -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
 -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/ps1" -I"$root/core/deps/libretro-common/include" \
 -DPSX3D_TEST_PRESETS="\"$root/tests/settings/fixtures/presets\"" -DPSX3D_DEFAULTS="\"$root/assets/romfs/presets/defaults.ini\"" $(pkg-config --cflags freetype2) \
 -I"$out" "$root/tests/look_equivalence/settings_systems.c" "$root/tests/look_equivalence/gba_adapter.c" "$root/systems/gba/gba_settings.c" "$root/systems/gba/gba_color.c" "$root/systems/gba/gba_settings_ui.c" "$root/systems/gba/gba_touch.c" "$root/systems/common/control_schemas.c" "$root/app/ui/bb_controls.c" "$root/app/ui/bb_profile.c" "$root/app/ui/bb_cheats.c" "$root/systems/ps1/clip.c" "$root/app/ui/safefile.c" "$root/tools/input-replay/mock-core.c" \
 $(pkg-config --libs freetype2) -lz -lm -I"$root/systems/gba" -o "$out/settings_systems"
python3 - "$root" "$out" <<'PYCODE'
from pathlib import Path
import sys
sys.path.insert(0,str(Path(sys.argv[1])/'tests/look_equivalence'))
from extract import function
src=(Path(sys.argv[1])/'systems/ps1/savestates.c').read_text()
a=src.index('#define SLOTS');b=src.index('#define accent',a)
Path(sys.argv[2],'ps1-slots-types.inc').write_text(src[a:b])
a=src.index('static pc_view slot_view');b=src.index('static const char *state_label',a)
text='#define STATES_BAR_MAX UI_BAR_MAX\n'+src[a:b]
for name in ('slot_path','slot_delete','states_bar','lift_update','grid_input','pick_plays','states_run'):text+=function(src,name)+'\n'
Path(sys.argv[2],'ps1-slots-controller.inc').write_text(text)
PYCODE
cc -std=gnu11 -w -g -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
 -I"$out" -I"$root/tests/app/host" -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/ps1" -I"$root/core/deps/libretro-common/include" \
 -DPSX3D_TEST_PRESETS="\"$root/assets/romfs/presets\"" $(pkg-config --cflags freetype2) \
 "$root/tests/look_equivalence/ps1_slots.c" "$root/systems/ps1/clip.c" "$root/app/ui/safefile.c" "$root/app/ui/history.c" "$root/tools/input-replay/mock-core.c" \
 $(pkg-config --libs freetype2) -lz -lm -o "$out/ps1_slots"
for look in day psx3d simple;do
 mkdir "$out/slots-ps1-$look" "$out/settings-ps1-$look" "$out/browser-$look" "$out/features-$look" "$out/ps1-$look" "$out/$look" "$out/save-$look" "$out/cards-$look" "$out/welcome-$look" "$out/share-$look"
 (cd "$out/slots-ps1-$look" && "$out/ps1_slots" "$look") > "$out/ps1_slots-$look.trace"
 (cd "$out/settings-ps1-$look" && "$out/settings_systems" "$look") > "$out/settings_systems-$look.trace"
 (cd "$out/browser-$look" && "$out/browser" "$look") > "$out/browser-$look.trace"
 (cd "$out/features-$look" && "$out/features" "$look") > "$out/features-$look.trace"
 (cd "$out/ps1-$look" && "$out/ps1_pause" "$look") > "$out/ps1_pause-$look.trace"
 (cd "$out/share-$look" && "$out/share" "$look") > "$out/share-$look.trace"
 (cd "$out/welcome-$look" && "$out/welcome" "$look") > "$out/welcome-$look.trace"
 (cd "$out/cards-$look" && "$out/cards" "$look") > "$out/cards-$look.trace"
 (cd "$out/save-$look" && "$out/pause" "$look") > "$out/pause-$look.trace"
 "$out/dialogs" "$look" > "$out/dialogs-$look.trace"
 "$out/home" "$look" > "$out/home-$look.trace"
 (cd "$out/$look" && "$out/settings" "$look") > "$out/settings-$look.trace"
 BB_EQ_TRACE=1 "$out/shelf" "$look" | sed -n '/^TRACE /p' > "$out/shelf-$look.trace"
done
python3 "$root/tests/look_equivalence/compare.py" "$out"
python3 "$root/tests/look_equivalence/bar_words.py" "$out"
if [[ -n ${BB_EQ_OUT:-} ]];then mkdir -p "$BB_EQ_OUT";cp "$out/"*.trace "$BB_EQ_OUT/";fi
