# BreadBox code map

BreadBox is the 3DS product (PS1, GBA, experimental NES), built with devkitARM,
libctru, citro2d and citro3d. Main stays on `trunk`; work in a branch/worktree.
PSX3D is retired: `APP=psx3d` is rejected. Its core hooks, `PSX3D_*` paths and
one-time import from `sdmc:/3ds/PSX3D` remain live BreadBox compatibility code.

## Where things live

- `app/ui/`: `main.c` lifecycle, room/library, shared `components/*.inc` and
  `ui.c` drawing; `components/value_card.inc` pages long focused values.
  `bb_save.c` owns battery/RTC sessions; `bb_final.c` owns
  immutable final files through retry/discard, including PS1 cards and Continue
  (one writer; per path the newest wins).
  `safefile.c`, `save_writer.c`, `history.c` provide atomic files and backups.
  `bb_pause.c` is the shared pause card and Continue question; `folder_peek.c`
  the folder picker's shallow look for games in the 3DS's own root folders.
  `welcome.c` is the first-launch welcome's flag (`welcome` in config.ini) and
  words; `home_welcome.inc` tours the room with it (`bb_welcome_open()` for
  Settings > Help; dev `ui welcome`). `place_cards.c`: the room cards' lines.
- `app/settings/`: the Settings pages (one list per page, a page stack; adapters own the rows);
  `app/ui/ui_mark.c` the selection highlighter (`ui_mark.h`, masks from `tools/ui/mark_masks.py`);
  `app/ui/ui_font.c` where the loaded fonts' letters stand (`UI_PAPER_TOP` puts text on ruled lines with any font; test `tests/text_baseline`);
  `app/services/`: updater and sharing.
- `platform/3ds/`: device boundaries, video, audio, input and networking.
- `systems/<id>/sys_<id>.c`: system descriptors behind `bb_system.h`.
  `systems/common/retro_host.c` owns libretro callbacks/core validity;
  `retro_ctr.c` is the shared runner. PS1 runs in `systems/ps1/emu.c`,
  `systems/ps1/ps1_messages.c` routes routine BIOS/game-fix notices to the log,
  `card_question.c` asks about an unreadable memory card before a game runs;
  GBA runs in `systems/gba/gba.c`. `core/` and `gba-core/` are vendored engines.
- `include/breadbox/`: system contracts, shared style tokens and one header per
  shell module (`app_config.h`, `platform_video.h`, `ui.h`, etc.); `ui_words.h`
  holds the words every system says alike (postcards), defines only.
  `bb_registry.h` declares the registry; `tools/build/systems-table.py` generates
  its table from `SYSTEMS` in `apps/breadbox.mk`.
- `app/ui/room_*.c`: the room main menu (`room_bbr.c` portable BBR v3 tables,
  styles, camera and library case; `room_scene.c` GPU draw and final dither pass;
  `room_walls.c`/`room_board.c` the poster and corkboard). Themes are room styles
  (Day, Night: `theme.c`); the room file is `assets/romfs/room/room.bbr`.
- `assets/`: ROMFS; `apps/breadbox/`: product metadata; `build/3ds/`: Makefile,
  linker scripts and link order. `tools/`: build, layout, architecture, cleanup;
  `tools/room/lamplight/` builds the room (Blender: `scene.py` geometry,
  `gi.py` Cycles light per vertex, `soft.py` the soft texture finish
  (test: `tests/room/soft-textures.py`, needs NumPy), `lights.json`/`refine.json` data;
  `docs/assets/room.md`).
- `tests/`: host suites (`tests/ps1_messages/check.py` covers both libretro
  message callbacks and the production notice router); `docs/`: documentation.

## Build and test

Set up devkitPro as `docs/building.md` says (`DEVKITPRO`, `DEVKITARM`). When the host SDK
differs from the pin, build in the pinned container: `env -u TMPDIR PSX3D_CONTAINER=bwrap tools/build/container.sh make -j3`.
Do not bypass or change the SDK pins.
CIA: `make -j3 cia VERSION=<private counter> MAKEROM=<makerom-fixed>`;
explicit development fixtures use `cia-development`.
Leave `BB_EXPERIMENTAL_NES` unset for default/release builds.

```sh
bash tools/build/host-tests.sh
bash tests/save_service/run.sh
bash tests/app/save/run.sh
bash tests/update/run.sh
python3 tests/main_lifecycle/check.py
bash tools/layout/check.sh
bash tools/layout/baseline-check.sh
python3 tools/architecture/ownership-guard.py --strict
```

Old 3DS code placement affects performance: `build/3ds/core-first.ld` pins the
core and frame functions. Keep both layout gates passing; accept intentional
changes per function in `tools/layout/accepted-changes.json`, never by moving
the baseline. Mark cold menus/saves `PSX3D_COLD` (`cold_code.h`). Speed claims
require Old hardware. Shared code holds no system policy; adapters own it.
UI uses shared components/tokens and American English.
See `docs/building.md`, `docs/dev/save-service.md`, `docs/auto-update.md`.
Keep evidence outside the checkout; no push, console contact or attribution
unless the task explicitly authorizes it.
