# Vendored gpSP

Source: https://github.com/libretro/gpsp
Pinned commit: 5819380c2ffb0900219d700a382ee68c464ebb99 (2026-10-25).
This is a normal source directory, not a submodule. Keep COPYING, upstream
README.md, BIOS source/license and libretro dependency notices.

gpSP is GPLv2 family. The bundled open BIOS is GPLv2, by Normmatt and the
VBA/VBA-M team; see bios/README.md. No Nintendo BIOS is included.

BreadBox changes: CTR small JIT caches and bounded 4 MiB paged ROM cache;
Luma's cache SVCs 0x92/0x94 replace privileged callbacks; checked JIT allocation
and interpreter fallback; release the dynarec worklist on unload; deterministic
vendor version; actual JIT mapping/fallback logs; frontend-controlled picture
skipping while CPU/audio continue; bounded cartridge-window mappings for
non-power-of-two ROM page counts. The build partial-links
and namespaces all defined global symbols with gpsp_, and gives core sections
the .gba prefix so PS1 core/library selectors cannot consume them.

Only the BreadBox target builds and links this core. See tools/build/gba-core.py.

BreadBox frame deadline: optional retro_set_run_guard callback is polled every
1024 scheduler entries, allowing an emulated frame timeout to return normally.

CTR cache ownership (1041): map at most once per BreadBox process. Retain
successful aliases, aligned backing pages and the real process handle across
games; never unmap, reprotect or free aliased pages from deinit or rollback.
Only never-mapped allocations are freed after setup failure; a partial failure
latches interpreter mode until process restart. Each game clears execution
pointers and initializes fresh translation tables/emitter before JIT execution.
The BreadBox userAppExit hook suppresses libctru's regular-heap bulk free while
aliases exist and forces process termination (3dsx exits to HOME in that case).
The OS reclaims heap, aliases and handle together. Cost: 2.375 MiB retained
between games after the first successful GBA JIT load. Availability gates still
protect interpreter options/reset/cache-flush. Host fault injection:
`python3 tests/gba/jit.py` from the repository root.

ARM VRAM prefetch (1049): retain the two words already fetched by ARM7 while
interpreting VRAM, so a store over an in-flight instruction takes effect only
after pipeline refill. Classic NES startup executes precisely this SMC probe;
fresh-reading every instruction caused all 12 supplied Classic NES cartridges
to remain black. Branches/mode changes refill; VRAM translations remain uncached.
The independently authored implementation is GPL-2.0-or-later. GBATEK ARM7
pipeline semantics and comparative traces from mGBA (MPL; no code copied) were
used to diagnose it. Test: python3 tests/gba/prefetch.py. All 12 reach title or
attract screens at native host frame 599 with the bundled BIOS; hardware results
are tracked in the external perf-compat report.

Boktai GPIO and durable clock (1046/1051): independently authored solar
comparator and RTC date/time writes based on GBATEK, GPL-2.0-or-later.
Sunlight is a per-game 0..10 setting (default 5), with Y cycling it in play.
The frontend stores RTC offset/status in a checksummed, safely replaced .rtc
sidecar keyed by the full-ROM identity; wall time continues while closed.
Loading an older gameplay state preserves the current cartridge clock, as it
does battery RAM. Clock files recover from .bak/.tmp via the shared save layer.

Timer reload and wild jumps (16555; both also in upstream 5819380, worth
sending there): update_timers() now writes an overflowed timer's reloaded count
to its register at once (it read 0 until the next tick). GBA Video carts read
Timer 1 right after its overflow IRQ and took 0 as a full lap: Disney Channel
Collection Vol. 1 decoded ~4 billion samples over its own state and the dynarec
branched to -1; Ms. Pac-Man - Maze Madness hung at frame 1024. Branch targets in
regions nothing can execute (open bus, I/O, palette, OAM, save chips, above
0x0FFFFFFF) now get the VRAM bridge entry instead of ~0, and the bridge halts
the CPU until the next interrupt for them: bad dumps freeze instead of crashing
the host. Test: python3 tests/gba/core-fixes.py.
