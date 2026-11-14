# Contributing

Thanks for helping. Bug reports with a log are the most useful thing you can send.

## Reporting a bug

Open an [issue](https://github.com/BreadChaser/BreadBox/issues/new/choose) and fill in the form. We need:

- Your console: Old 3DS, Old 3DS XL, Old 2DS, New 3DS, New 3DS XL or New 2DS XL
- The BreadBox version (shown on the Settings card in the room, and in Settings → About → Credits)
- The system (PlayStation or Game Boy Advance), the game and its region
- If you can, the PlayStation disc serial (for example SCUS-94900) or the Game Boy Advance game code (for example AMKE)
- What happened, what you expected, and how to make it happen again
- `sdmc:/3ds/BreadBox/log.txt`

About the log:

- BreadBox keeps the last five sessions as `log.txt` (newest), `log.1.txt` … `log.4.txt`.
- Copy the right file before you start BreadBox again, or pick the one from the session where it went wrong.
- For a drawing problem, a photo or screenshot of the screen helps a lot.
- Don't send game images, BIOS files or save files you don't own.

## Asking about a game

Check [docs/compatibility.md](docs/compatibility.md) and the [known issues](README.md#known-issues) first.
A game that isn't listed hasn't been tested. Tell us how it ran with the **Game report** form and we will add it.

## Changing code

- Open an issue first for anything bigger than a small fix.
- The app is in `app/`, `platform/3ds/` and `include/breadbox/`. Each system lives in `systems/<id>/`.
- The PlayStation emulator is in `core/` and the Game Boy Advance emulator in `gba-core/`. Both are edited directly; they started as PCSX-ReARMed and gpSP and keep their GPL licenses and credits.
- [AGENTS.md](AGENTS.md) has a one-page map of the code. Build steps and tests: [docs/building.md](docs/building.md). Developer docs: [docs/dev/](docs/dev/README.md).
- Run the host tests before you send a change: `make host-tests`.
- Keep a change to one thing, with a message that says what and why.
- Code shared by all systems holds no system-specific rules; those go in the system's own folder ([system layer](docs/dev/system-layer.md)).
- Menu text is American English. Screens use the shared components in `app/ui/components/` and the style tokens in `include/breadbox/`; `tests/ui_consistency` checks this.

**Speed on the Old 3DS depends on where the code sits in memory.**
A small, harmless-looking change can move hot code and cost a few percent.

- Only a measurement on a real Old 3DS decides if a speed change is good.
- A PC simulator tells you the output is the same. It can't tell you the speed.
- Measure before, after and before again (A/B/A) in the same scene, and report the three numbers.
- For PlayStation, use the 13 heavy scenes as the main speed check. Report the mean and look into any game more than about 1% slower.

The layout checks (`tools/layout/check.sh` and `tools/layout/baseline-check.sh`) must pass.
They stop a change from moving the emulator cores and the code that runs every frame.

## Game settings and widescreen

- Per-game PlayStation settings live in `assets/romfs/presets/` ([how they work](docs/settings.md)).
- Every value needs a source and a confidence. Say which console, disc and scene you tested.
- Don't copy a setting from another region's disc without testing it.
- Widescreen fixes are described in [docs/widescreen.md](docs/widescreen.md).

## License

BreadBox is GPL-2.0-or-later (GNU GPL version 2, or at your option any later version).
By sending code you agree it is released under the same license.
