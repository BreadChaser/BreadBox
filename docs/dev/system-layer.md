# System layer

`include/breadbox/bb_system.h` is the contract and the source of truth; its
`BB_SYSTEM_ABI` is **13**, and `tests/docs_current/check.py` fails if this page
names another number. BreadBox is the only app (PSX3D is frozen at v0.5.2 and no
longer built). New systems follow [the new-system checklist](new-system.md).

## Current contract (ABI 13)

A system is one `const bb_system_t` descriptor registered through
`systems/<id>/sys_<id>.c`:

- identity and model: `id`, `name`, `short_name`, `models`, `stereo`, `budget[]`;
- library: `extensions`, `folder_hints`, `probe`, `group`, `identify`,
  cover hooks (`cover_item`, `cover_candidates`, `cover_db`), and optional
  `poll_library(refresh)` for staged adapter publication;
- settings and presets: `settings`/`nsettings` schema, `preset_dir`, value hooks; ABI 13 adds the optional typed
  `settings_model` for scoped rows, provenance, actions and resets;
- lifecycle: `run` owns the frame loop, `continue_available`;
- saves: `save` (`bb_save_ops`: RAM battery, state and RTC through the shared save
  service) or NULL when the system keeps its own save code (PS1 today);
- pause: `pause_rows` extend the shared pause menu (a row can be hidden or
  disabled by callbacks and carries its card icon, tone and left/right arrows);
  the Undo and quick-save slots are `BB_SLOT_UNDO` / `BB_SLOT_QUICK`;
- cheats and controls: `cheats` (`bb_cheat_ops`: count, label, value, toggle, add)
  feeds the shared cheat manager; `controls` and `control_shoulders` say which
  shared remaps apply; `setting_source`, `setting_clear` and `settings_open`
  serve the labeled app / system / game settings pages;
- display and shelf: `release_year`/`release_month` order the systems newest
  first, `logo_url` is the maker's mark fetched once to the SD card, and
  `picture_box` places the game's picture on the top screen;
- optional: `dev` (remote developer ops), `mp`, `package` (shelf look), the
  executable-arena overlay (`arena_start`/`arena_end`) and `tick`.

PS1 still fills `shell` (`bb_shell_ops`) with legacy library, settings and
Continue callbacks, including `continue_capture` for an owned `bb_path_snapshot`
used by the asynchronous Continue worker; that bridge is transitional (see the [new-system checklist](new-system.md)). The
shared index, probes, system-qualified favorites, covers and the save service are
generic. Content identity keys are `<system>:<key>` and are stable on disk.

## Known debt

- Step 3: typed content identity and fully system-owned settings still need a
  follow-up refactor; the current PS1 bridge is transitional.
- Large UI and session files remain unsplit, including `shelf_scene.c`,
  `emu.c::run_game`, `covers.c`, and `main()`.
- `UI_METRIC_<n>` names still encode visual roles by number and should become
  semantic names.
- The developer remote is included in the build but ships disabled by default. Only `sdmc:/3ds/BreadBox/switches.txt` enables it; see [developer controls](dev-control.md).

## Shared runtime behavior

The scanner stores generic descriptor, entry, and path records and routes files by
scored content probes, refusing ties. Legacy PS1 scanning and grouping stays in
its system module. Covers use registry buckets and database namespaces. Favorites
and metadata use system-qualified identities. New adapters can use the default
library and settings pages while retaining system-specific hooks.

Arena overlays loan PS1 core memory to one system at a time. Bounds come from the
built symbols, and calls reject access after a loan ends. Battery snapshots use
CRC checks and safe history writes; state loading validates payloads before
entering the core and preserves durable battery data. Session launch releases
menu resources before budget checks and restores them on failure.

## Hardware validation

Layout and host checks do not establish console performance, SD timing, suspend
latency, or pixel parity. PS1 and GBA still require their system-specific Old/New
console checks before a layout or runtime change is accepted.
