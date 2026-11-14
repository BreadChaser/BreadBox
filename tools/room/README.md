# Room assets: rebuilding them from source

The room binaries are reproducible from the checked-in sources and recorded inputs.

| Shipped file | Made from |
|---|---|
| `room.bbr` | `tools/room/lamplight/` (Blender 5.2.3 headless: geometry, textures, the Cycles per-vertex bake of every room style, the clip check) |
| `props.bbr` | `source/props/` (`build.py`; NumPy) |
| `hands.bbh` | `source/hands-bake.json` (the sampled clips) + `source/hands/` (the hand's mesh, rig and its one texture), packed by `source/pack-hands.py` |
| `case-motion.bbh` | `hands.bbh` with the hand mesh stripped; prop and camera tracks are retained |
| `CREDITS.md` | written by hand: `source/CREDITS.md` |

The app build itself needs none of this (it packs the checked-in binaries).

## The Lamplight room (`tools/room/lamplight/`)

The Lamplight room: PS1-style low-poly models, small hand-drawn textures at 32 texels per
metre, light baked per vertex (Gouraud). The light is Cycles global
illumination baked per vertex on a mesh refined where the light changes, calm textures, the final pass's dither at
half strength and decor in every other empty cubby.

- `scene.py` the models (metres, z up), with the parts the app needs: the bookcase built from the library's `case`
  contract (8 rows x 3 bays), the poster and corkboard frames, the TV picture, the share card, the glows.
- `tex.py` every texture from shapes and palette ramps (at most 16 colours and 64x64 each, asserted), one set per room
  style; `bbr3.py` packs them into one 256x128 RGBA5551 atlas per set (nearest filtered, no wrap: polygons are cut
  on their texture's repeat lines) and writes BBR v3 (`source/SPEC.md`).
- `lights.json` every room style (Day, Night) as data: its Cycles light rig (`gi`: sun, moon, bulbs, area lights,
  exposure, light range 2 or 4, contact-shadow pass, sample counts; a light's `only` / `except` part list links it to
  those parts: the window's sun and sky have warmer, weaker copies for the wallpaper), tints, glows, and its texture set (wood, floor,
  wallpaper and rug palettes, the window's gradient and view); `post`: the dither strength and the share of empty
  cubbies with decor (the room file's `post` anchor). Changing a style's look is editing this file and rebuilding: no
  code change. `gi.py` bakes it with Cycles into the style's colour stream and its probe set (the light on runtime
  meshes: shelf games, wall pictures, hands); `rig.py` holds the tints, emission and the unlit parts' colours.
- `refine.json` extra grid lines per surface (floor, walls, wainscot, rug) where the baked light changes faster than
  the 30 cm cells follow; `scene.py` cuts the surfaces on them. It is kept data: it was made once,
  from a dense bake of the room's light (bilinear error at most 0.25 display units, lines at least 7.5 cm apart), and
  nothing in this tree regenerates it. Change it by hand (whole lines per surface: no T-junctions) when the light or
  the geometry moves. Host test: `tests/room/refine.py` (the shipped room has every refined line).
- `clip.py` the clip check, a failing gate: no two props intersect or come within 5 mm, except the resting contacts
  it lists (what holds, what rests), and a resting prop sinks at most 20 mm into its support (the rug is 12 mm).
  `build.sh` also runs two self-tests that must fail: `--clip-selftest` (a lamp placed inside the bookcase) and
  `--sink-selftest` (the desk 5 cm into the floor). The build also fails when a cushion leaves the Home view.
- `build.py` removes an earlier run's outputs first, writes `room.bbr` through a temporary file and writes `OK` (the
  room file's SHA-256) last; `build.sh` runs Blender with `--python-exit-code 1`, keeps its status and ships the room
  only with that marker (Blender otherwise exits 0 on a Python error). Host test: `tests/room/build-sh.sh`.
- `preview.py room.bbr out.png [--style night] [--pose settings]` draws the file as the app does (without the runtime
  parts), through the final RGB565 dither: review art changes before a device build.

## Rebuild and check

```
export BLENDER=/path/to/blender-5.2.3-linux-x64/blender   # 5.2.3 only, build.sh refuses other versions
tools/room/build.sh                  # the room: about 10 min (Cycles, 2 styles x 2048 samples a vertex)
tools/room/build.sh --hands          # also the hands and props (seconds)
python3 tools/room/check.py          # SHA-256 of every rebuilt shipped file against the rebuilt one (--all: every file)
```

`build.sh` writes to `tools/room/build/` (git-ignored) and never touches the source or the romfs. `check.py` exits
non-zero if a rebuilt file differs from the shipped one, or a shipped file from its line in `tools/room/ASSETS.sha256`.

Reproducible outputs: the Blender build is pinned (and checked), the scene starts from `--factory-startup`, the room
build has no randomness beyond fixed seeds (Cycles seed 1), and the packers are plain Python with fixed field order.
Two builds give the same SHA-256 only with the same sample counts: the `samples` / `ao_samples` of each style in
`lights.json` (Day and Night: 2048 / 256). `build.sh` drops every `GI_*` variable of the caller; `--quick` takes
`GI_SAMPLES` and `GI_AO_SAMPLES` for a fast look and ships nothing. `--threads N` (1 or 2) is Blender's and Cycles'
thread count; `--ref` renders each style's bake scene from Home (plain Cycles, no textures) and writes no room file.

## Changing an asset

1. Edit the source (`lamplight/*.py`, `lamplight/lights.json`, `source/props/*.py`, `source/hands/`, `source/hands-bake.json`, ...).
2. `tools/room/build.sh` (`--hands` for the hands), then copy the files you changed from `tools/room/build/romfs/room/`
   into `assets/romfs/room/`.
3. `python3 tools/room/check.py --update` rewrites `ASSETS.sha256` from the shipped files, then
   `python3 tools/room/check.py` must report `all match`.
4. Commit source, binaries and `ASSETS.sha256` together.

## Not regenerated here

- `source/hands-bake.json` stores the sampled hand clips used by the build. It is not regenerated by `build.sh`; the
  original mock simulation is not a standalone pipeline in this repository. Packing the recorded clips is reproducible.
- `source/hands/` is recorded data (from the retired legacy room builder, whose hand model is
  `source/hand-ref/`); `pack-hands.py --textures ...` checked it repacks the shipped hands byte for byte first.
- Spine and held-box textures are generated at runtime from each game cover; there is no fixed romfs texture binary.
- The hand model is CC BY 4.0 (`source/hand-ref/`); see `CREDITS`.
