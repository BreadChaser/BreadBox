# Performance

Speed of BreadBox 0.9.0 on real consoles, one part per system, measured on
hardware in the scenes listed. A dash means that game and console have not been
measured for this release.

**How to read the tables**

- **Speed**: emulated time against real time. 100% is full speed. Rounded to
  whole percents.
- **FPS**: new pictures shown per second, like the in-game FPS counter (drawn
  game pictures, capped by top-screen presentations). Full speed does not mean
  60 FPS: most PlayStation 3D games draw 30 pictures a second at most, and
  Auto frameskip shows fewer pictures to keep the game at speed.
- **—**: not measured yet.

These are measurements of the scenes listed, not guarantees for the whole game. See also the [compatibility list](compatibility.md).

## PlayStation

Old 3DS: build 16642, the 0.9.0 release code (the released package differs only
in its build ID and version number). Each game's heaviest saved scene, one run
per scene and frameskip mode, player default settings, built-in BIOS, the game
loaded from a save state. Each run has a 3-second warmup and a 12-second sample
(Need for Speed: 1 + 5 seconds, its demo race is short). Racing games use their
attract-mode races. Speed is the guest frame rate divided by 60.

| Game | Scene | Old 3DS, frameskip off | Old 3DS, Auto frameskip | New 3DS, frameskip off | New 3DS, Auto frameskip |
|---|---|---:|---:|---:|---:|
| Symphony of the Night | Prologue, Dracula fight | 100% / 60.7 | 100% / 56.8 | — | — |
| Final Fantasy VII | Battle | 100% / 15.2 | 100% / 15.2 | — | — |
| Klonoa | Heaviest scene | 93% / 56.5 | 100% / 23.0 | — | — |
| Crash Bandicoot | Heaviest scene | 92% / 22.2 | 98% / 17.9 | — | — |
| Crash Bandicoot 2 | Heaviest scene | 88% / 26.7 | 99% / 18.0 | — | — |
| Crash Team Racing | Attract-mode race | 84% / 24.8 | 96% / 15.2 | — | — |
| Ridge Racer Type 4 | Attract-mode race | 89% / 26.9 | 95% / 20.4 | — | — |
| Tekken 3 | Heaviest scene | 73% / 44.1 | 98% / 15.2 | — | — |
| Silent Hill | Garage | 90% / 21.8 | 100% / 12.1 | — | — |
| Silent Hill | Alley | 79% / 23.9 | 98% / 12.0 | — | — |
| Silent Hill | Café | 82% / 19.3 | 96% / 12.1 | — | — |
| Gran Turismo 2 (Arcade) | Attract-mode replay race | 61% / 15.6 | 64% / 12.1 | — | — |
| Need for Speed: High Stakes | Demo race | 38% / 12.2 | 38% / 11.0 | — | — |

The New 3DS has not been measured game by game for this release.

## Game Boy Advance

Old 3DS, one run per scene, read from the per-second log, Frame blend Off (the
default). Kirby, Mario Kart and Excitebike were measured on build 16642, the
release code, over about 55 seconds each. The other rows come from 60-second
runs on build 16640, an earlier release candidate; the release later changed
how Game Boy Advance frame pacing handles games that draw every other frame.
The intro and title rows are not gameplay, and the Star Wars flight ended in
its score screen during the minute. Game Boy Advance has no frameskip setting;
a late frame can skip its picture while the game keeps full speed.

| Game | Scene | Old 3DS | New 3DS |
|---|---|---:|---:|
| Kirby: Nightmare in Dream Land | Gameplay | 100% / 60.0 | — |
| Pokémon Emerald | Intro | 100% / 60.0 | — |
| Ms. Pac-Man: Maze Madness | Intro | 100% / 60.0 | — |
| Mario Kart: Super Circuit | Race | 100% / 37.5 | — |
| Classic NES Series: Excitebike | Race | 100% / 30.0 | — |
| Star Wars: Flight of the Falcon | In flight, then score screen | 98% / 28.3 | — |
| Game Boy Advance Video: Disney Channel Collection Vol. 1 | Episode playing | 92% / 52.0 | — |
| Golden Sun | Intro and name entry | 92% / 23.9 | — |
| Mario Golf: Advance Tour | Town | 80% / 19.7 | — |
| Doom | First level (E1M1) | 60% / 17.3 | — |

The New 3DS has not been measured game by game for this release.

## Checking a change

Use the same scenes, settings, warmup and network conditions, and measure a
baseline, the candidate and the baseline again (A/B/A). Compare against the mean
of both baseline runs and report how far they drift apart. For PlayStation, the
13 scenes above are the main speed check: report the mean and look into any game
more than about 1% slower. A PC simulator can show that the output is the same;
its timing cannot predict the Old 3DS, whose speed depends on where code sits in
memory.
