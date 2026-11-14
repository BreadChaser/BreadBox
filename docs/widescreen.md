# Widescreen

Setting **Screen** to **Widescreen** (Settings → Systems → PlayStation → Picture,
or the game's own settings) turns on a game's widescreen fix. A fix is a curated bundle of game patches: the projection, the
culling and level-of-detail fixes that keep the wider edges filled, and the HUD
repairs, applied together. Players never manage individual codes. Games without
a fix keep the general GTE widescreen fallback, which can still cut off geometry
at the edges.

This page is for contributors adding or reviewing a fix. The bundle file format
is in [tools/widescreen/README.md](../tools/widescreen/README.md).

## How a fix is matched and applied

Files in `romfs:/widescreen/SERIAL-FNV.ws` match both normalized disc serial
and FNV-1a over the on-disc PS-X EXE header plus declared text. A different
revision never borrows codes by title or description. The serial collision
between Urban Chaos and Threads of Fate is disambiguated by
the boot fingerprint. Staged `tools/widescreen/research/` files are never
loaded by the frontend.

Application is atomic. Unsupported, malformed, unaligned or out-of-RAM codes
reject the complete file. A7 compares known originals; A4 guards overlay
blocks; D0 guards its single following write. Leaving Widescreen restores
still-owned changes, preserves subsequent overlay/game writes, and invalidates
the dynarec blocks affected. Patched states also clean up when loaded with the
toggle off. Guards constrain restoration as well as application. The ordinary
cheat list and its saved selections do not own or expose the bundle.
See [the format reference](../tools/widescreen/README.md).

Native bundles fit 16:9 into 400×225; GTE fallback retains 5:3.
Whether a bundle is offered is part of the game's preset file (`widescreen = hidden|on|warn|off`, per console; see [settings](settings.md)); bundles carry codes only. The `models x,y` values in the table below are the per-console values (Old,New: 0 hidden, 1 offered, 2 offered with a speed warning, 3 held back to keep the speed up), which the preset files reproduce.
CTR and Silent Hill remain New-only after Old cost failures. Crash 1, Spyro 1,
Musashi, Digimon World and NFS High Stakes are hidden. The catalog
also records 32 hidden identities for 2D-only presentations; Stretch remains
available. Unassessed games expose the existing fallback.

Old cost policy: hide a bundle if matched frameskip-off fps loses **>5%**,
or automatic frameskip loses previously full game speed. Representative cost
tests are not individual acceptance for other games; their table cells say
unmeasured. Inactive-path integration has the usual approximately 1% gate.
Development-only `wsbundle=on|off` and `wsscreen=4:3|stretch|wide` allow remote
comparison without saving user settings.

## Sources and credits

Major sources are credited one line each in
[tools/art/CREDITS.md](../tools/art/CREDITS.md). Patch rights remain with their
original authors. The data is not relabeled MIT or CC BY-SA merely because a
collection's processing scripts use that license.

- DuckStation chtdb, `8b2f1dd257ab9130d16b72e0caeb5a07af8bcbdc`: the main
  source for projection, culling and HUD patches; its processing scripts are MIT.
  Exact source sections remain in the data.
- libretro-database, `12c21cf79f1e20eff6ac68b80875816c717d0a7a`: adapted data
  keeps its **CC BY-SA 4.0** attribution and the full bundled license.
- CTR-ModSDK (GPL-3.0), REDRIVER2 (MIT), GT1 OverlayCheat (GPL-3.0), the Crash
  decompilation and PeterDelta releases were read to find companion repairs and
  matching retail revisions. No project source code was copied into the app.

No ROM bytes are included.

## Per-game table

All files declare 16:9. Each link opens the file with every code, original and
guard it uses; “rows” counts guards and terminators as well as writes. Each fix
was checked in the scenes its culling and HUD cells name, in the PC simulator and
for some games on a console, not through whole games. The Old cost column gives
the measured cost where there is one; elsewhere the general Old 3DS rule above
applies.

| Game / exact identity | Codes used | Culling / LOD repair | HUD / 2D | Old cost | Offered |
|---|---|---|---|---|---|
| Ape Escape (USA) · `SCUS94423-1F9F011C` | [1 rows](../assets/romfs/widescreen/SCUS94423-1F9F011C.ws) | No independent bypass required in tested training area; expanded geometry stays filled. | Icons and training dialogue remain visible; no separate fix available. | Not individually measured | Old 3DS: offered; New 3DS: offered. |
| CTR - Crash Team Racing (USA) · `SCUS94426-CC613C2E` | [41 rows](../assets/romfs/widescreen/SCUS94426-CC613C2E.ws) | Yes: 043168 frustum width | HUD, font and overlay helpers included | 37.37 -> 33.38 fps (-10.68%) | Old 3DS: not offered (too slow); New 3DS: offered. |
| Crash Bandicoot (USA) · `SCUS94900-10A81C20` | [3 rows](../assets/romfs/widescreen/SCUS94900-10A81C20.ws) | No | No separate HUD repair found | Not individually measured | Old 3DS: not offered; New 3DS: not offered. Leaves gaps in the ground at N. Sanity Beach. |
| Crash Bandicoot 2 - Cortex Strikes Back (USA) · `SCUS94154-C5DF1D69` | [5 rows](../assets/romfs/widescreen/SCUS94154-C5DF1D69.ws) | Five-part projection/frustum bundle; Turtle Woods edges filled during traversal. | No separate HUD repair in source. | Not individually measured | Old 3DS: offered; New 3DS: offered. |
| Gran Turismo 2 (USA) (Arcade Mode) (Rev 1) · `SCUS94455-5B4CDDF4` | [53 rows](../assets/romfs/widescreen/SCUS94455-5B4CDDF4.ws) | Race and mirror frustum extents included | Car selection, prerace, results, trophy and GT-mode 2D fixes included | 32.30 -> 31.19 fps (-3.42%) | Old 3DS: offered with a speed warning; New 3DS: offered. |
| Klonoa - Door to Phantomile (USA) · `SLUS00585-BD171E3C` | [5 rows](../assets/romfs/widescreen/SLUS00585-BD171E3C.ws) | Four projection sites (one duplicated source row); first-level traversal keeps expanded geometry filled. | HUD remains visible but stretches; no separate source repair. | Not individually measured | Old 3DS: offered; New 3DS: offered. |
| Legacy of Kain - Soul Reaver (USA) · `SLUS00708-54A3F489` | [5 rows](../assets/romfs/widescreen/SLUS00708-54A3F489.ws) | Five-part native projection and companion instructions; movement through the opening cave stays filled. | No separate source HUD fix; HUD remains visible. | Not individually measured | Old 3DS: offered; New 3DS: offered. |
| Metal Gear Solid (USA) (Disc 1) (Rev 1) · `SLUS00594-23BB5B12` | [28 rows](../assets/romfs/widescreen/SLUS00594-23BB5B12.ws) | Complete injected projection bundle; dock geometry remains filled. | Radar/text remain visible but stretch; no separate source repair. | Not individually measured | Old 3DS: offered; New 3DS: offered. Disc 2 not checked. |
| R4 - Ridge Racer Type 4 (USA) · `SLUS00797-4AA72B9E` | [1 rows](../assets/romfs/widescreen/SLUS00797-4AA72B9E.ws) | No separate culling fix found; needs edge validation | No HUD correction available | 60.07 -> 60.19 fps (+0.21%) | Old 3DS: offered; New 3DS: offered. |
| Silent Hill (USA) · `SLUS00707-6434EBE6` | [2 rows](../assets/romfs/widescreen/SLUS00707-6434EBE6.ws) | No separate fix found | No separate repair found | 41.86 -> 38.69 fps (-7.58%) | Old 3DS: not offered (too slow); New 3DS: offered. |
| Spider-Man (USA) · `SLUS00875-E2EA0021` | [2 rows](../assets/romfs/widescreen/SLUS00875-E2EA0021.ws) | Native projection and companion instruction; rooftop edge geometry remains filled during traversal. | HUD remains visible; no independent source repair. | Not individually measured | Old 3DS: offered; New 3DS: offered. |
| Spyro the Dragon (USA) · `SCUS94228-05FE587F` | [3 rows](../assets/romfs/widescreen/SCUS94228-05FE587F.ws) | Includes projection and companion A7BB instruction; needs edge validation | No separate HUD fix | Not individually measured | Old 3DS: not offered; New 3DS: not offered. The source patch warns it can crash. |
| Vagrant Story (USA) · `SLUS01040-2DF49560` | [2 rows](../assets/romfs/widescreen/SLUS01040-2DF49560.ws) | Native projection plus companion ratio; opening room movement stays filled. | Radar and HP remain visible; no separate source HUD repair. | Not individually measured | Old 3DS: offered; New 3DS: offered. |
| Brave Fencer Musashi (USA) · `SLUS00726-490B13D4` | [1 rows](../assets/romfs/widescreen/SLUS00726-490B13D4.ws) | No independent fix found | No independently verified repair | Not individually measured | Old 3DS: not offered; New 3DS: not offered. The wider forest loses floor triangles; no matching culling fix found. |
| Crash Bandicoot - Warped (USA) · `SCUS94244-317BD2C7` | [5 rows](../assets/romfs/widescreen/SCUS94244-317BD2C7.ws) | Five-part projection/frustum bundle; Orient Express edges stay filled during riding and jumps. | No separate source HUD repair; counters remain visible. | Not individually measured | Old 3DS: offered; New 3DS: offered. |
| Digimon World (USA) · `SLUS01032-E2974D8B` | [1 rows](../assets/romfs/widescreen/SLUS01032-E2974D8B.ws) | No independent fix found | No independently verified repair | Not individually measured | Old 3DS: not offered; New 3DS: not offered. Fixed backgrounds stay put while characters shift and shrink. |
| Disney-Pixar Toy Story 2 - Buzz Lightyear to the Rescue! (USA) (Rev 1) · `SLUS00893-1203F43F` | [2 rows](../assets/romfs/widescreen/SLUS00893-1203F43F.ws) | Native projection and companion; Andy house traversal shows added geometry without holes. | No independent source HUD repair available. | Not individually measured | Old 3DS: offered; New 3DS: offered. |
| Gran Turismo 2 (USA) (Simulation Mode) (Rev 2) · `SCUS94488-91BB5858` | [53 rows](../assets/romfs/widescreen/SCUS94488-91BB5858.ws) | Race and mirror frustum extents included | Car selection, prerace, results, trophy and GT-mode 2D fixes included | Not individually measured | Old 3DS: offered with a speed warning; New 3DS: offered. |
| Medal of Honor (USA) · `SLUS00974-DD99BDE6` | [33 rows](../assets/romfs/widescreen/SLUS00974-DD99BDE6.ws) | No independent frustum bypass found | Camera/weapon companions and aspect coefficient included | Not individually measured | Old 3DS: offered; New 3DS: offered. |
| Medal of Honor - Underground (USA) · `SLUS01270-9436FCBE` | [33 rows](../assets/romfs/widescreen/SLUS01270-9436FCBE.ws) | Full projection/companion injection and weapon/HUD correction; main-game overlay guarded. | Weapon is intact with corrected live originals; compass and ammunition remain visible. | Not individually measured | Old 3DS: offered; New 3DS: offered. |
| MediEvil II (USA) · `SCUS94564-0FF78A7B` | [1 rows](../assets/romfs/widescreen/SCUS94564-0FF78A7B.ws) | Native projection; expanded crypt geometry stays filled during room traversal. | No separate source HUD repair available. | Not individually measured | Old 3DS: offered; New 3DS: offered. |
| Mega Man Legends (USA) · `SLUS00603-643F0FDD` | [28 rows](../assets/romfs/widescreen/SLUS00603-643F0FDD.ws) | Native injected projection/companion routine; post-tutorial corridor traversal stays filled. | HUD and minimap remain visible; no separate source repair. | Not individually measured | Old 3DS: offered; New 3DS: offered. |
| Need for Speed - High Stakes (USA) · `SLUS00826-C5D38319` | [16 rows](../assets/romfs/widescreen/SLUS00826-C5D38319.ws) | projection/GeomScreen and sky coordinates; no separate frustum bypass verified. | companion coordinates included; no independent HUD patch found. | Not individually measured | Old 3DS: not offered; New 3DS: not offered. Flares, smoke and the sun are not scaled; the menu patch has no clear redistribution grant. |
| Spyro - Year of the Dragon (USA) (Rev 1) · `SCUS94467-E868CD6A` | [20 rows](../assets/romfs/widescreen/SCUS94467-E868CD6A.ws) | Injected companion routine; opening outdoor edges filled during movement. | No separate HUD fix available. | Not individually measured | Old 3DS: offered; New 3DS: offered. |
| Spyro 2 - Ripto's Rage! (USA) · `SCUS94425-1C372E92` | [30 rows](../assets/romfs/widescreen/SCUS94425-1C372E92.ws) | Native companion projection routine plus all ten source LOD bypasses (no separate user toggle). | No separate HUD fix | Not individually measured | Old 3DS: offered; New 3DS: offered. |
| Syphon Filter (USA) (Rev 1) · `SCUS94240-A9FFAA57` | [2 rows](../assets/romfs/widescreen/SCUS94240-A9FFAA57.ws) | Two native projection sites; Washington street traversal retains filled expanded edges. No independent bypass found. | Armor, danger indicator, radar and ammunition stay visible; no separate source repair. | Not individually measured | Old 3DS: offered; New 3DS: offered. |
| Syphon Filter 2 (USA) (Disc 1) · `SCUS94451-260DF52C` | [2 rows](../assets/romfs/widescreen/SCUS94451-260DF52C.ws) | Two native projection sites; Colorado mountain opening traversal retains filled expanded edges. No independent bypass found. | Armor, radar and ammunition stay visible; no separate source repair. | Not individually measured | Old 3DS: offered; New 3DS: offered. Disc 1 only. |
| Syphon Filter 3 (USA) · `SCUS94640-56B9564B` | [2 rows](../assets/romfs/widescreen/SCUS94640-56B9564B.ws) | Two native projection sites; Tokyo hotel opening traversal retains filled expanded edges. No independent bypass found. | Armor, radar and ammunition stay visible; no separate source repair. | Not individually measured | Old 3DS: offered; New 3DS: offered. |
| Tenchu - Stealth Assassins (USA) (Rev 1) · `SLUS00706-65EFFADA` | [1 rows](../assets/romfs/widescreen/SLUS00706-65EFFADA.ws) | Native projection; starting mission edges stay filled during movement after matching the live overlay original. | Health and item HUD remain visible; no independent source repair. | Not individually measured | Old 3DS: offered; New 3DS: offered. |

## Known gaps

- No complete, revision-matched fix has been found for Tekken 3, Tomb Raider 1–4,
  Driver 1 and 2, Gran Turismo, Wipeout 3 and XL, Twisted Metal 2 and 4, and
  Final Fantasy VII and IX.
- Final Fantasy VIII's battle-only patch needs battle testing and must not
  distort the fixed-background fields.
- Crash Bandicoot and Spyro the Dragon need a safe, complete alternative before
  they can be offered. Brave Fencer Musashi, Digimon World and Need for Speed:
  High Stakes need complete geometry, 2D and effects repairs.
- Each fix was checked in the scenes its row names, not in every level, overlay
  or cutscene. Old 3DS cost is marked "not individually measured" where it was not.
