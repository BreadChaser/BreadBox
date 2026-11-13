// The Settings screens (app/settings/settings_ui.c)
#pragma once
#include <stdbool.h>

bool settings_ui_run(bool game, void (*draw_top)(void *));
bool settings_ui_cheats(void);
bool settings_ui_take_folders(void);   // Settings > Games folder: open the folder browser

bool settings_ui_hands_row(void);   // Skin tone / Gloves selected: the room shows your hands
bool settings_ui_active(void);
bool settings_ui_debug_capture(void);
bool settings_ui_debug_key(const char *name);
