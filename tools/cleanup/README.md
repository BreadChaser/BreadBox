# Structure cleanup scripts

Mechanical, behavior-neutral cleanups. Each script runs from the repository root,
changes only what its rules name, prints what it could not apply, and is a no-op
when run a second time. Together they took BreadBox from the two-app layout
(BreadBox and the retired PSX3D) to one app.

The cleanup branch ran them in this order, one commit each, then added a few plain commits:

| Step | Command | What it does |
|---|---|---|
| 1 | `python3 tools/cleanup/unifdef_app.py` | Removes the always-true `APP_BREADBOX` guards: keeps the BreadBox side, drops the `#else`/`#ifndef` (PSX3D) side, rewrites mixed conditions (`defined(APP_BREADBOX) && X` becomes `X`). Also deletes the PSX3D fallbacks that sat after a guarded `return` (`DEAD_TAILS`), the `-DAPP_BREADBOX`/`#define APP_BREADBOX` lines of test builds and the test code that cut source text at the guards (`TEST_FIXUPS`). |
| 2 | `python3 tools/cleanup/split_psx3d_h.py` | Splits `include/breadbox/psx3d.h` into one header per module (`psx3d-header-map.txt`), points every includer at the headers whose names it uses, updates the tools that read the header (ownership exceptions, shell baseline, UI surface hashes, tests) and removes `psx3d.h`. |
| 3 | `python3 tools/cleanup/rename_ui_metrics.py` | Renames `UI_METRIC_<n>` tokens to names from `ui-metric-names.tsv`. A new name gets the old token's exact value in `ui_style.h`; tokens with no use left are removed. |

## Replaying on another branch

`replay.sh` puts the whole cleanup on top of a branch as new commits; it never replaces a file
the branch already has:

```sh
git switch <target>                     # a clean tree
bash <(git show cleanup/structure:tools/cleanup/replay.sh) cleanup/structure
```

1. It copies `tools/cleanup/` (a new folder) from the cleanup branch.
2. For each step it runs the script, which rewrites only what its rules name, then applies
   `patches/step<N>.patch`: the edits a script cannot make (test harness and build-line
   adaptations, see below), as hunks against the script's result. `git apply --check` runs
   first; when the branch changed those lines it falls back to `git apply --3way`, and a
   conflict stops the replay with the markers in place. Each step is one commit.
3. It cherry-picks the plain commits in `patches/series` (3-way; conflicts stop it).
4. It fails when a host suite the branch registered in `tools/build/host-tests.sh` is no
   longer registered.
5. It compares every Python test assertion supplied by the cleanup ref (AST expressions,
   including `and` terms and duplicate counts), and fails if any are lost. The only anchor
   adaptation accepted is the reviewed libretro `bb_save_runnable()` loop.

After manual conflict resolution, run the same replay script with
`--check-assertions <cleanup-ref>` before the final gates. This also catches context
assertions accidentally discarded while keeping a newer test's extraction logic.

Then build, compare the executable with the branch's own (below) and run the gates.

The patches are `git diff <script result> <cleanup commit>` for each step, made by running the
current scripts on the commit before it; regenerate them the same way if a script changes.

## When a script reports something

- `unifdef_app.py` handles any new `APP_BREADBOX` conditional. A changed test or
  function named in `TEST_FIXUPS`/`DEAD_TAILS` is reported; fix it by hand.
- `split_psx3d_h.py` stops before writing anything when `psx3d.h` holds a declaration
  no rule in `psx3d-header-map.txt` matches, and lists them: add a rule (an exact
  `=name` or a pattern) under the right header and run it again.
- `rename_ui_metrics.py` reports rows whose token or scope no longer exists.
- `--check` (unifdef, rename) and `--dry-run` (split) show what would change.

## Proving no behavior change

Build before and after with the same stamp (`SOURCE_DATE_EPOCH` fixed, `git
describe` disabled, `BB_EXPERIMENTAL_NES` unset) and compare the ELF's loadable
sections byte for byte; then run `tools/layout/check.sh`,
`tools/layout/baseline-check.sh` and `make host-tests`.

## What the patches carry

Step 1 (`patches/step1.patch`, guards):
- `apps/breadbox.mk`, `build/3ds/Makefile`: `APP_DEFS := -DAPP_BREADBOX` and its use removed.
  The `APP=psx3d` refusal stays (user-facing, checked by `tests/app_targets`).
- Host suites that used to build the PSX3D side of shared code now build the BreadBox side:
  - `tests/app/host/breadbox_stubs.c` (new) linked by `tests/app/host/run.sh`,
    `tests/settings/run.py` and `tests/settings/spec.sh`;
  - `tests/app/host/check.c`: the game-palette loop skips the themes with their own palette
    (rooms, Light, Dark, Original) and pins Original for the mockups; PSX3D's confirm-timing
    check is gone (BreadBox dialogs reveal through `ui_swap_*`, covered by `tests/room`);
  - `tests/settings/navigation.c` and `docs/settings.md`: the theme list is BreadBox's;
  - `tests/renderer_config.c`, `tests/app/dev/check.c`, `tests/bb_index/registry.c`: stubs
    for the BreadBox registry/session, `ps1_3d_available`, `input_hotkey_cycle` and the CHD probe;
    `tests/gba/remote.py` brings its own registry (`DEV_CHECK_OWN_REGISTRY`);
  - `tests/app/dev/client_test.py`: the first invalid slot is 12; `tests/app/dev/run.sh`
    builds the update test once;
  - `tests/app/covers/worker.py`: one build (the PSX3D variant run is gone);
  - `tests/main_lifecycle/check.py`, `tests/system_iface/continue.py`,
    `tests/room/libui-publication.py`, `tests/share_room/check.py`: source slices no longer
    end at `#endif`;
  - `tests/app/save/history.c`: adds the ring bound outside a game session, the path the
    PSX3D build used to exercise;
  - removed: `tests/app/library` (its folder-scanner cases are PSX3D-only; commit 61ec32f3
    in `patches/series` brings back the cases for the functions BreadBox runs) and the
    synchronous music-reload check in `tests/gba/hw1.py` (BreadBox reloads music asynchronously).

Step 2 (`patches/step2.patch`, header split):
- Six host tests used `#define PSX3D_H` to skip the real header and bring their own
  declarations; they now put `tests/app/host/skip` (empty stand-ins the script writes) first
  on their include path instead: `tests/app/covers/{sdk.h,rebuild.sh,worker.py}`,
  `tests/app/dev/{check.c,transport.c,update.c,run.sh}`, `tests/arena/{sdk.c,run.sh}`,
  `tests/statsview/{check.c,mockup.py}`, `tests/gba/remote.py` and the statsview line of
  `tools/build/host-tests.sh`. `tests/statsview/check.c` also defines `u8`/`u16` for `theme.h`.
- `tests/renderer_config.sh`: its generated fake `3ds.h`/`citro2d.h` get `#pragma once`
  (several headers include them now).
- `tests/themes/run.py`: also drops `<3ds.h>`, `<citro2d.h>` and `"libretro.h"` from the bodies
  it compiles against its own fakes (`psx3d.h` used to bring them).

Two `assert()` calls (`app/settings/settings_ui.c`, `app/ui/shelf_scene.c`) sit below the
replaced include, so the line number in their failure message changes; that is the only
difference in the executable (two literal words, same sizes and addresses).
