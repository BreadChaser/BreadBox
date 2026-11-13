# GBA layer stereo research — round 2

No setting: GBA 3D follows the 3D slider (slider up = 3D, down = 2D) on Old and New
3DS, independently of PS1's stereo setting. Color correction and frame blending
keep the mono picture. Smoothing remains supported.

The compositor renders **both eyes independently** from the same scanline state.
It shifts each BG's sampling registers and each OBJ priority's renderer-local X
coordinate, then restores all sampling registers. OAM, VRAM, emulation, audio and
save state contents are untouched. The eyes share BG decoding on eligible rows, never emulation steps or a
displacement of the finished image. Windows/OBJ windows stay
at the screen; normal priority ties, transparency and blends resolve per eye.

Round 1 assigned depth by BG number (0/1/2/3 px), moving only the right eye.
That reverses TMNT's scenery order: its gameplay uses BG3 priority 0 for the
front overlay, BG2 priority 1 for the street, BG1 priority 2 for middle scenery,
and BG0 priority 3 for the far background. Round 2 sorts by BGxCNT priority,
then BG number for hardware ties. OBJ attr2 priority chooses sprite planes.

A complete frame's enabled BG priority votes and visible OBJ-list counts estimate
the main actor priority. A nonzero OBJ priority is preferred over priority-zero
HUD objects; among these, the largest sustained count wins. The first BG at that
priority (or nearest foreground BG when no exact match exists) anchors the
playfield. Overlays and playfield stay at screen depth; farther BG ranks spread
to eight native pixels behind. Main actors get two native pixels in front;
higher-priority OBJ overlays stay at the screen, and lower-priority OBJ scenery
is placed between its neighboring BG planes. This is a heuristic for hardware
layers, not a semantic segmentation guarantee. It does not use scroll speed.

For TMNT's captured street scene the targets are BG3=0, BG2=0, BG1=4, BG0=8,
OBJ priority 1=-2 native pixels. Kirby's mountain scene has HUD/terrain=0,
mountains=8 and OBJ priority 2=-2. Positive disparity is behind the screen.
The slider supplies 0..256 strength; each eye takes half the scaled separation
in the opposite direction, rounded to native pixels. Full-scale eye shifts are
bounded to one pixel in front and four behind (ten-pixel total depth span).
Odd target separations quantize to even realized separations. Low slider values
can round to zero; the framebuffer does not support subpixel layer sampling.

Role changes must persist for twelve rendered frames, then target depth slews
by at most 1/8 native pixel per rendered frame. Ordinary scrolling, palette
animation and transparent pixels do not reorder depth. Startup/blank recovery
also ramps over sixty rendered frames. Bitmap/forced-blank lines are mono, hold
subsequent rendering flat for thirty rendered frames, then restore stereo.
A split-mode frame can contain both flat and stereo bands; both eye buffers are
always presented together, so a later flat band does not discard one eye's
previous stereo bands. Timing is measured in rendered frames, so frameskip
extends the wall-clock settling interval.

With stereo off the original mono compositor runs once. No second render,
priority voting, eye upload/draw or extra allocation occurs. Small scanline
eligibility checks and a zero-valued OBJ coordinate addition remain; this is
not a claim of literally zero extra instructions. Turning it off restores exact
mono pixels. Eye texture lifetime waits remain unchanged; either model can allocate the buffers.

`GBA_STEREO_DEBUG` builds add an unshifted source-ownership pass, priority bits
and per-scanline target depths. Blends label the top contributor; the ordinary
eye compositor still blends both contributors. The production CIA has no debug
pass/buffers. `tests/gba_stereo/run.sh` uses real renderer sources and synthetic
VRAM/OAM, without ROMs, to verify priority/tie order, playfield anchoring,
debounce, symmetric BG/OBJ movement, restored registers/OAM and exact mono off.
It is included in `make host-tests`.

The gallery includes TMNT and Kirby gameplay, Emerald overworld/dialog, a Mario
Kart race, Advance Wars map, Metroid intro and Aria name entry. Single hardware
layers mixing HUD and scenery cannot be split by this method. Bitmap games stay
flat; affine tracks remain planes, not reconstructed perspective geometry.
Physical comfort, sustained performance and edge/window rivalry require Old and New
3DS review. This worker does not contact either console or publish a release.

## Build 1042: shared decoding and timing admission

Plain tiled/affine rows with no windows, mosaic, color effect or translucent OBJ
walk layer priority once. Each decoded BG color writes to both eye destinations
at that BG's fixed separation. Transparency leaves each destination's independent
underlay intact. Only nonoverlapping edge columns decode again. OBJ groups retain
the original per-eye renderer and culling, including its outer-edge behavior.
No whole-frame layer surfaces or per-pixel ownership/depth buffers are needed.

Other eligible rows cache the base BG before it is overdrawn, in the exact
RGB565/indexed/stacked representation used by the existing compositor. The right
eye copies the overlapping shifted span and renders the exposed edge. WIN0/1
segments are cached separately, with their original screen-fixed boundaries and
effect modes. OBJ-window and mosaic rows fall back to complete eye composition.
Rows with no quantized shift on any enabled BG/visible OBJ group render once.
All decisions use this scanline's registers/order; no cache survives the row.
Scratch storage is 960 bytes plus eight small span records. No mono pixel loop
has a runtime stereo branch: the dual-store mode is a separate template instance.

Before emulation the frontend selects 60 stereo pictures, 30 stereo pictures,
or ordinary mono. Admission uses medians of the latest 60 available mono,
presentation, CPU/audio-only and differential stereo cost samples, with 1 ms
headroom per emulated frame. Startup collects 30 mono frames and one CPU/audio-only frame. Until actual
stereo samples exist, the extra estimate is 2 ms Old / 1 ms New; unlike the old
high-water floor, a measured median replaces it, even if smaller.

At 30 pictures, every other frame skips video entirely and holds both eyes of
the preceding picture. CPU, audio, input and save work still run each frame.
The pair budget includes one stereo frame and one CPU/audio-only frame; before
a skipped-frame sample exists it conservatively substitutes the mono cost.
60-picture admission uses one frame's budget. Neither mode changes the clock.
Persistent schedule debt returns to mono for 120 frames before retrying.
Skipped pictures never upload or present a partial eye. Mono rejection uses the
ordinary mono compositor; the 1042 eye compositor is unchanged.

Per-second `gba: layer3d` logs include `stereo`, `mono`, `pictures`,
`mode=60|30|mono`, `mono_median_ms`, `extra_ms`, `extra_samples`, `force` and
`cooldown`. Pictures are frontend submissions, not physical LCD measurements.
`extra_ms` is the median observed stereo emulation+upload+presentation total
minus the calibrated mono+presentation median (clamped at zero), not a pure
right-eye microbenchmark. It remains zero with `extra_samples=0` until actual
nonflat stereo samples arrive. Baseline samples refresh during ordinary mono
fallback; no periodic depth-dropping probe interrupts steady stereo. Scene
changes and scheduling contention can affect the differential estimate.
Developer remote `layer3d=2` bypasses cost/late admission after calibration.

This is a predictive safeguard, not a proof that an unobserved workload spike
cannot miss a deadline. Stereo still has CPU/upload/draw overhead; unknown scene
costs and Old instruction-cache layout require hardware A/B/A. If any sustained
speed/audio regression remains, leave the default-off option off for that game.

Offline acceptance compares 600 consecutive frames in each of the seven r2
fixtures: both eyes match the original r2 compositor byte for byte with timing
admission disabled; PCM and serialized state match; disabled mono matches too.
The production frontend's intended difference is whole-frame mono under timing
pressure (also no extra eye for completely flat frames). Synthetic differential
tests exercise text/affine sampling, transparent edges, windows, mosaic and
blends against the original two-pass algorithm. No console contact by this worker.
