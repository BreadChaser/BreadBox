# Getting started

Where to put your games and what BreadBox keeps on the SD card. Install steps are in the [README](../README.md#install).

**Games.** Put all your games under one folder, such as `sdmc:/roms/`. Subfolders
are fine, and so is a mix of systems in one folder. BreadBox looks in common
places by itself; to use another folder, pick it in **Settings → Collection →
Games folder**. Each start shows your Collection at once and checks the card for
new, removed and renamed games in the background. Hold **Y** at the bookcase to
check right away.

| System | Files |
|---|---|
| PlayStation | `.cue/.bin`, `.chd`, `.pbp`, `.iso`, `.img`, `.mdf`, and `.m3u` for multi-disc games |
| Game Boy Advance | `.gba`, and `.zip` with exactly one game inside (extract `.7z` first; rename `.agb` files to `.gba`) |

Keep `.cue` files next to the tracks they name. A zipped Game Boy Advance game is
unpacked when you play it, so leave about 32 MB free on the SD card. Games over
32 MB don't start.

**BIOS.** Not needed. If you have a real BIOS, copy it to `sdmc:/3ds/BreadBox/bios/`
and BreadBox uses it: for PlayStation `scph5500.bin`, `scph5501.bin`,
`scph5502.bin`, `scph1001.bin`, `scph7001.bin`, `scph101.bin` or
`psxonpsp660.bin`; for Game Boy Advance `gba_bios.bin`.

**Box art** downloads automatically, matched by file name, disc serial or
cartridge code. To use your own picture, add a PNG named like the game file
(up to 4096 pixels wide and high; it is scaled to fit): for PlayStation in `sdmc:/3ds/BreadBox/covers/` (for multi-disc
games, name it after disc 1), for Game Boy Advance next to the game or in
`sdmc:/3ds/BreadBox/covers/gba/`. Your picture wins over the download.

**Your files** live in `sdmc:/3ds/BreadBox/`:

| Folder or file | What it holds |
|---|---|
| `saves/` | Memory cards and Game Boy Advance battery saves |
| `states/` | Postcards (save states) and Continue |
| `backups/` | Save backups |
| `config.ini`, `games/` | Your settings |
| `covers/` | Downloaded box art |
| `bios/` | Optional BIOS files |
| `log.txt` | What happened last session (older ones: `log.1.txt` to `log.4.txt`) |

Back up this folder before you install a test build.

## Moving from PSX3D

On its first start, BreadBox copies from `sdmc:/3ds/PSX3D/` your memory cards,
postcards (save states and Continue) with their pictures, per-game settings,
cheats, BIOS files and play history. If BreadBox has no settings of its own yet,
it also takes PSX3D's app settings. A notice says how many files came over.

It is a one-way copy: PSX3D's files are never changed, and what you play in
BreadBox does not go back to PSX3D. Both apps can stay installed. PSX3D numbered
its save slots from 0 and BreadBox numbers postcards from 1, so PSX3D slot 0 is
postcard 1. **Settings → Collection → Import again from PSX3D** copies anything
new. Details: [Moving from PSX3D](moving-from-psx3d.md).

## Updates

BreadBox checks for a new version from the room when it starts, about once a
day while it sits idle, and when the 3DS wakes up if the last check was over an
hour ago. It never checks during a
game. With **Automatic** (the default), a short countdown runs before the update
installs, and any button puts it off. **Settings → Updates** switches to
**Ask first** or **Off**, and **Check now** looks right away. The tested
settings for PlayStation games also update by themselves from the room.
