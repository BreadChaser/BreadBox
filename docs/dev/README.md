# Developer documentation

For people changing BreadBox. Build and test steps are in
[building.md](../building.md); the release checklist is [TESTING.md](../../TESTING.md);
[code-map.md](code-map.md) maps the code.

| Page | Topic |
|---|---|
| [new-system.md](new-system.md) | Adding a system, step by step |
| [system-layer.md](system-layer.md) | The system interface (`bb_system.h`) every system plugs into |
| [libretro-host.md](libretro-host.md) | The shared libretro host and runner |
| [save-service.md](save-service.md) | Battery files, postcards, Continue, Undo and the pause menu |
| [../gba-save-safety.md](../gba-save-safety.md) | Game Boy Advance save files, slots and their life cycle |
| [../settings.md](../settings.md) | The settings contract the menu is tested against |
| [../widescreen.md](../widescreen.md) | Adding or reviewing a PlayStation widescreen fix |
| [../auto-update.md](../auto-update.md) | How updates work, and publishing a release |
| [../gba-covers.md](../gba-covers.md) | Game Boy Advance box art matching and its data source |
| [dev-control.md](dev-control.md) | The opt-in remote control and input recorder used for tests on a console |

The room's models and textures are rebuilt with the tools in
[tools/room/](../../tools/room/README.md).
