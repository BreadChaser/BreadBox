# BBR v3 — the Lamplight room (2026-11-06, the shipped format)

`assets/romfs/room/room.bbr`, built by `tools/room/lamplight/build.py`. The BBR1 layout below with these changes:
no lightmap, no lightmap uv, light baked per vertex, **one colour stream per room style** (and optionally its own
atlas). Little-endian, every table 4-byte aligned.

| Offset | Size | What |
|---|---|---|
| 0 | 32 | header: `BBR1`, **version 3**, **flags = nStyle**, nTex, nPart, nAnchor, **stride 16**, nVert, nIdx, default style id |
| 32 | 24 × nTex | texture table (as v1) |
| … | 48 × nPart | part table (as v1) |
| … | 56 × nAnchor | anchor table (as v1) |
| … | 24 × nStyle | **style table**: `char id[8]; u32 colours, probes; u16 nProbe, atlas, range, pad` (offsets from the file start; atlas = texture index; range = the light range: 0 or 2 = 2.0, 4 = 4.0) |
| … | 16 × nVert | vertices (below); colours are the first style's |
| … | 2 × nIdx | u16 triangle list, padded to 4 |
| … | per style | its colour stream `u8 r, g, b, a` × nVert, then its probes 32 × nProbe (`f32 pos[3]; u8 cube[6][3]; u8 pad[2]`: light / range along +x −x +y −y +z −z) |
| … | … | texture data (each padded to 4) |

**Vertex (16 bytes):** `s16 x, y, z, w; s16 u, v; u8 r, g, b, a`. Meshes: w = palette slot, uv = atlas texels × 16
(top-left origin), colour = baked light × tint, 128 = 1.0 (×2 overbright, PS1 style; a range-4 style: 255 / 4 = 1.0),
a = 255. Sprites: w = size
(mm), a = corner 0–3, colour additive (255 = 1.0) × the texture's colour.

**Textures:** `atlas` (the first style's set) and one `atlas_<x>` per further texture set, 256 × 128 RGBA5551,
nearest filtered (polygons are cut on their texture's repeat lines at build time: no wrap needed); `tv` 128 × 128,
`shelf` 256 × 512 and `spines` 512 × 256 are runtime (no data).

**Parts (draw order):** `static` (slot 0), `p_tv p_lib p_set p_tog p_share` (slots 1–5), `a_card` (slot 11, pivot =
the card's centre): one draw, the room. `tv_screen` (the `tv` texture, slot 1, unlit). `glow`, `steam` (additive
sprites).

**Anchors:** `pose.<home continue library settings together share poster board>`, `focus.<place>`, `screen`, `scope`,
`slabel`, `knob`, `card`, `mug`, `poster`, `board` (as v1), and `case` = x0 (first upright's centre), bay, bays, row
gap, rows, top, depth, upright, board, **base** (the bottom row's floor, mm); `post` = the final pass's dither strength
(0..1: 1 is the PS1's ±4), the share of empty cubbies with decor (0..1).

**Styles:** at load the app makes one position/uv vertex buffer and, per style, a 4-byte colour array (the second
vertex attribute buffer) and the style's atlas texture; switching a style only picks other ones. After load only the
file's tables and the styles' probes are kept. The probe set (a few dozen points: in front of every cubby, over the
wall pictures, where the hands work) lights what the app builds at runtime (the shelf's games and plates, the wall
pictures, the hands).

## The library (the case and its runtime meshes)

The bookcase is a wall unit of `rows` × `bays` = 8 × 3 = **24 system slots**; every bay of a row is one
system's *section*. Only owned systems get a section, packed from the top left in a fixed order (gaps close
up); the other slots stay empty shelf. The file holds only the wood; the games are built at runtime:

- **Far (the room, the wall view)**: the `shelf` texture is baked ONCE per library: one generic spine tile per
  spine style (128×64: jewel, cardboard, clamshell, handheld case, mixed) and one brass plate per section
  (128×16, up to 24). Every section is one quad of its style's tile, cropped to how full the section is
  (every other section mirrored), plus its plate. All sections: one draw, no repaints while browsing.
  The wall view shows four rows at a time and follows the cursor; the section names are 2D labels.
- **Close (the section the camera glides into)**: its real boxes (≤31, every box inside the section's two
  uprights) with real spines (32×256 cells in the `spines` ring: a scan when available, else generated in the
  system's style). They cross-fade in over the generic quad while the camera arrives (alpha = smooth((zoom − .3)/.4)),
  then the generic quad is hidden (palette slot 2). Inside a section the camera stays near its middle and the
  boxes slide along it; boxes disappear behind the uprights (a box is drawn while its centre is between the
  upright mid-lines). Cells are streamed ≤1 per frame while scrolling.
- The pulled-out box is `props.bbr` (scaled to the system's box size) + the hands.

## props.bbr (BBR1 version 2, read as plain meshes)

Parts `ps1_tray ps1_disc ps1_lid gba_box gba_flap gba_cart` in R8's box-local frame (round 3: `ps1_disc` is no longer drawn — no disc images for most games)
(x = thickness, front at +x; y up; z = width, spine at −z). Pivots: lid hinge at the spine edge
(rotate about y), flap hinge on the back top edge (rotate about z), cartridge slides along y.
Anchors `ps1` / `gba`: T, H, W of the box.

`props.bbr` keeps the older layout: version 2, stride 20 (a lightmap uv the app skips), the header's flags field
counting probes (none). Built by `props/build.py`. The full v1/v2 description is in `docs/archive/room-bbr-v1-v2.md`.

# BBH1 — the hands for the device (round 3e)

`assets/romfs/room/hands.bbh`, built by `tools/room/source/pack-hands.py` from the hand source (`hands/hand.json`, `hands/hand-room.png`) and the recorded clips (`hands-bake.json`; the mock's own animation, sampled).
(the bake replays the live mock's clips at 30 Hz: what the mock shows is what the file holds). Romfs-ready
next to it: `props.bbr` (the cases the clips hold), `CREDITS.md` (CC BY 4.0 hand), `MANIFEST.json` (sizes,
SHA-256, clips, the contact check of every clip). Little-endian, tables 4-byte aligned, one `fread`.
`js/room3d-player.js` is the reference player: it draws every clip from this file alone (`#bb10=1&bbplayer=1`).

## Layout

| Offset | Size | What |
|---|---|---|
| 0 | 48 | header |
| … | 16 × nTex | textures: `char style[8]; u16 w, h; u32 offset` (**v3: 512×256** RGBA5551 (round 3h); since 2026-11-06 one texture, `room`, the app's sleeve and gloves; layout below) |
| … | 32 × 3 | parts: `char name[12]; u32 firstIdx, nIdx; f32 pivot[3]` — `hand`, `hand_sleeve`, `hand_mitt` (contiguous) |
| … | 40 × 17 | bones: `f32 head[3], flex[3], spread[3]; s8 parent; u8 pad[3]` (bones 0, 1: roots, parent −1) |
| … | 40 × 3 | anchors `hand`, `hand2`, `hand3`: `char name[8]; f32 v[8]` (as in the room file) |
| … | 4 × 6 | **v3**: glove table `s16 du, dv` (texels) for bare, knit, leather, fingerless, work, mittens |
| … | 48 × nClip | clips (below) |
| … | 16 × nVert | vertices: the room file's format; position w = bone (0–16) |
| … | 2 × nIdx | u16 triangle list, padded to 4 |
| … | … | texture data, then clip data (each padded to 4) |

Header: `char magic[4] = "BBH1"; u16 version (3), fps (30), nTex, nPart (3), nBone (17), nClip;
u32 nVert, nIdx, offVert, offIdx, offTex, offPart, offBone, offClip`. (Anchors, then the glove table, follow the bones.)

**Format change v2 → v3 (round 3g, for the port worker)**: the hand texture is 512×256 (round 3h; was 128×64) and its
layout changed; a 24-byte glove table follows the anchors (offClip moved by 24); the hand body's uvs come from a
real unwrap and a detailed mesh (round 3h: 1003 positions, 1988 triangles, 1616 vertices after the unwrap's seams in the bare range; mitten 545); mitten vertices are no longer flagged as
skin (alpha 255, absolute uvs). Everything else (vertex format, bones, clips, frame records) is unchanged.

Texture layout (v3, round 3h): six 128×128 tiles of the hand body's unwrap at (0,0) (128,0) (256,0) (0,128) (128,128) (256,128)
= bare skin, knit, leather, fingerless, work gloves (the sixth is unused: the mitten has its own mesh); the
mitten's own 128×128 unwrap at (384,0); the sleeve 32×32 at (384,128), the cuff 32×12 at (416,128). Every tile has
the **ambient occlusion baked in** (Blender Cycles AO of the rest pose: between the fingers, the knuckle folds,
under the palm), multiplied over its colour; the bare tile adds a light mottling and slightly warmer knuckles and
fingertips. Alpha 1 = skin (tinted by the skin tone uniform), 0 = fabric. A glove = the vertex shader adds that
glove's table offset to the uv of vertices with alpha 254 (the body); the mitten, sleeve and cuff use their uvs
as they are.

Clip entry: `char name[16]; u8 space, box, loop, flags; u16 nFrames, frameBytes; u32 mask, offset; f32 THW[3]; u32 pad`.
- space: 0 = **slot** (the selected box standing in its shelf slot: x right, y up, z into the shelf; add the
  slot's world position), 1 = **card** (the QR card's stand pivot), 2 = **camera** (the scope camera: x right,
  y up, z forward). box: 0 none, 1 PS1, 2 GBA; THW = the box the clip was baked with (T, H, W mm).
- flags bit 0: the mitten version (`*_m` clips: the grips are solved for the mitten's shape; every other glove
  uses the bare clips). loop: play `hold_*`, `card_hold`, `picker_idle` in a loop.
- mask: bit 0 right hand, bit 1 left hand, bits 2–6 objects `case lid flap cart card`: which blocks a frame holds.

Frame record (`frameBytes`, fixed per clip), in this order:

| Field | Type | |
|---|---|---|
| flags | u8 | bit 0 right hand shown, bit 1 left hand shown (a block is present but unused when its bit is 0) |
| fade | u8 | the game-start fade to black, 255 = black |
| pad | u16 | |
| camera | s16 × 8 | position xyz, look-at xyz (¼ mm), focal length (px), pad |
| right hand (mask bit 0) | s16 × 54 | palm matrix (12), sleeve matrix (12), joints (30) |
| left hand (mask bit 1) | s16 × 54 | the same, mirrored matrices (det −1) |
| objects (mask bits 2–6) | s16 × 12 each | `case`, `lid`, `flap`, `cart`, `card` placement |

Matrices are 3×4 row-major `r00 r01 r02 tx r10 … tz`: rotation × 16384, translation in ¼ mm. Joints: bones
2–16 as `flex, spread` pairs, radians × 8192. The sleeve matrix is its own (the forearm twists less than the
hand); the palm matrix places bone 1.

## Playing a frame (CPU, per hand, < 10 µs)

1. Interpolate between the two keyframes around `t × 30`: joints linearly; matrices linearly then
   re-orthonormalise (the steps are 1/30 s: no slerp needed). Mirrored matrices stay mirrored.
2. Forward kinematics, k = 2 … 16 in order: `M[k] = M[parent] · T(head) · R(spread axis, spread) · R(flex axis,
   flex) · T(−head)` with `M[1]` = the palm matrix, `M[0]` = the sleeve matrix (axes in model space, the
   rotation is the usual axis-angle matrix). Mittens: copy the middle finger's three joints to index, ring and
   pinky (the pouch only uses the middle chain; the mitten clips already do).
3. World = clip space → world (the slot position, the card stand, the scope camera) × M[k].

## Drawing (PICA vertex shader skinning, one draw per hand)

- Uniforms per draw: the 17 matrices as 3 rows each (**51 float vec4**), the view-projection (4), the glove
  offset + skin tone (2). 57 of the 96 float uniforms.
- Range: bare hand = parts `hand`..`hand_sleeve` (round 3h: 6222 indices, 1616 vertices; smooth normals, split only at the unwrap's seams); mittens = `hand_sleeve`..
  `hand_mitt` (1518 indices; v3 vertex counts in MANIFEST.json). Same texture for both, culling off (the left hand is mirrored).
- The vertex shader (v0 = position + bone, v1 = uv, v2 = colour):

```
mul   r0.x, v0.w, c_three.x      ; bone × 3 (rows per matrix)
mova  a0.x, r0.x
mov   r1, v0
mov   r1.w, c_one.x              ; position, w = 1
dp4   r2.x, c_bones[a0.x+0], r1  ; skin: 3 dp4 against the bone's rows
dp4   r2.y, c_bones[a0.x+1], r1
dp4   r2.z, c_bones[a0.x+2], r1
mov   r2.w, c_one.x
dp4   o0.x, c_vp[0], r2          ; then the usual projection (4 dp4)
...
slt   r3.x, v2.w, c_0999.x       ; alpha 254 = hand surface -> add the glove tile offset
mad   o1.xy, c_glove.xy, r3.xx, v1.xy
```
- Fragment: texture × vertex colour × 2 × lerp(1, skin tone, texture alpha) (two combiner stages). The vertex
  colours are the light (round 3f: smooth normals, soft wrap key + fill, baked in model space): no runtime lighting.

## Budget per eye (Old 3DS, both hands out)

| | Bare hands | Mittens |
|---|---|---|
| Draws | 2 (+ the case/lid draws the clip shows anyway) | 2 |
| Vertices (shader runs, indexed) | 2 × 1616 = 3232 (2 × 6222 indices) | 2 × 423 = 846 (2 × 1518 indices) |
| Uniform upload | 2 × 51 vec4 bones + 6 = 108 vec4 ≈ 1.7 KB of commands | same |
| CPU | decode + interpolate + FK both hands ≈ 10–20 µs (once per frame, both eyes reuse it) | same |

Estimates, not measured on hardware: the vertex shader is 14 instructions; at the Old 3DS's 4 vertex units
and 268 MHz, ~3200 vertices ≈ 0.08–0.15 ms of GPU time per eye (round 3h; texture 256 KB per style in VRAM). The data: 108 B per hand per frame at 30 Hz.
