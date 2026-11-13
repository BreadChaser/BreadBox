# Shared libretro host

`systems/common/retro_host.h` separates core calls and RAM operations from
3DS devices and session file policy. One host may be active because libretro
callbacks carry no user pointer. The system runner claims the executable arena
before binding and releases it after unbinding.

The descriptor supplies a generated `bb_retro_core`, geometry limits, joypad
mapping, option values and directories. `bb_retro_ext` supplies optional
special video, environment and frame hooks, preparation/release, generation
tracking and RTC blobs. RTC is never enabled or persisted implicitly.

`bb_retro_open` installs callbacks, initializes/loads the core and validates AV
rates. `step` runs CPU/audio on every frame; `pace` advances an exact rational
clock, caps consecutive skipped pictures at two and bounds catch-up after a
stall. `close` unloads/deinitializes before removing callbacks. A failed open
unwinds through the same close path.

The generated core table guards every entry by arena ownership. C++ static
constructor/destructor arrays are isolated with each core and run on bind/unbind,
so clearing arena BSS does not erase objects constructed only at process startup.
Core data remains resident; cores must reset mutable initialized data in their
normal load/deinit cycle. Adding a C++ core requires a repeated-load smoke.

The RAM helpers expose SAVE_RAM, serialization and opt-in RTC. State restore can
preserve newer authoritative battery RAM even after a partial restore, and
reacquires the core's backing RAM in case the restore changed its allocation.
They do not choose paths, timestamps, Continue slots or history policy.

`retro_ctr.c` connects audio.c, the pitched/unaligned RGB565, XRGB8888 and
0RGB1555 texture converter, Original/Fit/Stretch and texture filtering. Its
experimental runner has minimal safe battery/state files and APT flushing.
It has no generic pause or Continue shell; the save service in plan B replaces
that temporary file policy.

GBA uses shared standard callbacks, input/audio accounting, frame execution,
RAM adapters. Its fixed-rate rational clock keeps the original compact storage
and constant carry code for ARM11. It retains its established outer loop for
stereo admission, diagnostics, GPU compositor and save/pause/Continue/APT policy.
The descriptor's optional `video_refresh` is a direct adapter-owned callback for
bind/run_frame migrations. It preserves the adapter's NULL, upload and watchdog
policy and avoids a second dispatch/tick read. `open` rejects this mode and
`step`/`pace` do no work in it: the adapter must own timing and presentation.
The compositor callbacks and indexed/journal interfaces are unchanged. Its
remaining direct extension guards are generated from `retro-guards.json`.

QuickNES is the optional third-system proof (`BB_EXPERIMENTAL_NES=1`). See
`systems/nes/PROVENANCE.md` and `systems/nes/tests/ROMS.md`. Default BreadBox
continues to register only PS1 and GBA. Its memory declarations are conservative
smoke budgets, awaiting physical measurement and a complete pause/save shell.
