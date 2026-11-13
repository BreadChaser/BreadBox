# Game Boy Advance in BreadBox

`make` builds the shared PS1 frontend plus the vendored gpSP core. Installable
packages use `make cia` with the fixed makerom described in `docs/building.md`.
BreadBox has its own title ID, `000400000B0B0B00`, and SD root,
`sdmc:/3ds/BreadBox`. It does not modify PSX3D's settings or saves (the retired
PSX3D app, frozen at v0.5.2).

GBA runs gpSP with its own CPU renderer and shows the picture as a texture; it
does not use the PS1 core or the PS1 GPU renderer. The GBA GPU compositor
experiments are shelved.

## Playing

Choose Collection, then Game Boy Advance. Put uncompressed `.gba` files under the
configured games folder; subfolders are scanned. No BIOS file is required.
An optional user-provided BIOS goes at `3ds/BreadBox/bios/gba_bios.bin`.
The bundled replacement BIOS and its source are in `gba-core/bios/`.

A/B, L/R, Start/Select and the D-pad map directly. The circle pad also acts as
the D-pad. X or touching the bottom screen opens the pause menu. Choose Quit game
to save battery RAM and return to your collection. The bottom screen
stays black during play after a short pause-control hint, except for save/load notices.

GBA settings are separate from PS1: Original shows 240×160 centered; Fit shows
360×240; Stretch fills 400×240. Sharp/Smooth controls texture filtering. Color correction and interframe blending default off; Fit defaults
on. Pacing follows the exact GBA clock (16,777,216 clocks/s, 280,896 clocks/frame),
independently of screen refresh. Fractional timer ticks carry between frames,
so rounding does not accumulate drift. A late frame can skip its picture; CPU,
audio and saves still run, and at least every third picture is shown. A long
stall bounds catch-up, and waking resets the deadline. Actual speed depends on
the console and game. Different GBA/LCD refresh rates still require occasional
repeated display frames; this does not change game speed.

Fast-forward and rewind work like PS1 and use the same hotkeys (set once for all
systems; default SELECT+R toggles fast-forward, SELECT+L held rewinds; the GBA
settings show them as "Source: All systems"). Fast-forward runs up to three more
frames per shown frame without sound (at most 4x); the Old 3DS gives what its CPU
allows. Rewind is New 3DS only, like PS1: Rewind (on by default, per system and
per game) keeps a capture every 10 frames as compressed differences in a history
of up to 16 MB, compared on a worker on the spare core. Stepping back never rolls
back the battery save or the RTC clock; loading a state or resetting the game
starts a new history. ▶▶ / ◀◀ show at the top right while active.

There is no GBA 3D setting: 3D follows the 3D slider. Sunlight appears only for
games with a solar sensor (Boktai 1-3, detected by game code U3I/U32/U33).

## Saves

Battery files are `saves/gba/<ROM filename without .gba>.sav`, 128 KiB to cover
SRAM, EEPROM and flash. Any other size is asked about before the game runs
(Restore backup / Start empty; the file is kept as `.bad`); nothing is converted
on its own. When the size is one another emulator writes for the cartridge's
chip (512 B or 8 KiB EEPROM, 32 KiB SRAM, 64 KiB flash; any of them when gpSP
does not know the chip yet), the question also offers "Use it": the original goes
to Backups byte for byte first, then the game gets it padded to 128 KiB. Keep ROM filenames unique when using different game
versions. Battery RAM is saved when changed every 180 frames, on pause,
sleep/suspend, HOME Close, and exit. Writes use the existing temporary/backup/repair
protocol; a write failure is shown on the bottom screen. Use an in-game save
before quitting, as on a GBA.

Pause-menu state slots 1–12 live at `states/gba/<basename>.stateN`: slots 1–10 are N = 0–9, slots 11 and 12 are N = 13 and 14 (10, 11 and 12 are Continue, Undo and the quick state; `bb_slots.h`). Each contains
a versioned BreadBox header, gpSP's state, and battery RAM. ROM header identity,
ROM size and a payload CRC are checked before gpSP parses the state. A damaged
current state falls back to a valid backup. These states are specific to this
core version and are not raw RetroArch states. Loading a state also restores
its battery RAM; queued audio is discarded.

## Build isolation and memory

`gba-core/README.breadbox.md` pins the upstream source and local changes.
`systems/gba/core.mk` describes the core build; `tools/build/core-blob.py gba`
partially links the CTR archive, prefixes all defined global symbols with
`gpsp_`, and renames allocated core sections to `.sys.gba.*`.
The generated `build/3ds/core-first-breadbox.ld` preserves PS1 core layout, library
addresses and protected frontend cache offsets, then places GBA additions.
`breadbox-library-text.ld` records the original M0 library input sections.
Regenerate and verify that manifest deliberately when upgrading the toolchain
or PS1 dependencies; it is not a generic library-ordering policy.

The GBA ROM cache is bounded to 4 MiB; large ROMs page from the SD card. Only one
system runs at a time. GBA core/audio/texture/JIT allocations are released on
return to the library. On Luma, the CTR dynarec uses a 2 MiB ROM code cache and
384 KiB RAM code cache mapped below `0x02000000`. Cache synchronization uses
Luma's documented SVCs 0x92 and 0x94, with no privileged callback. Allocation
or mapping failure selects the interpreter. Hardware performance and that
mapping path require console acceptance; Azahar normally uses the interpreter.
The session log names the engine actually selected and reports JIT mapping
addresses or failure codes, rather than inferring the engine from SVC access.

The startup-only test switches `gba=<path>`, `gba_frames=N`, and `gba_test=1`
autostart a ROM, stop after N frames, and exercise slot 9 at frames 120/180.
Use `gba_test=2` for PCM counters without intrusive state operations. With either
test mode (or `stats`), one-second reports include emulated speed, skipped/shown
pictures, core/presentation time and audio queue/underrun/drop counts. They also
log PCM counts/hash and memory around sessions. `switches.txt` is
whitespace-tokenized, so test aliases should have no spaces. These switches
are for private validation, not player configuration.

BreadBox remote updates require an explicit package size and SHA256, the
BreadBox title ID, and a TMD version strictly greater than the installed title.
