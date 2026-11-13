# Experimental journal-backed indexed renderer

`indexed=1` enables the GBA adapter's native-index PICA compositor for a load;
`indexed=0` is the default CPU renderer in the same executable. `indexed=2`
keeps the CPU renderer as an oracle and compares every submitted PICA eye
against its RGB565 pixels. Verification adds synchronous readbacks and must
not be used for performance measurements. These are developer remote options,
not product settings. Color correction and frame mixing disable this prototype.

The memory write journal observes VRAM, OAM, palette and display-register writes,
including DMA and JIT stores. The compositor consumes its dirty bitmaps directly;
it does not invoke the older color-converting journal cache. A dirty 32-byte
block is checked only when its journal bit is set. Equal-value writes do not
split bands. The first frame is explicitly invalidated.

Referenced 4bpp/8bpp tiles occupy persistent native L4/L8 textures. Only a new or
changed referenced tile needs Morton swizzling. Palette changes never swizzle
or color-expand tiles. Generations referenced by earlier scanline bands remain
immutable until submission finishes. A previous-frame GPU fence precedes raw
texture writes. Slot exhaustion resets the cache between frames or falls back.

Immutable bands carry register/palette versions and tile quads. PICA lighting
RGB LUTs map native indices to palette colors. Palette bank, LUT role and
palette version select a persistent LUT cache. Geometry is grouped by palette
bank; depth preserves BG/OBJ order. An OBJ winner-ID target first flattens
objects, so an obscured object cannot become a second blend contributor.
Depth and stencil select the first and second contributors. Quotient/remainder
passes reproduce the CPU renderer's fixed-point blend rounding, including its
six-bit RGB565 green arithmetic. WIN0/WIN1 use scissor partitions. Scissoring
must be reapplied after every `C3D_FrameDrawOn`, which resets the viewport.

The CPU normally records writes, swizzles changed indices, builds tile quads and
submits draws/LUTs. The pixel oracle is used only by tests and to reconstruct a
retained frame prefix when an unsupported feature appears after GPU-owned rows.
A fallback is a complete CPU frame, not a CPU-composited common GPU frame.

This is a prototype, not full feature coverage. Bitmap modes, affine BG,
general affine OBJ (positive identity is admitted), mosaic, OBJ windows and
OBJ scanline-budget overflow currently fall back. Bounded band/quad/slot
exhaustion also falls back. These are implementation/exactness gaps, not claims
that PICA cannot express those features. They must be removed or individually
justified before the renderer can meet the full GPU-ownership product goal.

Host gates compare immutable lists and fallback against the production CPU
renderer, including real-core pixel/PCM/state captures where private fixtures
are available. Actual PICA readback remains a separate gate; passing the list
oracle is insufficient. Hardware timing is mandatory for Old 3DS acceptance.
Emulator tick timings and draw/upload counts are estimates only. The private
round report records fixture coverage, package hashes and pending hardware work.

Each visible scanline can begin a band (160 maximum). Buffers remain bounded;
quads and native index generations may still exhaust their separate capacities.

The offline prototype on trunk b5d6197 is packaged privately as title2003
(identity `breadbox-gba-index2003`, executable source411d038). Its CIA SHA256 is
`6ba7cf681e2e3a8bbe2c4e5fc66541abdac424d498b0a198db09bc5822f2023e`.
All13 private fixtures were run in mono/stereo for120guestframes per leg in
silent New-model Azahar. Admitted frames matched the CPU reference exactly:
0/57,715,200RGB565pixels across1503eye readbacks. CPU fallback-only legs have
no PICA comparison. Separately, real ARM JIT core tests passed exact pixel,
PCM and serialized-state comparisons for all26legs (600frames, Video1800).
The default-off/indexed1 same-binary Kirby/Metroid load/quit smoke also passed.

Full600frame core runs use GPU composition throughout OldMetroid,Kirby,Aria,
Emerald,Wario,AdvanceWars and ClassicNES. TMNT falls back27/600frames for affine
OBJ. Metroid story/GoldenSun use affineOBJ fallback600/600; MarioKart/F-Zero
use affineBG fallback600/600; Video uses bitmap fallback1800/1800. No mosaic,
OBJwindow,OBJbudget or capacity fallback occurred in those core fixture windows.
These are fixture-specific prototype results, not whole-game compatibility.
No console was contacted for this offline task; New recovery and hardware LUT
costs precede compositor timing. GPU stays off until Old hardware acceptance.
