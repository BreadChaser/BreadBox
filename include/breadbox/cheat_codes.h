// PS1 cheat codes and widescreen patches (cheats.c)
#pragma once
#include <stdbool.h>

// cheats.c
void cheats_load(const char *game);
void cheats_unload(void);
int cheats_count(void);
const char *cheats_desc(int i);
bool cheats_on(int i);
void cheats_toggle(int i);
bool cheats_widescreen(bool on);
void cheats_disc_changed(void);

const char *cheats_add_dialog(void);
bool cheats_forced(int i);
