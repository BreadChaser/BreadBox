# Shell saves and pause menu

`bb_save.c` owns one session's battery files, asynchronous snapshots, history,
validated state files, Continue (slot 10), Undo (slot 11), timestamp sidecars and
backup recovery. `bb_pause.c` owns the pause menu, slot picker, confirmations,
Continue prompt and the suspend/restore barriers. These are main-thread services;
only copied immutable battery/RTC bytes reach `save_writer.c`.
`bb_final.c` owns final file bytes, paths and timestamps until written or explicitly
discarded. Queued, written and failed are distinct outcomes; a queued Continue is
not advertised as durable. The shell checks all retained failures before launch/exit.
`bb_final_write` is one write at a time whoever calls it, and every Continue point
(pause, HOME/sleep, end, Start over) goes through it. Each ticket gets a generation:
an older ticket of a path is skipped once a newer one is written, and an older failed
one is let go then (its bytes are kept until that moment); Retry writes the newest
first. A point's metadata is a fixed slot per ticket, so taking one at a pause needs
no allocation beyond its state bytes and writes nothing before the card shows.
Start over moves the point and its backup to history; only "not there" (ENOENT)
counts as absent, any other SD error keeps both, holds the point and does not start over.

A libretro system declares `.save=&bb_retro_ctr_save_ops` and optional
`.pause_rows` / `.npause_rows` on its `bb_system_t`, then uses
`bb_retro_ctr_run`. The scaffold emits the save declaration. No file writer,
slot browser, Continue hook, APT handler or pause controller is copied. Optional
RTC blobs come from the shared host extension. Settings rows use the existing
schema/change hooks; settings storage remains a separate service.

An adapter with a specialized frame loop supplies RAM-only `bb_save_ops`:
`state_size`, `state_save`, `state_load`, `battery`, optional generation/RTC.
It calls `bb_save_begin`, `bb_save_queue` periodically and `bb_save_end` **before**
unloading its core or releasing its arena. A failed begin forbids game frames.
Paths and identity are confirmed by the adapter before begin. `battery_native`
(optional) says whether a smaller file of that size could be the cartridge's own
save as other emulators write it (sizes that contradict a known save type are
not). It never converts by itself: such a file gets the damaged-save question
with `foreign` set, and only the answer `BB_DAMAGE_USE` copies the original into
history (`HIST_SAVE`, read back) and then publishes it padded with
`battery_fill` through `safe_restore`. A damaged battery the player restores
or starts empty is set aside with `safe_park` (first free `.bad`, `.bad2` ...),
never removed; the copy is written and read back before the damaged file moves. The generic host
creates `saves/<system>/<key>.sav` and `states/<system>/<key>.stateN`; history is
namespaced by system. Legacy adapters can supply an explicit history key and
8-byte state/RTC magic without duplicating persistence code.

The state wrapper is 28 bytes (magic, version, payload bytes, content identity,
content size, payload CRC32), followed by serialized core bytes and a battery
snapshot. Loading validates the complete payload before entering the core,
flushes current progress, then preserves the committed battery and live clock
on successful **and partially failed** loads, reacquiring the battery pointer
because cores may replace backing RAM. `BB_STATE_PARTLY` invalidates execution: no
more frames, serialization or battery submissions; teardown preserves the
authoritative pre-load files and a new session initializes the core again. Failed history writes stop before the
core is touched. Current files recover from validated `.bak` / transaction
files through `safefile.c`. `.time` is optional `{payload_crc, unix_time}` metadata;
stale metadata never dates another state.

GBA keeps its existing paths, `BBGBAST1` wrapper (including embedded 128 KiB
battery), `BBGBART1` RTC bytes, migration/owner checks and history keys. PS1 keeps
its existing PSX3D card/state formats and paths. Its descriptor has no shared
state codec, but final card/Continue failures use the same `bb_final.c` ownership
and shell retry/discard/start barrier. Card shadows are reserved before gameplay;
header-bearing cards retain their exact prefix in the atomic file snapshot.

APT dispatch is synchronous on the main/core thread. Suspend/sleep/exit pause
sound and play time, drain battery snapshots and write Continue without any
GPU operation. Restore rebases pacing and resumes sound only outside the pause
menu. Teardown joins the writer before another system can claim the arena.
There is no per-frame shell dispatch added to PS1.

Both synchronous and deferred `bb_save_end` capture before core destruction and
finish through the same retention path. A successful writer drain cannot certify
a failed capture. Allocation failure before begin refuses the session; a failed
Continue capture remains visible until explicitly acknowledged. The existing
shared dialog acknowledges capture failures without suggesting an SD retry.
Retained write failures separately offer retry/discard, including when both kinds
occur together. `bb_save_end(true)` flushes battery/RTC before synchronously
writing shared libretro Continue; a battery failure leaves the prior Continue. Deferred write
failure retains the complete state and optional postcard for retry.
