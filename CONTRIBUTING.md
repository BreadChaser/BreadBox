# Contributing

Thanks for helping. Bug reports with a log are the most useful thing you can send.

## Reporting a bug

Open an [issue](https://github.com/BreadChaser/BreadBox/issues/new/choose) and fill in the form. We need:

- Your console: Old 3DS, Old 3DS XL, Old 2DS, New 3DS, New 3DS XL or New 2DS XL
- The app name and version (shown in the top corner of the library)
- The game, its region and, if you can, its serial (for example SCUS-94900)
- What happened, what you expected, and how to make it happen again
- `sdmc:/3ds/BreadBox/log.txt`

About the log:

- BreadBox keeps the last five sessions as `log.txt` (newest), `log.1.txt` … `log.4.txt`.
- Copy the right file before you start BreadBox again, or pick the one from the session where it went wrong.
- For a drawing problem, a photo or screenshot of the screen helps a lot.
- Don't send game images, BIOS files or save files you don't own.

## Asking about a game

Check [docs/compatibility.md](docs/compatibility.md) and the [known issues](README.md#known-issues) first.
A game that isn't listed hasn't been tested. Tell us how it ran and we will add it.

## Changing code

- Open an issue first for anything bigger than a small fix.
- The emulator core is in `core/` and is edited directly (it started as PCSX-ReARMed and keeps its GPL license and credits).
- The app is in `app/`, `platform/3ds/` and `include/breadbox/` (emulators in `systems/`). Build steps and tests: [docs/building.md](docs/building.md).
- Run the host tests before you send a change: `make host-tests`.
- Keep a change to one thing, with a message that says what and why.

**Speed on the Old 3DS depends on where the code sits in memory.**
A small, harmless-looking change can move hot code and cost a few percent.

- Only a measurement on a real Old 3DS decides if a speed change is good.
- A PC simulator tells you the output is the same. It can't tell you the speed.
- Measure before, after and before again (A/B/A) in the same scene, and report the three numbers.
- Use the 13 heavy scenes as the primary speed gate. Report the mean and investigate any game more than about 1% slower.

The core layout check (`tools/layout/check.sh`) must pass. It stops a frontend change from moving the core.

## Game settings and widescreen

- Per-game settings live in `assets/romfs/presets/` ([how they work](docs/settings.md)).
- Every value needs a source and a confidence. Say which console, disc and scene you tested.
- Don't copy a setting from another region's disc without testing it.
- Widescreen fixes are described in [docs/widescreen.md](docs/widescreen.md).

## License

BreadBox and PSX3D are GPL-2.0-or-later (GNU GPL version 2, or at your option any later version). By sending code you agree it is released under the same license.
