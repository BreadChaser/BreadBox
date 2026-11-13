# Round 11 hands: integration note (the case flies into the hand)

Owner, Old 3DS, 2610: "Hand animations ... it looks so terrible still, I think the CD should just fly into the hand."

## What changed in the clips (`hands.bbh`, same BBH1 v3 layout, 76 clips)
- `pick_*` / `putback_*` (4 box classes x bare / mitten = 16 clips) are new: 1.1 s each (were 2.7 s), the **right hand only**
  (mask bit 1 is never set outside the game start).
- Take-out: the tipped case (browsing still only tips it 18 mm out) slides out of its cubby along its depth with nothing
  else moving, then arcs (26 mm) to the hold in front of the camera and turns to face it (spine turn, hold position
  and the round 9/10 framing are unchanged); one ease-out (soft start, fast middle, soft landing, no overshoot).
  The right hand rises into the frame while the case flies and closes on it as it lands (the open hand waits 40 mm
  off the grip until the case is there). The camera dollies back over the first 40 % of the clip.
- Put-back: the same timeline backwards (the hand lets go and leaves, the case flies home and ends tipped).
- Every other clip (hold, the four turns, open, card, picker: 60 of 76) is byte for byte the round 10 clip.

## What the device code has to do
Nothing. `shelf.c` already plays pick / putback at `hands_clip_seconds()` of the clip, the clip's first frame is the
tipped case, and the last frame is the hold's first frame. `hands_for_shelf()`'s camera blend (`camera_k`, over the first
35 % of the clip) now takes 0.4 s instead of 0.95 s: the clip's own camera is the device's close-up at the start, so it
blends from where it already is. Host test `tests/room/hands.c` checks the new lengths and that no left hand shows in
pick / putback / hold / turn clips.

## Measured (tools/breadbox-r10-motion.py 60, tools/breadbox-r10-framing.py)
See `ROOM3D-REPORT.md` round 11 (motion per 60 Hz frame, contact, clearance of the flying case from the neighbours, centring).
