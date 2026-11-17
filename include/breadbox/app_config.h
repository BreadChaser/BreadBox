// Frontend settings and core option overrides (config.c)
#pragma once
#include <stdbool.h>
#include "app_paths.h"

// config.c - frontend settings plus core option overrides
typedef struct {
	char last_dir[PATH_LEN];
	char games_dir[PATH_LEN];   // Y in the browser jumps here
	char reserved0[256];        // was last_title (the 2D library's selection, retired): no reader or writer.
	                            // Frozen per-frame play controls read the members below by offset; drop it when the
	                            // layout baseline moves.
	int  smooth;       // bilinear filtering
	int  theme;        // menu colours (theme.c), index into the theme table
	int  hand_skin;    // the room's hands: skin tone (HAND_SKINS), gloves (HAND_GLOVES)
	int  hand_glove;
	int  show_fps;
	int  stereo;       // 3D slider: 0 off, 1 screen depth, 2 real (per-object)
	int  stereo_screen; // 3D setting (New 3DS): 0 Real 3D, 1 Screen depth
	int  debug;        // show debug tools in the menu
	int  stretch;      // stretch the picture to 400x240
	int  sounds;       // interface sounds
	int  music;        // library music
	int  bottom_off;   // backlight off on the bottom screen while playing
	int  audio_stretch; // maximum slowdown in percent (forced on, defaults.ini)
	int  touch_stick;  // Old 3DS virtual right stick
	int  touch_click_left; // global New 3DS bottom-screen layout: 0 right, 1 left
	int  controller;   // all-games Controller: 0 digital, 1 analog (DualShock at start)
	// main menu Continue: the last game played here (home.c)
	char last_game[256];        // its title ("" = name from the path)
	char last_game_save[256];   // save name it was started with ("" = from the path)
	char last_game_state[256];  // save name it ended up with (states/<this>.sta)
	char last_game_path[PATH_LEN];
} psx3d_config;

extern psx3d_config g_cfg;
void config_user_changed(const char *key);
bool config_setting_changed(const char *key);
void config_user_forget(const char *key);
int config_factory_value(const char *key);
void config_core_default(const char *key);
void config_room_defaults(void);
void config_set_user_core_override(const char *key, const char *value);
int input_hotkey_factory(int slot);
bool config_present(void);        /* BreadBox has its own config.ini (or its backup / unfinished copy) */
bool config_import_psx3d(void);   /* first start beside PSX3D: its config.ini, copied */
/* config.ini "welcome", the first-launch welcome (welcome.c): -1 no key (a BreadBox from before it), 0 still to
 * show, 1 seen. Not a setting: no reset changes it. */
extern int config_welcome;
void config_load(void);
void config_save(void);
bool config_last_save_ok(void);
void config_reset_defaults(void);
bool config_game_reset(void);
bool config_game_clear(const char *key);
bool config_game_overridden(const char *key);
int config_controller(void);     // effective Controller for this game (1 = analog)
bool config_rewind_on(void);     // Rewind is on for the running game
void config_game_reset_advanced(const char *const *keys, int n);
bool config_control_overridden(int row);
const char *input_control_key(int row);
void config_control_clear(int row);

void config_controls_save(void);
void config_controls_defaults(void);
const char *config_core_override(const char *key);
bool config_widescreen(void);
int config_screen_mode(void);
void config_set_screen_mode(int mode);
void config_game_begin(const char *game);
void config_game_set_screen(int mode);
void config_game_end(void);
void config_game_store(void);
bool config_in_game(void);
bool config_ws_fix(void);
void config_ws_sync(void);
void config_set_widescreen(bool on);
void config_set_core_override(const char *key, const char *value);

void config_sync_stereo(void);
void config_set_stereo_active(bool on);
