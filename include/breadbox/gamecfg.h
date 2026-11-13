// Per-game settings files (gamecfg.c)
#pragma once
#include <stdbool.h>

// gamecfg.c - per-game settings (sdmc:/3ds/PSX3D/games/<game>.ini)
void gamecfg_load(const char *game);
void gamecfg_end(void);
bool gamecfg_reset(void);
void gamecfg_unset(const char *key);
const char *gamecfg_get(const char *key);
int gamecfg_int(const char *key, int def);
void gamecfg_set(const char *key, const char *value);
void gamecfg_set_int(const char *key, int v);
