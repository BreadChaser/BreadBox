# BreadBox

BreadBox is a multi-system emulator for Nintendo 3DS. Version 0.9.0, the public beta, supports
PlayStation and Game Boy Advance, with experimental NES support available in
development builds. BreadBox grew from PSX3D and is the maintained app. It uses title ID
`000400000B0B0B00` and stores app data in `sdmc:/3ds/BreadBox`.

Coming from PSX3D: on first start, BreadBox copies from `sdmc:/3ds/PSX3D` your
memory cards, postcards (save states and Continue) with their pictures,
per-game settings, cheats (the ones you switched on and your own codes), BIOS
files, play history and, when BreadBox has no settings of its own yet, PSX3D's
app settings. A notice says how many files came over. It is a one-way copy:
PSX3D's files are never changed, and what you play in BreadBox does not go back
to PSX3D. PSX3D numbered postcards from 0; BreadBox numbers them from 1, so
PSX3D slot 0 is slot 1. Settings > Collection > Import again from PSX3D copies
anything new.

Each system has its own core and renderer:

| System | Core | Renderer | Status |
|---|---|---|---|
| PS1 | vendored, modified [PCSX-ReARMed](https://github.com/libretro/pcsx_rearmed) in `core/` | PICA GPU renderer (`gpu_pica`) | Mature; tested on Old and New 3DS hardware. Real 3D is experimental. |
| GBA | vendored, modified gpSP in `gba-core/` | its own CPU renderer, picture shown as a texture; it does not use the PS1 core or GPU renderer | Tested on Old and New 3DS hardware; some demanding games run below full speed on Old 3DS. The experimental GBA GPU compositor is shelved. |
| NES | QuickNES through the shared libretro host in `systems/nes/` | CPU | Experimental development option; not part of the standard release. |

## Build

Use the pinned devkitPro toolchain described in [building](docs/building.md).

```sh
make                # build/3ds/BreadBox.3dsx (PS1 and GBA)
make cia            # installable CIA; requires bannertool and fixed makerom
make host-tests
```

Install the CIA with FBI on a console running Luma3DS. The `.3dsx` is also
available for Homebrew Launcher. The retired PSX3D app (`000400000C3D3D00`) can
stay installed beside BreadBox. Do not install a test build over another app.

## Games and controls

BreadBox finds common ROM folders automatically. Use one root, such as
`sdmc:/roms`, for every system; subfolders such as `PS1` and
`Nintendo - Game Boy Advance` work, as do mixed folders. Settings > Collection > Games folder
lets you choose a different root. A shared index fills in the background and is
cached on the SD card. Every start shows your Collection at once and checks the card
for new, removed and renamed games in the background; hold Y in your Collection to
check right away.

GBA accepts `.gba` and ZIP files containing exactly one `.gba` game with a valid
header. Extract `.7z` archives first. ZIPs are extracted only when played, to a
single cache file using bounded memory; allow up to 32 MiB of free SD space.
Saves and states follow the original game filename. The list shows scan progress
and supports Back while scanning.

PS1 accepts CUE/BIN, CHD, PBP, ISO, IMG, MDF, and M3U. Keep CUE files
with their referenced tracks. No games or proprietary BIOS files are included;
the built-in HLE BIOS is the default. An optional BIOS goes in
`sdmc:/3ds/BreadBox/bios`.

BreadBox checks for game-setting updates automatically while you are in the
room, using Wi-Fi. It also checks for BreadBox app updates from the room. The
default update mode is Automatic: after a short idle countdown, an update
installs; any button postpones it. Choose Settings > Updates to set
Automatic, Ask first, or Off. The main menu opens your
collection, settings, and the most recent game. Use the D-pad and A to select; B
goes back. The touch screen provides menu controls and additional PS1 buttons.

Settings > Systems opened from the room apply to all games in that system. Settings opened from
a game apply to that game and override the system values; reset or inherit a
value to return to the system setting. PS1 and GBA each have their own settings.

App data stays under `sdmc:/3ds/BreadBox`: `bios/`, `config/`, `cache/`,
`covers/`, `saves/`, `states/`, `backups/`, and `updates/` hold BIOS files,
settings, cached data, covers, game saves, save states, recovery copies, and
update staging. ROM folders can be anywhere on the SD card and are selected in
Settings > Collection > Games folder. Back up the app folder before installing a test build.

See [PSX3D save import](docs/psx3d-save-import.md) and [automatic updates](docs/auto-update.md)
for details.

## Known issues

Some demanding games run slowly on Old 3DS. Experimental Real 3D and some GPU
effects can have visual issues. See the [compatibility list](docs/compatibility.md)
for tested games and current notes. [Box art](docs/box-art.md) and
[widescreen](docs/widescreen.md) describe the PS1 features.

## Contributing and license

See [CONTRIBUTING.md](CONTRIBUTING.md) and [the build and test guide](docs/building.md).
BreadBox and the frozen PSX3D are GPL-2.0-or-later; retain
[LICENSE](LICENSE), [CREDITS](CREDITS), and the core and dependency notices when
redistributing sources or binaries. The core is maintained directly in `core/`.
