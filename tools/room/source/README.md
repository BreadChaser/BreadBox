# Editable room asset sources

Everything the app's `assets/romfs/room` binaries are made from. How to rebuild and check them: `docs/assets/room.md`
(`tools/room/build.sh`, `tools/room/check.py`).

- The shipped room is `../lamplight/` (BBR v3, `SPEC.md`). This directory holds the hands and props sources.
- `hands/` the hand's mesh, 17-bone rig and anchors (`hand.json`) and its texture (`hand-room.png`, RGBA5551 values),
  recorded 2026-11-06 from the retired legacy room builder; `pack-hands.py` packs them with `hands-bake.json`.
- `props/` the game-start boxes (`props.py`, `geo.py`, `bbr.py`, `pix.py`; `build.py` runs them, NumPy only).
- `../strip-hand-mesh.py` derives `case-motion.bbh` from rebuilt `hands.bbh`, stripping the hand mesh and retaining the prop/camera tracks. It is reproducible from the recorded hand bake and build scripts.
- `hands-bake.json` the sampled hand clips (30 Hz) that `pack-hands.py` turns into `hands.bbh`. Editable data.
- `animation/` the JS that simulates and samples those clips (needs the UI mock page to run).
- `SPEC.md` the BBR1/BBH1 binary formats; `verification/` the round-10 framing and motion checks.
- `notes/` the round-8 GBA box/spine research and hand integration contract carried with the source.
- `hand-ref/` the CC BY 4.0 hand model the hands are made from, with its attribution.
- `CREDITS.md` the credits file the app ships. The invitation card uses a decorative bread medallion; the official
  share QR is drawn live by the app on the bottom screen.

Project-authored geometry, textures, animation logic and packers use the project GPL-2.0-or-later license; the hand
retains CC BY 4.0.
