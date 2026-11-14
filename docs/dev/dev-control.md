# Developer controls

Developer remote control is an opt-in developer facility and is off by default.
Enable it in `switches.txt` with `remote token=<secret>`; both the `remote`
switch and a nonempty token are required. For BreadBox, the file belongs at
`sdmc:/3ds/BreadBox/switches.txt`. For compatibility with existing PSX3D
developer setups, BreadBox also reads `sdmc:/3ds/PSX3D/switches.txt` when its
own file is absent.

Remote control is intended for a trusted local area network. It does not use
TLS, so do not expose it to the public internet. The optional `update=<URL>`
command accepts a LAN URL only; public hosts and addresses are refused.

## Recording and exact replay

The developer recorder saves a starting guest state and frame-numbered PS1
controller inputs. It is opt-in and separate from normal save slots. Enable it
with `record` in `sdmc:/3ds/BreadBox/switches.txt`; `record_seconds=N` sets the clip
length (default 15 seconds). The pause menu then offers **Record test clip**.
The remote server also accepts `record on`/`record off` in the library and `record start` in game.

Clips live under `sdmc:/3ds/BreadBox/recordings/<Game>/<number>.state` and `.inp`.
Each new recording reserves a fresh number. Fetch them with the tracked client:

```sh
export PSX3D_TOKEN=choose-a-private-token
python3 tools/input-replay/fetch.py old Crash_Bandicoot --id 0 --out /tmp/psx3d-clips
python3 tools/input-replay/psx3dctl.py old load Crash_Bandicoot replay=0 frameskip=off
```

`recordings <title>` lists clips; `get recordings/<title>/<file>` retrieves
a clip file. `replay=N` works in `load` and batch lines and cannot be combined
with `state=N`. Check the requested clip number before replaying. Replay ignores
live gameplay inputs and checks guest state every 60 frames; divergence stops
the test at the first mismatching checkpoint. Replay does not write the protected
benchmark save slots.

Recording and replay disable the SPU thread and CD read-ahead for deterministic
execution. Their speed differs from ordinary gameplay; use them for relative
comparisons with identical recording settings. A clip includes private game
state and should be handled like a save state, not distributed with the emulator.

`tools/input-replay/check.sh` tests the codec under ASan/UBSan without a console.
`tools/input-replay/build-sim.sh` builds a private ARM simulator from this source
checkout using the local host support directory. Its runner accepts `CLIP_RECORD`
or `CLIP_REPLAY` as a file basename; `CLIP_INPUT=1` supplies scripted test inputs.
Run a recording and repeated replays with the same options and check
`first_divergent=0`. The input file has a versioned header, per-frame controller
records and checkpoint hashes; malformed or truncated files are rejected.
