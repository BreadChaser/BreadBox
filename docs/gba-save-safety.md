# Game Boy Advance saves

## Files and slots

Each cartridge gets a save identity, `gbs-<game code>-<sample CRC32>` (see
below), resolved at launch by `gba_saveid_resolve()`. The battery file is
`saves/gba/<identity>.sav` and postcards are `states/gba/<identity>.stateN`.
Files named after the ROM (`<ROM filename>.sav`) from earlier versions are only
read once, to adopt them into the new identity.

Battery files are 128 KiB, enough for SRAM, EEPROM and flash. Any other size is
asked about before the game runs (Restore backup / Start empty; the file is kept
as `.bad`); nothing is converted on its own. When the size is one another
emulator writes for the cartridge's chip (512 B or 8 KiB EEPROM, 32 KiB SRAM,
64 KiB flash; any of them when gpSP does not know the chip yet), the question
also offers "Use it": the original goes to Backups byte for byte first, then the
game gets it padded to 128 KiB. Changed battery RAM is queued every 180 frames
and written on pause, sleep or suspend, HOME Close and exit. A write failure is
shown on the bottom screen. Use an in-game save before quitting, as on a GBA.

Postcards 1–10 are N = 0–9 and postcards 11 and 12 are N = 13 and 14 (10, 11
and 12 are Continue, Undo and the quick state; `bb_slots.h`). Each holds a
versioned BreadBox header, gpSP's state and battery RAM. ROM header identity,
ROM size and a payload CRC are checked before gpSP parses the state, and a
damaged state falls back to a valid backup. These states belong to this core
version and are not RetroArch states. Loading a state keeps the committed
battery file (below); the battery bytes inside the state are not restored.

## Ownership and life cycle

GBA battery files are authoritative. Loading any state (Continue, manual or
Undo) first commits pending in-game battery changes through safefile, then
restores gameplay and puts the committed battery bytes back, even if the core
reports a partially failed restore. The embedded state battery is retained in
the format for compatibility, but is never implicitly restored. Before each
changed battery replaces the current file, its previous committed bytes must
be written to the existing history.c ring (HIST_SAVE, .sav). Failure to back up
or commit aborts the state load. The ten-entry ring shares the existing global
history budget. GBA history files are retained on SD; the PS1 backup browser
currently does not offer GBA restores.

Continue is written at HOME suspend, lid sleep and exit, plus teardown even
without the GPU right. Neither serialization nor the state file writer calls
the GPU. Stalled cores still skip serialization at teardown; their battery is
flushed. SD/memory failures can still prevent a save and are logged/reported.

New cartridges use `gbs-<game code>-<sample CRC32>` for battery, states and
settings. The fingerprint covers size and five evenly spaced 4 KiB blocks
(including the header and tail), independent of path and ZIP cache name.
This is a practical fingerprint, not a guarantee against differences outside
sampled blocks or adversarial CRC collisions. Same-basename cartridges whose
sampled contents differ get independent saves; raw/ZIP and renamed copies agree.
ZIP preparation still verifies its full member CRC before use.

Library enumeration reads only headers. Both BBIDX004 and BBIDX005 caches are
accepted without rereading cartridges; the old v5-only gate caused the expensive
rebuild regression. New scans record zero in the obsolete CRC field. Library
favorites/history use path-scoped metadata keys without ROM reads, adopting old
code/known full-CRC keys; they do not own battery/state data. Metadata for a moved
ROM may need re-favoriting, while content-based saves/settings follow the ROM.

Existing `gba-<code>-<full CRC32>` save/state/RTC/settings files retain their exact
owner. If old data for that code exists and no sampled owner exists, launch
computes the actual full CRC once using 256 KiB reads, then stores a checksummed
40-byte record bound to size and samples in cache/gba-id. Later launches validate
only those samples. No stale index/path/header can adopt another ROM's old data.
This exceptional first launch can exceed two seconds; ordinary new launches
perform only bounded sample reads. Existing sampled data wins over subsequently
added legacy cartridges sharing its code. Deleting a full-CRC cache record is
safe: an owner is reverified rather than guessed.

The ZIP extraction cache retains only the latest selected archive. The same
archive reuses its verified extraction; switching archives removes previous
extractions and metadata. Save identity is derived from prepared contents,
independent of extraction filename and cache eviction.

On first launch, `<old basename>.id` claims the legacy basename for this
identity, following PS1 saveid's first-adopter policy. No owner can be inferred
from a raw battery, so ambiguous historical files belong to that first
cartridge; the others get fresh namespaces. Adoption is a copy, including
.bak/.tmp candidates, never deletion of the original. Existing destination
files win. Each atomic copy can be retried after interruption. A completed owner claim
prevents retained legacy copies from resurrecting intentionally deleted slots. Legacy states
still undergo their existing header CRC/size/payload validation before loading.
Per-path settings are copied to the identity filename on first access. Legacy
code-key favorites/history are adopted once with a durable marker; un-starring
then stays unstarred. The old log/favorite entries remain available for recovery.

## Sleep is single-writer in the pinned SDK

In **libctru 2.7.0** (pinned and ABI-checked by tools/build/toolchain.lock), the
sleep hooks run on the main thread, not on APT's event thread. Checked against:
https://github.com/devkitPro/libctru/blob/v2.7.0/libctru/source/services/apt.c
(SHA256 `2f5378fdc0f23b96f74b46e1df95d34467384fb46c7ded3d5d4314a19ae7c3ff`).

* aptEventHandler (line 469) receives SLEEP_ENTER and sets FLAG_SHOULDSLEEP
  (line 586). It does not dispatch ONSLEEP/ONWAKEUP hooks.
* aptMainLoop (line 851) calls aptHandleSleep; that invokes ONSLEEP (line 841),
  acknowledges sleep, waits for wake, and invokes ONWAKEUP (line 848).
* aptJumpToHomeMenu invokes ONSUSPEND (line 821), likewise on its caller.
* ONEXIT is called by aptExit, not by the event worker.

The app's APT/menu loops and core execution are all on the main thread. Thus
GBA's hook, state operations and teardown remain main-thread barriers.
Periodic saves now copy RAM/RTC to the shared snapshot queue; its sole worker
writes history and battery files without calling gpSP. The main thread drains
both active and pending jobs before suspend, sleep, exit, state load and game
switch. Handing sleep off to a later frame would delay the save until waking.
This contract must be rechecked on SDK upgrades or moving core/APT work to
another thread. Do not invoke aptMainLoop or save routines from workers.

The PS1 hook has the same dispatch ownership. Its sleep_save first calls
autosave_wait (joining the periodic state-file writer), then cards_save_now
and save_take/save_put. HOME suspend also joins first. The PS1 core does not
run concurrently with these calls; no PS1 behavior change is needed.

## Host coverage

`python3 tests/gba_saves/run.py` executes the production GBA battery/state
function bodies against a fake core with real safefile/history/saveid/settings
sources: close/relaunch/Continue, old slot and failed restore battery authority,
repeated flushes, history retention, same-basename adoption and moved settings.
The playlog test compiles its production source and checks legacy adoption and
persistent unstar behavior. The sleep test compiles verbatim aptHandleSleep /
aptMainLoop from the pinned SDK with a concurrent notification producer and
asserts all hooks stay on the main thread outside core execution (100k frames).
The fixture carries upstream provenance; it must track any SDK upgrade.
Existing GBA lifecycle tests check actual suspend/close call sites and GPU guards.
These tests do not execute gpSP or certify hardware APT/SD timing.

## Asynchronous battery persistence

`save_writer.c` is a bounded single-producer/single-consumer save service. One
immutable active snapshot and one replaceable pending snapshot prevent concurrent
writers; rapid requests replace the pending snapshot. A flush drains both jobs,
and a failed commit remains failed until a successful retry. Allocation/thread
failure retains the synchronous safe path. Old uses core 1's existing syscore
share; New uses core 2. No CPU budget or core execution policy changes.

Only the GBA adapter samples live RAM and RTC, on the main thread under its arena
loan. The worker owns durable CRC/RTC metadata until a barrier. It preserves the
previous committed version in the history ring before the unchanged safefile
replace protocol. Uncommitted coalesced versions never enter history. Quiet
history insertion uses identical retention/budget logic and avoids touching the
main log stream. Remote file mutation remains library-only, after worker stop.

The state-load barrier completes before gpSP restore, including failed restore;
the authoritative file is read back before mutating the core. Continue is still
serialized on the main thread after the battery barrier during sleep/suspend.
Teardown flushes and joins before releasing history ownership or the arena.
Normal gameplay does not wait for file completion. An unexpected power loss
before a queued snapshot commits can retain the previous durable save; it cannot
publish a partial final file. Lifecycle barriers require the latest snapshot.

The host save suite additionally executes the real queue with blocked writes,
199 coalesced requests, immediate producer-buffer mutation, failed-write retry,
500 wake/drain races and 16 injected crash points in asynchronous history/replace.
