# Settings

How BreadBox's settings are organized, where a value comes from, and what a
contributor must update when adding or changing one. Options with one right
value per console are fixed in code; settings exist for taste and comfort, and
technical PlayStation options sit on an Advanced page.

## Pages

One list per page, no tab bar; each system has its own page. `app/settings/settings_ui.c` owns the pages; adapters own the rows.

```
Settings          Customize >  Systems >  Collection >  Updates >  About >
  Customize       Room style (Left/Right relights the room; A opens the styles) | Hands > (dev switch roomhands)
                  | FF keys | Rewind keys (New) | Touch buttons (New: their side)  <- the shared shortcuts. one home
  Systems         one row per registered system with settings (PlayStation, Game Boy Advance; NES when built)
    <system>      its rows under red headings (Picture, Controls, Saves, Cartridge, Cheats), then More: Advanced >, Help
      Advanced    the rows the adapter marks advanced (PS1: Speed, Compatibility, Troubleshooting), Help
  Collection      Games folder | Import again from PSX3D
  Updates         the update page (Automatic / Ask first / Off, check now)
  About           Credits | How to install | Welcome (when the welcome tour is built in)
```

- A game's own Settings (the shelf's X, pause > Settings) open straight on its system list, titled with the
  game; Advanced is one level below it. In a running game the list does not repeat what the pause card owns:
  Cheats (both systems) and GBA's Button mappings.
- B goes up one level (from the top list or a game's list it leaves Settings); tapping the title does the same.
  Each page reopens on the row it was left on. L / R jump to the previous / next section on a system's list
  (L inside a section first goes to its first row); they do not wrap. START opens the focused row's help.
- Y resets only where a scope is: Customize (room style, hands, the shared shortcuts), a system's pages (that
  system, or this game). X restores the focused value (Inherit in a game). Reset controls stays on Button mappings.
- The card's title is the page ("Settings", "Customize", "PlayStation", "Advanced"); the room's oscilloscope
  shows where you are ("PlayStation · Picture": on a system's list, the focused row's section).
- The selected row is marked by the shared highlighter stroke (`ui_mark.h`), on every notebook card and the
  room's lists. No mark for a System value; Changed (pencil), Recommended and All systems keep theirs.

## Where a value comes from

- **Taste** (`layer = taste` in the menu rows below): `defaults < preset < user all-games < user per-game`.
  Presets supply taste values only as starting points (touch bindings).
- **Technical** (`layer = tech`): `defaults < user all-games < preset < user per-game`.
  A game's tested value beats an all-games choice. The Advanced page header says so. A per-game change beats the preset and is marked Changed.
- **Forced** values are not layered. Only the developer `o:` switch can override
  them, per load.

An explicit game choice stays **Changed** even when its value equals the system value; later system edits do not replace that game choice. X Inherit removes that field's override. The focused help identifies its source.

Bottom-screen button side and speed shortcuts are **All systems** preferences. Reset PlayStation defaults and Reset controls preserve them. Shared shortcut edits preserve older per-game shortcut exceptions; the focused card shows **Game exception: …** when that game will still use its own shortcut. Reset this game removes the exception.

Room style is a single choice for the whole app (Day or Night), with no per-game
value.

## PlayStation rows

- **Screen.** Original, Stretch, Widescreen. In a game,
  Widescreen is listed only when the game has a revision-matched widescreen
  bundle and its preset is not `hidden` (2D games, no fix). The preset
  `widescreen` key decides how it is offered:
  - `on`: offered; follows the all-games Screen choice.
  - `warn`: offered with the note "Widescreen can lower the speed on Old 3DS.";
    follows the all-games choice.
  - `off`: offered with the note "Widescreen lowers the speed a lot on Old 3DS.";
    the all-games Widescreen choice does **not** apply. Only an explicit
    per-game choice turns it on.
  - `hidden`: not offered (it cannot work).
  In the library, Widescreen means "use widescreen in games that have a fix".
- **3D** (New 3DS only; per game like the other Display rows): Screen depth
  (default) or Real 3D (experimental). The slider still turns 3D
  on and off and sets the strength in both. Tip: "Real 3D is experimental:
  slower in busy games." The value reads "Real 3D (experimental)". Old 3DS has no row: this build draws no game 3D there (`stereo` is
  forced off on Old). Screen depth would be the cheaper style on Old, but it
  still draws a second top picture every frame and has never been speed-gated
  on the Old, so it is not offered.

- **FPS counter.** Off, FPS or Detailed (game speed and pictures per second).
- **Controller.** Digital or Analog DualShock.
  Games that switch the pad themselves still do. The preset key `controller`
  sets the default for games that support analog but do not switch the pad.
  Unlike other control rows it uses the technical layer order (see Where a value comes from), because
  the right value depends on the game. Touch L3/R3 and the Old touch stick
  continue to follow analog mode.
- **Bottom-screen buttons** (New only): Right side or Left side.
- **Memory cards.** Per game, per disc, shared or off. A change takes effect on the next game start.
- **Rewind** (New only, game only). It needs free memory
  and shows "Unavailable" when that runs out. The preset key `rewind` keeps
  only its Old 3DS meaning (impossible there). On New it is a plain choice,
  default **On**.
- **Frameskip.** Off or Auto; Auto keeps
  the 15 pictures/s floor.
- **Game CPU speed.** Auto = 57 (the real PS1). New 3DS presets set measured
  values (CTR 75, GT2 Arcade 80, SH 100, NFS 80). Help text: "Higher can raise
  the picture rate on New 3DS; on Old 3DS any change usually slows the game."
- **Compatibility** group. These are PCSX's hacks and accuracy switches, ordered
  from "usually harmless" to "experts only". Each keeps its existing help text
  and impact line (options.c `notes[]`). Their defaults are the general correct
  value per console. Presets set them per game only after a test on that console
  shows a gain (see Defaults and presets).

## Defaults and presets

`assets/romfs/presets/defaults.ini` holds every PlayStation setting and every
fixed (forced) value, per console. Each released `defaults_version` is kept
unchanged in `tools/presets/defaults-history/<N>.ini`, and `tools/presets/check.py`
requires `defaults.ini` to equal the history file of its version, so changing a
default means a new version and a new history file.

- A preset file gets `tested_with = <N>` per console, in `[old]` / `[new]` or
  in common for both. It means: "the values in this file were tested on this
  console with defaults N; every option it doesn't set was at its defaults-N
  value."
- `tested_with` also drives the Advanced header. The legacy `curated`
  field is retired: the checker warns about it and the runtime ignores it.
- Presets stay sparse. The checker's existing rule still applies: no key equal
  to the current default, unless it pins (below).

The build check (`tools/presets/check.py`, run by `make` and CI): for every preset file `P` and console `C` with `tested_with = N` while the
current version is `M > N`:

1. Diff `defaults-history/N.ini` against `M.ini` for console `C`, including
   `[forced.C]`.
2. For each **technical** option that changed: `P` must either set it
   explicitly in `C` (a *pin*, allowed to equal the old default and marked
   `<key>.source = pinned at defaults N`), or `P` must be retested, meaning
   `tested_with` is raised to `M`. Otherwise the check **fails** and names the
   file, console, option, and old → new value.
3. A changed **forced** value cannot be pinned. Every preset on that console
   must be retested (`tested_with = M`). This is deliberately strict, because
   forced changes affect every game.
4. A changed **taste** default never fails: presets make no taste claims.

The check also fails when:
- `tested_with` is greater than `defaults_version`, or no history file exists
  for it;
- `defaults.ini` keys do not exactly match the menu-row keys plus the forced list;
- a history file changed after it was released (compared with the last
  release tag, like the pack VERSION rule).

Bumping `tested_with` also requires the normal per-file `version` and pack
`VERSION` bump. Test fixtures (`tests/presets/`) cover: a changed default with
a pinned preset (pass), unpinned (fail), retested (pass), a changed forced
value (fail until retested), and a taste change (pass).

## Forced and removed options

### Core options (`pcsx_rearmed_*`)

| Key | Old default | New default | Fate | Why |
|---|---|---|---|---|
| region | auto | auto | **Force** auto | The disc decides. Forcing NTSC on a PAL disc is a timing hack, not a choice we curate. |
| bios | auto | auto | **Force** auto | HLE is primary. Auto already uses a real BIOS file when one exists, and no player needs to override that. |
| show_bios_bootlogo | disabled | disabled | **Force** disabled | Boot logo only exists with a real BIOS; it delays start. |
| memcard1 | libretro | libretro | **Setting** (Saves) | User's taste: per-game, per-disc or shared saves. |
| memcard2 | shared | shared | **Setting** (Saves) | as above |
| cd_readahead | forced 256 (0 under HBL) | 256 | **Force** 256 both (0 under HBL on Old) | Old: CTR CD reads 1.3→0.35 ms/frame. |
| drc | enabled (auto via Luma) | same | **Force** | Chosen by SVC access. The slow-mode notice explains when it is off. |
| drc_thread | disabled | disabled | **Force** | already forced (`locked[]`) |
| psxclock | auto | auto | **Setting** (Advanced) | Real per-game value (New overclock presets). |
| dithering | enabled | enabled | **Force** enabled | cost about 0 measured; Off bands SH fog. "Always" (force) also goes. |
| gpu_thread_rendering | forced disabled | forced enabled | **Force/console** (unchanged) | Old: syscore 30% slices made drawing wait (25 vs 36 fps). New: GPU worker +2.7–3.0% in SH, CTR, R4 (Crash even). |
| frameskip_type | auto | auto | **Setting** (Advanced, "Frameskip") | Real per-game choice; Off/Auto only. |
| frameskip_threshold | 33 | 33 | **Remove** (already retired) | Off/Auto only |
| frameskip_interval | 2 | 2 | **Force** (internal) | Used only for fast-forward / legacy policy |
| display_fps_v2 | disabled | disabled | **Force** | PSX3D has its own counter |
| display_info | disabled | disabled | **Dev** (switch `debug`) | Troubleshooting only |
| fractional_framerate | auto | auto | **Force** | Frontend paces frames |
| alt_flip | auto | auto | **Setting** (Compatibility) | Per-game fix |
| rgb32_output | disabled | disabled | **Force** | Frontend draws RGB565 |
| scale_hires | enabled | enabled | **Force** | 240-line screen |
| gpu_slow_llists | auto | auto | **Setting** (Compatibility) | Per-game fix |
| show_overscan | disabled | disabled | **Force** disabled | Overscan would not fit the 400-px screen with Original framing. |
| screen_centering, _x, _y, _h_adj | auto | auto | **Force** auto | The game's own display position (gpulib "auto": pictures within 8 px of the middle are centered). `systems/ps1/display.c` shows the field like a TV: short pictures get black borders, 480i is two lines per row, PAL shows a 240-line window. Same in every screen mode. |
| neon_*, gpu_peops_*, gpu_unai_* | — | — | **Remove** (not built) | No such renderers on 3DS (`hidden_prefix`); the unai prefix is a leftover from the Software renderer removal. |
| show_gpu_peops_settings, show_input_settings | disabled | disabled | **Force** | RetroArch menu folding |
| spu_reverb | disabled | forced enabled | **Force** On, both | The PS1 has it. On Old 3DS it costs about 0.8% game speed on average in the heavy scenes. |
| spu_interpolation | forced simple | forced gaussian | **Force/console** (unchanged) | Gaussian costs real time at 268 MHz. |
| nocdaudio | enabled (music on) | enabled | **Force** on | Off just removes music (dynarec-speed2 survey: removes audio, no real gain). |
| noxadecoding | enabled | enabled | **Force** on | Off removes voices/music; same survey. |
| spu_thread | forced enabled (syscore granted) / disabled under HBL | forced disabled | **Force/console** (unchanged) | Old GT2 27.0→27.8, Crash +3%; New: it would share core 2 with the GPU. |
| analog_axis_modifier | square | square | **Setting** (Controls) | Feel of the stick: taste. |
| vibration | — | — | **Force** off | No rumble motor |
| analog_combo | l1+r1+select | l1+r1+select | **Setting** (Controls) | Taste; some games use the default combo. |
| multitap | disabled | disabled | **Force** | One player per 3DS |
| negcon_*, input_sensitivity, crosshair*, konamigun*, guncon* | core defaults | same | **Force** | No such controllers or lightguns on 3DS |
| icache_emulation | enabled | enabled | **Setting** (Compatibility) | Some games need it; some run faster without |
| exception_emulation | disabled | disabled | **Setting** (Compatibility) | as above |
| 3ds_appcputime | syscore % (30) | 35 (unused) | **Force/console** (unchanged) | `MaxCpu` 30%: 50/80% starve the system |
| cd_turbo | disabled | disabled | **Setting** (Compatibility) | Per-game: Crash 1 loads faster; NFS −7% |
| nocompathacks | disabled | disabled | **Setting** (Compatibility) | Experts only |
| nolddelay | disabled | disabled | **Setting** (Compatibility) | Tekken gain now exact in the dynarec; keep for odd games |
| nosmccheck | disabled | disabled | **Setting** (Compatibility) | Breaks SH boot |
| gteregsunneeded | **enabled** (o3ds table) | disabled | **Setting** (Compatibility), per-console default | Measured faster on Old 3DS; the two work together |
| nogteflags | **enabled** | disabled | **Setting** (Compatibility), per-console default | as above |
| nostalls | disabled | disabled | **Setting** (Compatibility) | Per-game trade: CTR +2.2/+6.3%, R4 +2.1%, NFS −7%, SH garage −4% |
| psx3d_widescreen | 0 | 0 | **Force** (internal) | Widescreen only through bundles |
| psx3d_stereo | disabled | slider up in a game | **Force** (internal) |  |

Saved values for forced or removed options:

- `config.ini` `core.<key>` and `games/*.ini` `o.<key>` entries for forced or
  removed options are **ignored** on load and dropped on the next save (the
  sparse save never writes them). A player who had set "Disc music Off" gets
  music back. This is intended.
- `show_all_settings`, `audio_stretch`, `fast_forward` / `preset_fast_forward`,
  `stereo`, `sounds`, `music`, the all-games `psx3d_auto_floor` and per-game
  `o.auto_floor` are dropped the same way. The per-game `stereo`,
  `audio_stretch` and `fast_forward` lines in `games/*.ini` are ignored.
- A per-game `preset_widescreen=1` (written by the old Show-all override)
  becomes an explicit per-game Screen = Widescreen.
- Old 3DS players who set `gteregsunneeded`/`nogteflags` keep their choice:
  those remain settings.
- A migrated file is rewritten only when a setting changes.

Developer switches (`switches.txt`, remote `load`/batch): these stay developer-only. They never appear in the menu and are never saved
(devswitch.c restores them before local play):
`remote token=`, `stats`, `noskip`, `frameskip=`, `fs1`, `fs3`, `clock=`, `cdra=`, `state=`,
`slider=`, `stereo=`, `pmu=`, `gldbg=`, `vramdump=`, `autosavetest=`, `record`/`record_seconds=`,
`replay=`, `nosputhread`, `nogputhread`, `acap`, `prof`, `aa`, `sbs`, `showright`, `gpu_async`,
`gpu_flush`, `pmuphase`, `pmuclass`, `rtfs*`, `ff`, `floataudio`, `frontend`, `fsmart`,
`wsbundle`/`wsscreen=`, `oldmode`, `o:<core option>=<value>` (any core option per load,
including forced ones, for A/B tests), and `o:auto_floor=N`.

Rule: a forced option may still be overridden **per load** with `o:` for tests.
That keeps the A/B workflow intact and is invisible to players.

## Tests

`bash tests/settings/spec.sh` (part of the host suite) builds the PlayStation
settings model with the core's real option table and the real `defaults.ini`,
walks it in eight contexts (Old and New × library, game without tested settings,
game with tested settings, game with `widescreen = off`) and compares the rows
with the blocks below. It also fails when the core registers an option that has
no row in the core options table, when a forced key appears in the menu, or when
`defaults.ini` disagrees with the forced block.

The menu code it walks is a frozen copy of the earlier PlayStation settings menu
(`tests/settings/fixtures/v1-r2/`), so the blocks below describe that model: its
page names (Display, Controls, Saves, Advanced) are the sections of PlayStation's
list today. The current pages, layout and navigation described at the top of this
page are not covered by this test.

Row format: `page | label | key | values (first = shown order) | Old default | New default | consoles | scope | layer`.
`consoles`: both/old/new. `scope`: all = library and game; lib = library only;
game = game only. `layer`: taste or tech (see Where a value comes from). Values use the labels the
player sees.

```settings-menu
Display | Screen | screen | Original, Stretch, Widescreen | Original | Original | both | all | taste
Display | Smoothing | smooth | On, Off | On | On | both | all | taste
Display | FPS counter | show_fps | Off, FPS, Detailed | Off | Off | both | all | taste
Display | 3D | stereo_style | Real 3D (experimental), Screen depth | - | Screen depth | new | all | taste
Display | Theme | theme | - | Day | Day | both | lib | taste
Controls | Button mappings | > | - | - | - | both | all | taste
Controls | Controller | controller | Digital, Analog | Digital | Digital | both | all | tech
Controls | Analog | analog_combo | Off, L1+R1+Select, L1+R1+Start, L1+R1+L3, L1+R1+R3, L3+R3 | L1+R1+Select | L1+R1+Select | both | all | taste
Controls | Stick response | analog_axis_modifier | Square, Round | Square | Square | both | all | taste
Controls | Bottom-screen buttons | touch_click_left | Right side, Left side | - | Right side | new | all | taste
Saves | Memory card 1 | memcard1 | Per game, Per disc, Shared, Off | Per game | Per game | both | all | taste
Saves | Memory card 2 | memcard2 | Per disc, Shared, Off | Shared | Shared | both | all | taste
Saves | Rewind | rewind | Off, On | - | On | new | game | taste
Advanced | Frameskip | frameskip | Off, Auto | Auto | Auto | both | all | tech
Advanced | Game CPU speed | psxclock | Auto, 30 … 100 | Auto | Auto | both | all | tech
Advanced/Compatibility | Flicker fix | alt_flip | Auto, Early, Late | Auto | Auto | both | all | tech
Advanced/Compatibility | Safe drawing | gpu_slow_llists | Auto, Off, On | Auto | Auto | both | all | tech
Advanced/Compatibility | Faster disc loading | cd_turbo | Off, On | Off | Off | both | all | tech
Advanced/Compatibility | Simpler CPU timing | nostalls | Off, On | Off | Off | both | all | tech
Advanced/Compatibility | Simplify CPU loads | nolddelay | Off, On | Off | Off | both | all | tech
Advanced/Compatibility | Faster 3D calculations | gteregsunneeded | Off, On | On | Off | both | all | tech
Advanced/Compatibility | Simpler 3D clipping | nogteflags | Off, On | On | Off | both | all | tech
Advanced/Compatibility | Skip code safety checks | nosmccheck | Off, On | Off | Off | both | all | tech
Advanced/Compatibility | Accurate CPU cache | icache_emulation | On, Off | On | On | both | all | tech
Advanced/Compatibility | Accurate CPU errors | exception_emulation | Off, On | Off | Off | both | all | tech
Advanced/Compatibility | Disable automatic game fixes | nocompathacks | Off, On | Off | Off | both | all | tech
Advanced | Reset to recommended | - | - | - | - | both | tested | tech
```

Values are compared as a cycle (the order Left/Right steps through them). In a
game without a widescreen fix, Screen offers only Original and Stretch. Scope
`tested`: only in a game with tested settings on this console.

Top level of the frozen model:

```settings-home
library | Display, Controls, Saves, Advanced, Games folder, Reset all settings
game | Display, Controls, Saves, Advanced, Reset this game
```

Advanced page header lines (exact text):

| Context | Text |
|---|---|
| In a game **with** tested settings on this console | **"This game has tested settings. Changing them can slow it down or break it."** Rows set by the preset show `P`. Bottom row: **Reset to recommended**. |
| In a game **without** tested settings on this console | Small line: **"This game hasn't been tested yet; these are the standard settings."** No warning and no Reset to recommended row. |
| Library (all games) | Small line: **"Games with tested settings keep their tested values for these options."** |

**Reset to recommended** (with confirmation) removes this game's overrides of
Advanced options only.

Forced values per console (must equal `[forced.*]` in `defaults.ini`):

```settings-forced
sounds | on | on
music | on | on
stereo | off | real
region | auto | auto
bios | auto | auto
show_bios_bootlogo | off | off
cd_readahead | 256 | 256
drc | on | on
drc_thread | off | off
dithering | on | on
gpu_thread_rendering | off | on
frameskip_interval | 2 | 2
frameskip_threshold | 33 | 33
display_fps_v2 | off | off
fractional_framerate | auto | auto
rgb32_output | off | off
scale_hires | on | on
show_overscan | off | off
screen_centering | auto | auto
show_input_settings | off | off
spu_reverb | on | on
spu_interpolation | simple | gaussian
nocdaudio | on | on
noxadecoding | on | on
spu_thread | on | off
vibration | on | on
multitap | off | off
3ds_appcputime | 30 | 35
audio_stretch | on | on
fast_forward | on | on
auto_floor | 15 | 15
```

Room styles:

```settings-themes
Day
Night
```
