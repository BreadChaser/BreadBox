# Round 10 animation checks

The framing and motion checks run the UI mock because that page supplies the R8/R9 scene dependencies and the
headless clip-simulation entry points. Copy the current files from `../animation/` into the mock's `js/` directory,
then run with `ROOM_MOCK` pointing at that mock. Every Chromium run is a heavy job and must follow the coordinator's
heavy-lock and memory rules.

```sh
ROOM_MOCK=/path/to/ui-mock python3 framing.py
ROOM_MOCK=/path/to/ui-mock python3 motion.py 60
```
