# Adding a system to BreadBox

This is the checklist for a new emulated system (NES, Game Boy, ...). Everything the system owns lives in
`systems/<id>/`. The only edit outside that folder is one word in `apps/breadbox.mk`.

## 1. Start the folder

```sh
python3 tools/new-system.py gb "Game Boy" GB --ext gb,gbc,zip --hint "game boy" --core systems/gb/core
```

This creates:

| File | What it is |
|---|---|
| `systems/<id>/sys_<id>.c` | The descriptor `const bb_system_t bb_system_<id>` (`include/breadbox/bb_system.h`). Every hook you still have to write is marked TODO. |
| `systems/<id>/core.mk` | With `--core`: how the vendored libretro core builds and links. |

Then add the id to `SYSTEMS` in `apps/breadbox.mk`. The registry (`systems_table.c`),
the arena overlay in the linker profile, the core blob and its include paths and
libraries all follow from that list. No shared file names your system.

## 2. The core (`core.mk`)

Vendor the core under `systems/<id>/core/`, retaining its license and provenance, and fill in:

| Key | Meaning |
|---|---|
| `<id>_CORE_DIR` | core folder, relative to the repository root |
| `<id>_CORE_MAKE` | arguments for its libretro Makefile (`platform=ctr ...`) |
| `<id>_CORE_ARCHIVE` | the static library that build produces |
| `<id>_CORE_PREFIX` | prefix for every global symbol (keeps it apart from the PS1 core) |
| `<id>_CORE_JIT_HEADER`, `_JIT_DEFINES`, `_JIT_EXPR` | optional: translation-cache bytes, from the core's own preprocessor |
| `<id>_CORE_INCLUDES`, `<id>_CORE_LIBS` | headers and libraries the system's frontend files use |
| `<id>_CORE_PREPARE` | optional command run at repository root before building the blob |
| `<id>_CORE_LIBDIRS` | optional repository-relative SDK/library roots (each has `lib/`); precede installed SDK libraries |
| `<id>_CORE_LICENSE` | must be compatible with GPL-2.0-or-later |

`tools/build/core-blob.py <id>` (run by `make`) builds it into
`build/systems/<id>/<id>_core_blob.o`. All of its sections become `.sys.<id>.*`. The
BSS goes into the `.sys.exec.<id>` overlay of the shared executable arena, which
PS1's dynarec leaves idle while another system runs. A system without `core.mk`
reserves no cache.

## 3. The descriptor

Fill in these, in roughly this order:

1. **Library**: `extensions`, `folder_hints`, `probe_bytes`, `probe()`, `identify()`.
   - The shared index (`bb_index.c`) scans and routes files by probe score.
   - `scan_priority` lets cheap cartridge checks run before slow disc traversal.
   - `identify()` confirms the game at launch and gives the save key.
2. **Covers**: `cover_db` (the libretro-thumbnails folder). Add `cover_item()` and
   `cover_candidates()` only when the shared index entry is not enough.
   **Shelf box** (optional): `package` gives the box the game stands in on the
   bookshelf of the room themes (`shelf.c`): cover width, height and spine depth in
   mm, `BB_PACKAGE_CASE` (clear case, hinge) or `BB_PACKAGE_CARTON`, the spine-top
   and side-band colors and an optional band label. NULL is a plain carton; the
   shelf, the spines and the card on the desk need nothing else from a system.
3. **Settings**: a `bb_setting_desc` table. The Settings scope (`bb_settings_run`,
   `app/settings/`) draws it. Values still come from your
   `setting_value` / `setting_change` hooks; no casts, write a small wrapper.
4. **Memory**: `budget[]` per console, **measured** on the Old and New 3DS. The worst
   game is measured after the menus release their buffers. `keep_kib` must be 0.
5. **Session**: use `systems/common/retro_host.h` and `retro_ctr.h`.
   `core-blob.py` generates a guarded `bb_retro_<id>` entry table. Supply a
   `bb_retro_desc` (core, variable defaults, joypad map, maximum geometry,
   optional exact FPS ratio, scale and filter); call `bb_retro_ctr_run`.
   See the experimental `systems/nes/sys_nes.c` adapter. The shared host owns:
   - libretro callbacks, presentation, input;
   - RAM battery/state operations and shell-owned crash-safe files;
   - frame pacing/frameskip and device audio/video callbacks.
   Declare `.save=&bb_retro_ctr_save_ops` and optional `pause_rows`. The host
   connects the shell save/pause services, Continue and APT barriers.
   Keep its save rules: crash-safe writes through `safefile.c`, history backups,
   and a state never rolling back a newer battery save.
6. **Dev remote**: `dev` hooks (`state`, `key`, `option`, `status`, `sample`). The
   remote already understands `load TITLE system=<id>`. Remote slot and 3D arrive
   in `bb_launch.slot` / `bb_launch.stereo`; never read shell globals.

Rules the build enforces:

- System files include `bb_system.h`, never `bb_registry.h` (the shell's side).
  Shell files never include a header from `systems/<id>/`
  (`tests/system_conformance/check.py`).
- No new system name in the shell (`app/`, `platform/3ds`, `include/breadbox`): `tools/architecture/shell-guard.py --strict`
  fails. Features every system needs go into the shell.
- Shared code owns no system policy: `tools/architecture/ownership-guard.py --strict`
  fails on private system or core headers, `game_t`, PCSX setting keys and PS1
  BIOS/card/disc policy in the shell (`app/`, `platform/3ds`, `include/breadbox`) and `systems/common`. A new shared file
  gets no exception; the legacy references are frozen line by line, with reasons,
  in `tools/architecture/ownership-exceptions.json` and the list may only shrink
  (a removed reference must leave the list in the same change). Words in a
  user-facing prose string (credits, help: four or more words) are not policy.
- Screens draw through the shared UI components. `tests/ui_consistency/check.py`
  also scans `systems/*/`.
- New files link after the measured frontend (`build/3ds/link-order.txt`). Do not add
  them to that list unless a hardware measurement says where they belong.

## 4. Gates before a console sees it

1. **PS1 core unchanged.** Adding a system must not move the PS1 core.
   - `tools/layout/baseline-check.sh` compares the new ELF with the qualified
     public BreadBox reference (`tools/layout/baseline.json`): core regions and core code
     words, the size and instructions of every pinned frame function, and the
     system frame pins. CI runs it.
2. **BreadBox layout.** Run `tools/layout/check-pinned.py` and
   `tools/layout/check.sh`.
   - Statically linked cores shift the PS1 data pages. That needs one PS1 Old A/B/A
     until cores load into the arena at run time.
3. **Host suites and behavioral conformance.** `bash tools/build/host-tests.sh` runs all of them, including
   `system-conformance`, with 0 skips. Add a host test for your probe and identify,
   using small synthetic headers in the style of `tests/gba_cart`.
   Run `bash tests/system_conformance/run.sh <id>` with generated or clearly licensed
   content. This must execute the real core, shared host and save service: cold-boot
   pixel/PCM determinism, non-silent audio, state replay, battery disk reload,
   Continue across teardown/reopen, schema/pause checks and ASan/UBSan/LSan.
   Add a driver in `tests/system_conformance/runtime.py`; unknown systems fail closed.
   See [coverage and limits](../../tests/system_conformance/README.md).
4. **Exact output.** Run your core's frames under qemu-arm, the way
   `tests/gba_arm_exact` does for gpSP. Pixels, PCM and state must be exact across
   a fresh and a reused arena.
5. **Emulator smoke.** Silent Azahar, Old and New models
   (the private headless emulator runner): boot, library, load, quit, library.
6. **Hardware.** New 3DS first, then the Old 3DS:
   - 10 load/quit cycles;
   - 20 switches with PS1 and GBA;
   - the memory log against `budget[]`.

## Known costs and limits

- **Saves, states, Continue, pause menu, HOME and sleep.** Shared by
  `bb_save.c` / `bb_pause.c`; declare RAM-only `bb_save_ops` and extension rows.
  See [the save service contract](save-service.md).
- **Video, input and the libretro frontend.** Shared by `systems/common/retro_host.c`;
  system-specific presentation (layer stereo or GPU compositor) is an optional extension.
- **Settings storage.** Your hooks store the values; the shell will own storage
  once the schema reader lands.

See [known debt](system-layer.md#known-debt) for remaining shared architecture work.

## Keeping frame code stable

A system can supply `systems/<id>/frame-text.ld`: measured text selectors and
4 KiB page-offset pins. `system-linker.py` inserts these blocks after PS1's
protected frame block and before freely growing menus. The PS1 core uses its original
profile directly. GBA pins its surviving adapter sections, shared host, generated bindings, indexed
compositor and namespaced core block; cold helpers are protected too. An extraction
may intentionally change function sizes; record those changes and recheck exact output.

List the frontend object basenames in `frame-pins.txt`; the matching core blob
is included automatically by `tools/layout/check-system-pinned.py`. The check
requires every surviving baseline function to retain its size and page offset,
and fails if one disappears. `tools/layout/check.sh` runs it on
both menu-growth and added-library variants. It protects instruction-cache
layout, not hardware speed: intentional core/frame changes still need their
normal exactness and hardware gates. Refresh pins only for an intentional
baseline change, and prove the old/new offsets before committing.

See [the shared host contract](libretro-host.md) for lifecycle and extension details.

## Experimental shared-host proof

`make BB_EXPERIMENTAL_NES=1` includes QuickNES; the default target
still contains PS1 and GBA only. Core source, provenance/licenses, descriptor,
probe/identity and the CC0 test-cartridge generator all live in `systems/nes/`.
`bash tests/retro_host/quicknes.sh` runs the real core through the shared host,
checking replay, state and battery round trips. `retro_host` tests cover pitched
and unaligned RGB565/XRGB8888/0RGB1555 conversion and rational pacing.
The shared runner supplies the pause menu, Continue, Undo and backups.
The conservative NES memory declaration is not a physical-console measurement.
Do not enable NES in a shipping target before that measurement.

System extensions may supply environment values, special video composition,
frame hooks, preparation/release, battery generation and opt-in RTC blobs.
Only the active arena owner can call the generated core entries. GBA's direct
extension entry guards are generated from `systems/gba/retro-guards.json` by
`tools/build/retro-legacy-guards.py`; `--check` rejects stale checked-in output.

A core with measured cache layout may supply `CORE_OBJECT_ORDER` (an exact
archive-member inventory). This avoids incremental archive append order moving
frame code on a clean build. Duplicate/missing members fail closed.

## Final new-system acceptance

The checklist ends with **conformance passes**: `bash tests/system_conformance/run.sh <id>`.
A structural-only result or the explicit PS1 partial coverage is not acceptance
for a newly added system. Hardware budgets, GPU/device behavior and ARM JIT
exactness remain separate gates.
