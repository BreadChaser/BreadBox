#ifndef BREADBOX_GBA_SETTINGS_H
#define BREADBOX_GBA_SETTINGS_H
#include <stdbool.h>

/* screen_size: 0 = Original, 1 = Fit (3:2), 2 = Stretch. stereo has no row:
 * GBA 3D follows the 3D slider. rewind: New 3DS only (like PS1). */
/* The core post-process slots stay off: picture effects belong to presentation,
 * including both layer-3D eyes. Keep the established core-facing offsets. */
typedef struct { int screen_size, smoothing, core_color_correction, core_frame_blend, stereo, solar, rewind;
 int color_correction, frame_blend; } GbaSettings;
#define GBA_SETTINGS_DEFAULT ((GbaSettings){1,0,0,0,1,5,1,0,0})
#define GBA_SETTING_FIELDS 7
enum { GBA_SET_SCREEN, GBA_SET_SMOOTH, GBA_SET_COLOR, GBA_SET_BLEND, GBA_SET_STEREO, GBA_SET_SOLAR, GBA_SET_REWIND,
       GBA_SET_HOTKEY_FF, GBA_SET_HOTKEY_REWIND };   /* hotkeys: app-wide, shared with PS1 */
extern GbaSettings gba_settings;
extern void (*gba_settings_changed)(void);
extern bool gba_settings_solar_game;   /* the running game has a solar sensor (Boktai) */
const char *gba_setting_label(int row, void *ud);
void gba_settings_load(void);
void gba_settings_save(void);
void gba_settings_for_identity(const char *path, const char *identity);
void gba_settings_for_game(const char *path);
void gba_settings_clear(int row);
const char *gba_settings_source(int row);
bool gba_settings_overridden(int field);
void gba_settings_set(int row, int dir);
/* Visible rows of the GBA settings lists (system page and in-game). */
int gba_setting_rows(void);
int gba_setting_field(int row);
void gba_setting_change(int row, int dir);
void gba_setting_clear(int row);
const char *gba_setting_source(int row);   /* "Source: ..." for a visible row */

#endif
