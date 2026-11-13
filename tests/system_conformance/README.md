# Behavioral system conformance

Run `python3 tests/system_conformance gba` (or `bash tests/system_conformance/run.sh gba`) or
`BB_EXPERIMENTAL_NES=1 bash tests/system_conformance/run.sh nes`.
No ids means every adapter, including the experimental NES proof. The host
suite and CI run this command with NES enabled; no downloads or consoles.
Requires Python 3, C/C++ compilers, make, pthreads, zlib, libdl and sanitizers.
The PS1 partial driver additionally requires ARM Linux GCC and qemu-arm
(or qemu-arm-static) on PATH, as the existing core microtests do.

GBA uses the real gpSP **native interpreter**, its bundled open BIOS and an
original generated CC0 cartridge (`make-gba.py`). NES uses the real QuickNES
core and the existing CC0 generator, now with an audible pulse channel.
Generated ROMs, objects and save files live in a private temporary directory
and are deleted on success or failure. No commercial ROM or external BIOS is
used. `BB_CONFORMANCE_BUILD=/absolute/private/path` retains a local debug build
only; use a separate path per system and remove it yourself afterward.

Both drivers execute the production `retro_host.c`, `bb_save.c`, `save_writer.c`,
`safefile.c`, `history.c` and `bb_session.c`. The writer uses the existing pthread SDK substitute.
The device callbacks and clock are host substitutes; save operations are real.
The test performs:

- Two cold 120-frame boots, comparing complete pixel and stereo PCM hashes;
  pictures must contain nonzero pixels and PCM must contain nonzero samples.
- Raw serialized state save/load/save bytes must match exactly. Two restores
  of the same shell state then produce identical 30-frame pixel/PCM streams.
- Battery writes through the save service and a real file; teardown/reopen
  reloads and compares all live battery bytes with the committed file.
- Continue checkpoint (slot 10), validated Home Continue discovery, simulated HOME-close (writer drained before
  core destruction), fresh host/core, Continue restore and identical replay.
- Session/core destruction, a freed test arena loan, no active shell session,
  no live host battery accessor or save queue;
  ASan/UBSan fail immediately and LSan checks process exit, including cores.
- Production content identity/probe checks: GBA raw/ZIP/legacy-name identity
  suites, and NES valid/invalid iNES headers plus real content identity.
- Full real-adapter syntax compilation with SDK stubs, compiled production
  settings tables (unique keys, labels, types, model masks, choice labels and
  valid choice/toggle defaults), and all seven shared pause row labels.
  NES has no custom settings table; the shared Settings row remains present.

Structural checks retain namespace/header/build/scaffold coverage. Unknown
systems fail until a runtime driver is registered. Scaffold generation is only
an interface syntax test, never behavioral acceptance. The suite deliberately
fails on broken core/service behavior rather than accepting missing hooks.

GBA intentionally does not serialize its rendered sound ring: loading clears
in-flight samples (up to one frame of silence). Reference playback starts from
a restore too, so the test checks deterministic restored PCM, including that
silence, rather than equating it with uninterrupted playback. The fractional
frontend readout counter resets at that boundary as well. The raw serialized
state byte check is immediate and exact; no warmup or samples are discarded.

## Limits

These are shell **host contract** checks, not the CTR device frame loop:
physical HOME/APT timing, NDSP/GSP, executable-arena protection, hardware memory
budgets and ARM JIT correctness need their existing independent gates. The
schema check compiles immutable declarations from the actual adapter; it does
not claim persistence of device-specific setting hooks. Pause row presence is
checked here; scripted pause/sleep/close/Undo behavior remains in `gba-runtime`
and `retro-host`, and save faults remain in `save-service`.

PS1 is explicitly **PARTIAL**, printed separately. Its conformance command runs
the existing synthetic 60-frame ARM HLE state replay (`tests/core/run.sh determinism`),
comparing RAM/GPR/CP2 hashes and cycle counts. Beyond that, its adapter still owns the
legacy PSX3D loop/save formats and is not a native shared-libretro-host client.
There is no native host driver for that loop, HLE CD boot, GPU readbacks, PS1
memory cards or Home Continue. Existing `tests/core/run.sh` runs synthetic MIPS
programs on the ARM dynarec/interpreter; the private simulator states and game
images cannot become portable CI inputs. Completing PS1 requires an offline
licensed PS-X EXE/CD fixture and a host platform bridge for the legacy shell,
then its own battery/card and Continue test. A synthetic-core PS1 pass is not full
system acceptance and must not be described as such.
