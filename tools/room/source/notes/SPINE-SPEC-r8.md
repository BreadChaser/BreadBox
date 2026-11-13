# Spines and boxes, round 8 (owner feedback 2026-10-30)

Sources: `the mock spine references` (photos + SPINE-NOTES.md), `the box research notes`
(box dimensions, region layouts, sources), `the runtime scan sources` (runtime scan sources).
Owner rules: authentic per region; nothing Nintendo/Sony shipped (wordmarks as plain text in our font, real
logos only when downloaded at runtime like today's PS logo); no slot numbers on the case sides / spines.

## Dimensions (mm, width x height x thickness)

| Box | Now | Round 8 | Note |
|---|---|---|---|
| GBA (NA + EU) | 120 x 137 x 27 | **125 x 125 x 23** | research: NA/EU face 124-125, depth 22.8-25 (scans give 23); owner said 125x125x25 unless better NA data: the scans are better |
| GBA (JP) | same | keep 125 x 125 x 23 on the shelf | real JP box is landscape 137 x 87 x 21; a separate shape is a later round |
| PS1 jewel, NA / JP, 1 disc | 142 x 125 x 14 | **142 x 125 x 10.5** | standard jewel case 10.4 |
| PS1 PAL, 1 disc | 142 x 125 x 14 | **142 x 125 x 17** | PAL "thick" case, refs 17-18 (sources disagree) |
| PS1 multi-disc (2+ discs, any region) | 14 | **142 x 125 x 25** | double jewel / PAL multiway 25 |

Multi-disc detection (device): a `.m3u` playlist, a "(Disc N)" tag group collapsed to one entry, or the PS1 library's
disc count; the mock uses a known-title list.

Held box: the hands are baked per box class (BBH1 v3 clip `box` byte): 1 = `ps1` (10.5), 2 = `gba` (23),
3 = `ps1p` (PAL 17), 4 = `ps1d` (multi-disc 25). Clip names end in the class (`pick_ps1d`, ...). A player that
only knows 1 and 2 keeps working (it picks `ps1` for every jewel case).

## Region (PS1)

From the serial when known (SLUS/SCUS/SLUSP = NA; SLES/SCES/SCED = PAL; SLPS/SLPM/SCPS/SCPM/SIPS/SLKA = JP),
else the file name's region tag (today's `region_of`), else NA.

## Spine layouts (fractions of the spine length from the top; width = the box thickness)

### PS1 NA (black)
- background near black (#1a1a1d -> #0c0c0c), 1 px lighter rail line each long edge;
- 0-11 %: the PlayStation logo when downloaded, else the text **PlayStation** (white, small, rotated with the title);
- 13-90 %: title, white, bold, top-to-bottom, centred, squeezed (not wrapped) when long;
- 90-100 %: empty.

### PS1 PAL
- background black (about 2 in 3 games, by title hash) or dark blue #1b2f6a, white title;
- logo block as NA plus a small **PAL** text under it; a thin light band at the foot (the publisher band, plain).

### PS1 JP (white spine card)
- background white #f2f2ee -> #e2e2dc; black title (top-to-bottom); logo block small, dark;
- bottom 92-98 %: the product code (e.g. `SLPS-01234`) in tiny dark text when the serial is known, else a thin dark bar.

### GBA (no common template: "zero consistency between GBA games")
1. A real spine scan, fetched at runtime like the covers, when a source has one (see gba-spine-sources.md).
2. Fallback: **one solid colour from the game's own cover** (the dominant saturated colour, not the average:
   hue bins weighted by saturation x value, the top bin's mean; lightness kept in 18-82 %), a soft vertical
   shade, a 1 px lighter line on both long edges (cardboard). Title top-to-bottom in the upper 8-80 %, white with
   a 1 px dark outline on dark/saturated colours, near black on light ones. At the foot (84-97 %) a small
   **GAME BOY ADVANCE** in plain text (same ink, 70 %). No ring mark, no art strip.
