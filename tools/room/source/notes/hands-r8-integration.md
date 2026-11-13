# Round 8 hands: integration note for ui/room-port (bb-room-port-r1)

Files: `the approved room export` (BBH1 v3, same layout), `props.bbr`, `MANIFEST.json`.
Mock source: `the room animation mock` (`pulledBox`, `holdHand`, `takeHand`, `bakeAll`).

## What changed in the clips
- **Take-out (owner: "grab it with the left, hold with the right")**: the LEFT hand reaches into the cubby,
  closes on the tipped box, pulls it straight out and lifts it to the view (turning it to face the camera); the
  RIGHT hand comes in and takes the free edge (the old view grip, palm lit as before), the left lets go and
  leaves. The box only moves sideways inside the lift (one eased arc to the view's middle).
  Timeline (u of the clip): left reach 0-.2, close .11-.29, pull out .3-.48, lift + turn .44-.7, right in .55-.7,
  right closes .62-.82, left lets go .78-.97, left out .9-1.
- **Put-back**: the same clip backwards (right passes to left, left slides it in) and it ends **tipped**.
- **Hold**: a seamless loop (see below); the turns, the put-back and the game start begin on the hold's
  first frame (baked on that phase).
- **Box classes** (clip `box` byte; names end in the class): 1 `ps1` jewel 10.5 mm, 2 `gba` 125 x 125 x 23,
  3 `ps1p` PAL jewel 17 mm, 4 `ps1d` multi-disc 25 mm. A player that only asks for 1 / 2 keeps working.

## What the device code has to do (shelf.c / hands.c / shelf_scene.c)
1. **No auto take-out on hover.** Browsing only tips the selected box out **18 mm straight toward the room
   (-z in slot space), no tilt** (ease it out ~0.2 s, back in when the selection moves). A starts the take-out.
   The pick clip's first frame is the box at slot + (0, 0, -18).
2. **Play pick / putback at their own length**: today `pull(1, 1.05f)` and `pull(0, .7f)` stretch them; use
   `hands_clip_seconds()` of the pick / putback clip (2.7 s each). After the put-back the box is back at the
   tip; the code eases it in when the selection moves (as for browsing).
3. **Hold loop**: `hold_*` has 121 frames at 30 Hz, frame 120 == frame 0 (one 4.0 s period, velocity matches
   too). Seamless playback: loop period `(frames - 1) / fps` and wrap the Catmull-Rom neighbours (index -1 ->
   frames - 2, index frames -> 1). With today's `frames / fps` + "hold the last key" it shows one 33 ms
   duplicate pose at the seam (no pop, a tiny stall). Same for `card_hold_*` and `picker_idle_*` (4.0 s too).
4. **Box size**: the held box is scaled by shelf size / clip size (`room_hands.c` ~529). These clips are baked
   for the round-8 sizes (GBA 125 x 125 x 23, PS1 10.5 / 17 / 25), which ship on branch `ui/gba-box`
   (sys_gba.c / sys_ps1.c / shelf.c). Take both together; with the old 120 x 137 x 27 GBA package the hands
   are ~2 mm off the box sides.
5. The left hand now shows in pick / putback (mask bit 1), as in open.

## Measured (tools/breadbox-r10-motion.py 60, every clip, 60 Hz; contact from the bake and replayed from the file)
all 76 clips: no hand vertex moves more than 30.4 mm and no object corner more than 26.6 mm per 60 Hz frame (target ~30); every loop (hold, card hold, picker idle) seams at 0 mm position / 0.012 mm velocity; the case's sideways range while held is 0 mm (target <= 2); contact 0.96 mm in the bake, 0.96 mm replayed from hands.bbh (target <= 1). hands.bbh 2,192,868 bytes, 76 clips.

| clip | hand mm/frame | object mm/frame | case sideways range mm | loop seam pos / vel mm |
|---|---|---|---|---|
| pick_ps1 | 29.3 | 9 | 30.59 |  /  |
| hold_ps1 | 0.1 | 0.1 | 0 | 0 / 0.002 |
| turn_r1_ps1 | 4.9 | 3.2 | 0 |  /  |
| turn_r2_ps1 | 21.5 | 3.1 | 0 |  /  |
| turn_l1_ps1 | 21.1 | 3.2 | 0 |  /  |
| turn_l2_ps1 | 4.8 | 3.1 | 0 |  /  |
| putback_ps1 | 29.3 | 9 | 30.59 |  /  |
| open_ps1 | 23.1 | 10.5 | 34 |  /  |
| pick_ps1p | 29.3 | 9 | 30.59 |  /  |
| hold_ps1p | 0.1 | 0.1 | 0 | 0 / 0.002 |
| turn_r1_ps1p | 5.4 | 3.2 | 0 |  /  |
| turn_r2_ps1p | 23.3 | 3.1 | 0 |  /  |
| turn_l1_ps1p | 22.7 | 3.2 | 0 |  /  |
| turn_l2_ps1p | 5.3 | 3.1 | 0 |  /  |
| putback_ps1p | 29.3 | 9 | 30.59 |  /  |
| open_ps1p | 23 | 10.5 | 34 |  /  |
| pick_ps1d | 29.3 | 9.1 | 30.59 |  /  |
| hold_ps1d | 0.1 | 0.1 | 0 | 0 / 0.002 |
| turn_r1_ps1d | 5.5 | 3.2 | 0 |  /  |
| turn_r2_ps1d | 21.1 | 3.2 | 0 |  /  |
| turn_l1_ps1d | 20.4 | 3.2 | 0 |  /  |
| turn_l2_ps1d | 5.4 | 3.2 | 0 |  /  |
| putback_ps1d | 29.3 | 9.1 | 30.59 |  /  |
| open_ps1d | 23 | 10.6 | 34 |  /  |
| pick_gba | 29.3 | 9.6 | 78.49 |  /  |
| hold_gba | 0.1 | 0.1 | 0 | 0 / 0.002 |
| turn_r1_gba | 5.2 | 2.9 | 0 |  /  |
| turn_r2_gba | 20.8 | 2.8 | 0 |  /  |
| turn_l1_gba | 20.2 | 2.9 | 0 |  /  |
| turn_l2_gba | 5 | 2.8 | 0 |  /  |
| putback_gba | 29.3 | 9.6 | 78.49 |  /  |
| open_gba | 24.6 | 26.6 | 14.14 |  /  |
| card_take | 24.1 | 8 | 0 |  /  |
| card_hold | 0.2 | 0.1 | 0 | 0 / 0.004 |
| card_return | 24.1 | 8 | 0 |  /  |
| picker_in | 21 | 0 | 0 |  /  |
| picker_idle | 0.5 | 0 | 0 | 0 / 0.01 |
| picker_out | 21 | 0 | 0 |  /  |

(The sideways range of pick / putback is the deliberate lift arc to the view's middle; during the hold it is 0.)
Note: the room-port host test `tests/room/hands.c` saw 44 clips with the old file; this one has 76 (4 box classes).

## Round 9 (owner: "the game cases are not centered when he is holding them")
- Cause: the mock held the case 30 mm left of the camera's line of sight (room for the right hand), about 43 px
  left of the screen centre at the clip camera, and 13 px high.
- Fix is in the clips only (`hands.bbh` re-exported, BBH1 v3, 76 clips, same size): the held case now sits on the
  camera's centre line and 9 mm lower. **No device code change**: at hold / end of pick / turns the device uses the
  clip's own camera (`camera_k` = 1, F 600), so the framing comes from the file. The device already plays pick /
  putback at clip length and loops hold at `hands_loop_seconds` (f278216).
- Measured at that camera (`tools/breadbox-r10-framing.py`, 400 x 240): hold, all four box classes: centre within
  1.4 px of x = 200, centre y 120-123, at least 24 px from every screen edge, right hand covers 3.1-4.5 % of the
  cover. Turn ends: within 6 px; mid-turn peak 8.6 px. Contact 0.96 mm (bake and replay), <= 30.4 mm per 60 Hz frame.
- The mock's shelf close-up now uses the device's round-13 camera (`room_section_pose`: 540 * (hmax + 26) / 240,
  min 325, 560 fast), so the start of the take-out films matches the device too.

## Round 10 (spine turn, backs, hand mesh)
- **Turns**: turning to the spine now rotates the case the other way, so its real spine face (-z, where the spine
  art is) comes square to the camera (the hold's .16 rad yaw fades out over the turn). Before, the free edge came to
  the camera. Turn-clip ends: centre x within 1.1 px, right hand behind the case (0 % of the spine covered).
  The device plays turns from the file, so **no code change**; `room_hands.c` `spine_both_ends()` (the +z end also
  showing the spine "until the export agrees") is no longer needed and can go.
- **Hand mesh**: the detail mesh is ~1800 positions from two subdivision levels (was ~1000 from one): 2702 GPU
  vertices / 3590 triangles per bare hand (was 1616 / 1988), mitten unchanged. The vertex shader cost grows; CPU
  unchanged. Both hands in a take-out: ~5.4k vertices. Room files +33 KB each, hands.bbh 2,219,856 bytes.
  Positions are still stored in whole mm (the room file's s16): ~1.4 px at the clip camera. Finer positions would
  need a scale in the format (BBH1 v4); not done.
- **Backs** (code side, not in the file): with no real back scan the device shows the front darkened. The mock now
  draws a plain back in the real style instead: GBA: the cover's colour, the indigo system band with GAME BOY
  ADVANCE down the left, the title, two screens cut from the cover art, blurb lines, a blank rating box; PS1: dark
  insert with the spine strips at both edges. No codes, no numbers (`authenticBack` in breadbox-r10-room3d.js).
