// The saved games folder and its fallbacks (breadbox_gamesdir.c)
#pragma once
#include <stdbool.h>
#include "app_paths.h"

extern bool bb_games_dir_gone;   // the saved games folder cannot be read and nothing else was found: it is kept, the first-run screen says so
extern char bb_unreadable_dir[PATH_LEN];   // the saved folder that cannot be read ("" when it can): a fallback never replaces it
extern bool bb_games_dir_session;   // g_cfg.games_dir is a fallback for this session only: config_save writes bb_unreadable_dir
void bb_games_dir_chosen(void);     // the player chose a folder: it replaces the saved one
void bb_games_dir_found(const char *found);   // discovery found a root: the saved folder if it is that, else a session fallback
void bb_games_dir_unreadable(void); // nothing found for the saved folder: keep it, flag it
bool bb_games_dir_retry(void);      // the saved folder reads again: use it
