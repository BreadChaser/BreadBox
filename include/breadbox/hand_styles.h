// The room's hand skin tones and gloves (Settings, config.c)
#pragma once

// The room's hands (Settings): ids are config.ini values, names are shown.
enum { HAND_SKINS = 4, HAND_GLOVES = 6, HAND_MITTENS = 5 };
extern const char *const hand_skin_id[HAND_SKINS], *const hand_skin_name[HAND_SKINS];
extern const char *const hand_glove_id[HAND_GLOVES], *const hand_glove_name[HAND_GLOVES];
