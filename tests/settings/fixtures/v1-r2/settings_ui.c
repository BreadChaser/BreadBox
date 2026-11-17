/* Frozen 4067397a migration oracle; test-only, never built into BreadBox.
 * Only exported identifiers and the local include path are adapted. */
// One settings navigator for the library and pause menu. Runs only while idle.
#include "devtools.h"
#include <stdio.h>
#include <string.h>
#include "theme.h"
/* Test-only adaptation: the theme list's row helpers this oracle used were retired
 * with the old themes; the themes are now the room styles, one row each, no heading. */
#define THEMES_SHOWN 1
static int theme_rows(void) { return theme_count(); }
static int theme_at_row(int row) { return row; }
static int theme_row_of(int i) { return i; }
static const char *theme_row_label(int row) { return theme_get(row)->name; }
#include "icons.h"
#include "app_config.h"
#include "core_options.h"
#include "platform_video.h"
#include "rewind.h"
#include "gamecfg.h"
#include "cheat_codes.h"
#include "emu.h"
#include "audio.h"
#include "app_input.h"
#include "presets.h"
#include "ui_list.h"
#include "ui_pause.h"
#include "ui.h"
#include "uisound.h"
#include "libui.h"
#include "hand_styles.h"
#include "app_main.h"
bool ps1_settings_ui_run(bool game,void (*top)(void *));
#ifdef APP_BREADBOX
#include "breadbox_import.h"
#include "breadbox_credits.h"
#include "room.h"
#endif
#ifdef APP_BREADBOX
#define LIST_HEADER ui_list_context
#define LIST_BACK(k) (k)
#else
#define LIST_HEADER ui_menu_header
#define LIST_BACK ui_header_back
#endif
#include "scene.h"
#include "statsview.h"
#include "widescreen.h"
#include "audio_resample.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Available only inside this navigator; the network mailbox authenticates first.
static bool active, capture_debug, resume_requested, folders_requested;
#ifdef APP_BREADBOX
static bool system_settings;
static void system_backdrop(void *p) {(void)p;libui_draw_backdrop();}
void ps1_settings_system_run(void) {
	system_settings = true; ps1_settings_ui_run(false, system_backdrop); system_settings = false;
}
#endif
bool ps1_settings_ui_active(void) { return active; }
bool ps1_settings_ui_debug_capture(void) { return active && capture_debug; }
// Volatile inspection only: no config write, reset on closing settings.
bool ps1_settings_ui_debug_key(const char *name)
{
	if (!active || (strcmp(name, "debug-on") && strcmp(name, "debug-off")))
		return false;
	capture_debug = !strcmp(name, "debug-on");
	return true;
}

enum {
	CAT_HOME,
	CAT_DISPLAY,
	CAT_CONTROLS,
	CAT_SAVES,
	CAT_ADVANCED,
	CAT_RESET,
	CAT_CHEATS,
	CAT_BINDINGS,
	CAT_DEBUG,
	CAT_THEME,
	CAT_COUNT
};
// The menu tree is fixed by docs/settings.md (tests/settings/spec.c checks it).
static const char *const categories[] = {"Settings",
										 "Display",
										 "Controls",
										 "Saves",
										 "Advanced",
										 "Reset all settings",
										 "Cheats",
										 "Button mappings",
										 "Debug", "Theme"};
enum { F_SCREEN, F_SMOOTH, F_FPS, F_CONTROLLER, F_SIDE,
       F_REWIND, F_HEADING, F_RECOMMENDED, F_RECORD, F_TONE, F_THEME, F_FOLDER, F_STEREO,
#ifdef APP_BREADBOX
       F_IMPORT, F_SKIN, F_GLOVES,   // the room's hands
       F_CREDITS,                    // About / Credits (app settings)
#endif
       F_COUNT };
static const char *const labels[] = {
 [F_SCREEN]="Screen", [F_SMOOTH]="Smoothing", [F_FPS]="FPS counter",
 [F_CONTROLLER]="Controller",
 [F_SIDE]="Bottom-screen buttons", [F_REWIND]="Rewind", [F_HEADING]="Compatibility",
 [F_RECOMMENDED]="Reset to recommended", [F_RECORD]="Record audio", [F_TONE]="Test tone",
 [F_THEME]="Theme", [F_FOLDER]="Games folder", [F_STEREO]="3D",
#ifdef APP_BREADBOX
 [F_IMPORT]="Import again from PSX3D", [F_SKIN]="Skin tone", [F_GLOVES]="Gloves", [F_CREDITS]="About / Credits"
#endif
};
#ifdef APP_BREADBOX
static int focused = -1;   // the row under the cursor (ps1_settings_ui_hands_row)
#endif
static const char *const tips[] = {
 [F_SCREEN]="Widescreen only in games with a widescreen fix.",
 [F_SMOOTH]="Soften pixels when enlarging the picture.",
 [F_FPS]="Show the frame counter on the bottom screen.",
 [F_CONTROLLER]="Analog starts the controller in analog mode.",
 [F_SIDE]="Choose the side for touch buttons.",
 [F_REWIND]="Hold the rewind shortcut to go back.",
 [F_RECOMMENDED]="Remove this game's Advanced changes.",
 [F_RECORD]="Capture 30 seconds for troubleshooting.", [F_TONE]="Play a tone to check audio output.",
#ifdef APP_BREADBOX
 [F_THEME]="Light, Dark or an animated theme.",
#else
 [F_THEME]="Change the menu colors and animated scene.",
#endif
 #ifdef APP_BREADBOX
 [F_FOLDER]="One folder for all systems; found automatically.",
#else
 [F_FOLDER]="Pick the folder the library shows.",
#endif
 [F_STEREO]="Real 3D is experimental: slower in busy games.",
#ifdef APP_BREADBOX
 [F_IMPORT]="Copy PlayStation saves from PSX3D safely.", [F_SKIN]="Your hands in the room.", [F_GLOVES]="Your hands in the room.",
 [F_CREDITS]="Version, authors and licenses.",
#endif
};
// Longer help, shown on the top screen while the row is selected.
static const char *const helps[] = {
 [F_SCREEN]="Original keeps the shape of a TV picture. Stretch fills the screen. Widescreen shows a wider view in games that have a widescreen fix; other games use Original.",
 [F_SMOOTH]="Soften enlarged pixels. Turn off for a sharper picture.",
 [F_FPS]="FPS shows new pictures per second and game speed on the bottom screen. Detailed adds more numbers. Tap the counter to switch views.",
 [F_CONTROLLER]="Digital starts the DualShock in digital mode; games that use the analog sticks switch it themselves. Analog starts it in analog mode, for games that support the sticks but don't switch. The analog mode shortcut still works.",
 [F_SIDE]="Place the touch buttons on the right or left side. The menu button and logo move to the other side.",
 [F_REWIND]="Save recent moments so you can hold the Rewind button to go back. Needs free memory.",
 [F_RECOMMENDED]="Remove this game's changes to the Advanced options, so its tested settings apply again. Display, Controls and Saves are kept.",
 [F_RECORD]="Record 30 seconds of game audio for troubleshooting.",
 [F_TONE]="Play a steady tone to check audio output.",
 #ifdef APP_BREADBOX
 [F_FOLDER]="BreadBox finds your ROMs folder automatically. This one folder holds every system, including subfolders. Choose another folder here if needed. SELECT in a library refreshes all games.",
#else
 [F_FOLDER]="The folder on the SD card that the library lists. Opens the folder browser; you can also start a game from there.",
#endif
 [F_STEREO]="Use the 3D slider to turn 3D on and set its strength. Screen depth puts the whole picture behind the screen. Real 3D gives every object its own depth; it is experimental and can be slower in busy games.",
#ifdef APP_BREADBOX
 [F_IMPORT]="Copy memory cards, states, pictures and per-game settings. Both versions of conflicting saves are kept. PSX3D is never changed.",
 [F_SKIN]="The skin tone of the hands that take your games off the shelf.",
 [F_GLOVES]="Gloves for the hands in the room: knit, leather, fingerless, work gloves or mittens.",
 [F_CREDITS]="BreadBox " PSX3D_VERSION ". Who made it and what it is built from: emulator cores, system files, libraries, art and licenses. A opens six short pages.",
#endif
#ifdef APP_BREADBOX
 [F_THEME]="Rustic room shows your games as boxes on a bookshelf. Light and Dark are plain, quiet looks. Animated themes add a moving background with their own colors. Game pictures and cover art keep their colors.",
#else
 [F_THEME]="Choose the menu colors, animated background and icons. Each theme has its own scene. Game pictures and cover art keep their colors.",
#endif
};
static const char *const category_tips[] = {
 [CAT_DISPLAY]=THEMES_SHOWN ? "Picture shape, smoothing, FPS counter and theme." : "Picture shape, smoothing and FPS counter.",
 [CAT_CONTROLS]="Buttons, touch buttons and the controller.",
 [CAT_CHEATS]="Turn this game's cheat codes on or off.",
 [CAT_SAVES]="Memory cards and rewind.",
 [CAT_ADVANCED]="Frameskip, CPU speed and compatibility options.",
 [CAT_RESET]="Restore defaults; asks for confirmation.",
 [CAT_DEBUG]="Troubleshooting tools.",
 [CAT_BINDINGS]="Change physical buttons, touch buttons and hotkeys.",
};
// Advanced page header (docs/settings.md section 2.3; the text is checked).
static const char *const advanced_tested = "This game has tested settings. Changing them can slow it down or break it.";
static const char *const advanced_untested = "This game hasn't been tested yet; these are the standard settings.";
static const char *const advanced_library = "Games with tested settings keep their tested values for these options.";
// The technical core options of the Advanced page, in menu order.
static const char *const advanced_keys[] = {
	"pcsx_rearmed_frameskip_type", "pcsx_rearmed_psxclock", NULL, // NULL: the Compatibility heading
	"pcsx_rearmed_alt_flip", "pcsx_rearmed_gpu_slow_llists", "pcsx_rearmed_cd_turbo",
	"pcsx_rearmed_nostalls", "pcsx_rearmed_nolddelay", "pcsx_rearmed_gteregsunneeded",
	"pcsx_rearmed_nogteflags", "pcsx_rearmed_nosmccheck", "pcsx_rearmed_icache_emulation",
	"pcsx_rearmed_exception_emulation", "pcsx_rearmed_nocompathacks",
};
#define ADVANCED_KEYS (int)(sizeof(advanced_keys) / sizeof(advanced_keys[0]))
static char message[64];    // result of Add code, shown in the tip line
static int message_frames;
#define CORE_ROW 1000
#define CONTROL_ROW 2000
#define CHEAT_ROW 3000
#define CATEGORY_ROW 4000
#define MAX_ROWS 256
#define VISIBLE_ROWS UI_SETTINGS_UI_VISIBLE_ROWS

typedef struct {
	bool game;
	int page, selected[CAT_COUNT], items[MAX_ROWS], count, theme_before;
	flist_t list;
} settings_ui_t;

static bool frontend_visible(settings_ui_t *s, int i)
{
	// Only physical limits hide a row (docs/settings.md section 1).
#ifdef APP_BREADBOX
	if ((i == F_SIDE || i == F_STEREO) && !ps1_3d_available())   // only New 3DS has PS1 3D settings
#else
	if ((i == F_SIDE || i == F_STEREO) && !g_new3ds)   // no game 3D on Old 3DS
#endif
		return false;
#ifdef APP_BREADBOX
	if ((i == F_IMPORT || i == F_CREDITS) && (s->game || system_settings)) return false;
	if ((i == F_SKIN || i == F_GLOVES) && (s->game || !theme_room(theme()) || !room_show_hands))   // hands: the room themes
		return false;
#endif
	if ((i == F_THEME || i == F_FOLDER) && (s->game
#ifdef APP_BREADBOX
        || system_settings
#endif
    ))   // one look, one library for the whole app
		return false;
	if (i == F_REWIND && (!s->game || !g_new3ds))
		return false;
	if (i == F_RECOMMENDED && (!s->game || !game_preset_tested()))
		return false;
	if ((i == F_RECORD || i == F_TONE) && (!s->game || !(g_cfg.debug || capture_debug)))
		return false;
	return true;
}
static void add(settings_ui_t *s, int item)
{
	if (s->count < MAX_ROWS)
		s->items[s->count++] = item;
}
static void add_frontend(settings_ui_t *s, int item)
{
	if (frontend_visible(s, item))
		add(s, item);
}
static void add_core(settings_ui_t *s, const char *key)
{
	int i = options_index(key);
	if (i >= 0)
		add(s, CORE_ROW + i);
}
static void build_page(settings_ui_t *s, int page)
{
	s->count = 0;
#ifdef APP_BREADBOX
	if (!s->game && !system_settings) {
		// App settings contain presentation and storage only. Emulation belongs
		// to a selected system or the running game.
		if (page == CAT_HOME) {
			if (THEMES_SHOWN) add_frontend(s, F_THEME);
			add_frontend(s, F_SKIN);
			add_frontend(s, F_GLOVES);
			add_frontend(s, F_FOLDER);
            add(s,CATEGORY_ROW+CAT_SAVES);
			add_frontend(s, F_CREDITS);
            add(s,CATEGORY_ROW+CAT_RESET);
		} else if (page == CAT_SAVES) {
            add(s,F_IMPORT);
        } else if (page == CAT_THEME) {
			for (int i=0;i<theme_rows();i++) add(s,i);
		}
		return;
	}
#endif
	options_count();   // refresh the shown core options (debug can change them)
	switch (page) {
	case CAT_HOME:
		// Cheats live in the pause menu only (ps1_settings_ui_cheats).
		for (int i = CAT_DISPLAY; i <= CAT_RESET; i++) {
			settings_ui_t probe = {.game = s->game};
			build_page(&probe, i);
			if (i == CAT_RESET)   // the folder picker moved here from the library's Y
				add_frontend(s, F_FOLDER);
			if (i == CAT_RESET || probe.count)
				add(s, CATEGORY_ROW + i);
		}
		break;
	case CAT_THEME:
#ifdef APP_BREADBOX
		if (!s->game) for (int i=0; i<theme_rows(); i++) add(s, i);
#else
		if (!s->game) for (int i=0; i<theme_count(); i++) add(s, i);
#endif
		break;
	case CAT_DISPLAY:
		add_frontend(s, F_SCREEN); add_frontend(s, F_SMOOTH); add_frontend(s, F_FPS); add_frontend(s, F_STEREO);
		if (THEMES_SHOWN)
			add_frontend(s, F_THEME);
		break;
	case CAT_CONTROLS:
		add(s, CATEGORY_ROW + CAT_BINDINGS);
		add_frontend(s, F_CONTROLLER);
		add_core(s, "pcsx_rearmed_analog_combo");
		add_core(s, "pcsx_rearmed_analog_axis_modifier");
		add_frontend(s, F_SIDE);
		break;
	case CAT_SAVES:
		add_core(s, "pcsx_rearmed_memcard1");
		add_core(s, "pcsx_rearmed_memcard2");
		add_frontend(s, F_REWIND);
		break;
	case CAT_ADVANCED:
		for (int i = 0; i < ADVANCED_KEYS; i++)
			if (advanced_keys[i]) add_core(s, advanced_keys[i]);
			else add(s, F_HEADING);
		add_frontend(s, F_RECOMMENDED);
		if (g_cfg.debug || capture_debug)
			add(s, CATEGORY_ROW + CAT_DEBUG);
		break;
	case CAT_DEBUG:
		add_frontend(s, F_RECORD); add_frontend(s, F_TONE);
		add_core(s, "pcsx_rearmed_display_info");
		break;
	case CAT_BINDINGS:
		for (int i = 0; i < input_controls_count(); i++) {
#ifdef APP_BREADBOX
			// the Old 3DS has no ZL / ZR (physical slots 6 and 7): nothing could press them, so no rows
			int slot = input_control_slot(i);
			if (!g_new3ds && (slot == 6 || slot == 7))
				continue;
#endif
			add(s, CONTROL_ROW + i);
		}
		break;
	case CAT_CHEATS:
		for (int i = 0; i < cheats_count(); i++)
			add(s, CHEAT_ROW + i);
		break;
	}
}
static bool heading(int i, void *ud)
{
	settings_ui_t *s = ud;
#ifdef APP_BREADBOX
	if (s->page == CAT_THEME) return i >= 0 && i < s->count && theme_at_row(s->items[i]) < 0;
#endif
	return s->page != CAT_THEME && i >= 0 && i < s->count && s->items[i] == F_HEADING;
}
static void rebuild(settings_ui_t *s)
{
	build_page(s, s->page);
	int *sel = &s->selected[s->page];
	if (*sel >= s->count)
		*sel = s->count ? s->count - 1 : 0;
	if (heading(*sel, s) && s->count > 1)
		*sel = *sel + 1 < s->count ? *sel + 1 : *sel - 1;
}
// The games/<game>.ini key a row's value is stored under in a game's menu.
static const char *override_key(int item)
{
	static const char *const keys[] = {
	 [F_SCREEN]="screen", [F_SMOOTH]="smooth", [F_FPS]="show_fps",
	 [F_SIDE]="touch_click_left", [F_CONTROLLER]="controller", [F_REWIND]="rewind",
	 [F_THEME]="theme", [F_STEREO]="stereo_style"};
	if (item >= CORE_ROW && item < CONTROL_ROW)
		return options_game_key(options_key(item - CORE_ROW));
	return item >= 0 && item < F_COUNT && item < (int)(sizeof(keys) / sizeof(keys[0])) ? keys[item] : NULL;
}
static bool overridden(settings_ui_t *s, int item)
{
	if (!s->game)
		return false;
	const char *key = override_key(item);
	return key && config_game_overridden(key);
}
static const char *preset_key(int item)
{
	if (item >= CORE_ROW && item < CONTROL_ROW) {
		const char *key = options_key(item - CORE_ROW);
		if (!strcmp(key, "pcsx_rearmed_frameskip_type")) return "frameskip";
	#ifdef PSX3D_PS1_OVERCLOCK_PRESETS
		if (!strcmp(key, "pcsx_rearmed_psxclock")) return "psxclock";
#endif
		// Technical options keep the preset key name (core key minus prefix).
		return game_preset_core_value(key) ? key + 13 : "";
	}
	if (item == F_CONTROLLER) return "controller";
	if (item >= CONTROL_ROW && item < CHEAT_ROW) {
		const char *key = input_control_key(item - CONTROL_ROW);
		int slot = input_control_slot(item - CONTROL_ROW) - INPUT_PHYSICAL_N;
		bool stick = key && !strcmp(key, "touch_stick");
		bool touch_mode = game_preset_sets("touch_controls");
		if ((stick || (slot >= 0 && slot < 6)) && touch_mode &&
		    game_capabilities().touch_controls != TOUCH_CONTROLS_AUTO)
			return "touch_controls";
		if (stick) return game_preset_sets("stick") ? "stick" : touch_mode ? "touch_controls" : "stick";
		static const char *const keys[] = {"touch_l2", "touch_r2", "touch_l3", "touch_r3", "touch_x1", "touch_x2"};
		if (slot >= 0 && slot < 6) {
			if (game_preset_sets(keys[slot])) return keys[slot];
			const char *need = slot < 2 ? "l2r2" : slot < 4 ? "l3r3" : slot == 4 ? "fast_forward" : "rewind";
			return game_preset_sets(need) ? need : touch_mode ? "touch_controls" : need;
		}
	}
	return "";
}
static bool preset_row(settings_ui_t *s, int item)
{
	const char *key = preset_key(item);
	if (!s->game || !game_preset_sets(key)) return false;
	// Derived touch defaults follow the effective capability too. A user's
	// capability override is not evidence that the preset supplied the binding.
	if (!strcmp(key, "rewind") && config_rewind_on()) return false;
	if (overridden(s, item)) return false;
	if (item >= CONTROL_ROW && item < CHEAT_ROW) {
		int slot = input_control_slot(item - CONTROL_ROW);
		if (slot >= 0 && input_map_raw(slot) != INPUT_BIND_PRESET) return false;
		const char *key = input_control_key(item - CONTROL_ROW);
		if (key && !strcmp(key, "touch_stick") && g_cfg.touch_stick >= 0) return false;
	}
	return true;
}
static const char *row_label(int i, void *ud); /* shared presentation */
static const char *row_value(int i, void *ud); /* shared presentation */
static int parent(int page)
{
 if (page == CAT_THEME) {
#ifdef APP_BREADBOX
    if(!system_settings)return CAT_HOME;
#endif
    return CAT_DISPLAY;
 }
 if (page == CAT_BINDINGS) return CAT_CONTROLS;
 if (page == CAT_DEBUG) return CAT_ADVANCED;
 return CAT_HOME;
}
static void open_page(settings_ui_t *s, int page)
{
	if (page == CAT_THEME) {
		s->theme_before = g_cfg.theme;
#ifdef APP_BREADBOX
		s->selected[CAT_THEME] = theme_row_of(g_cfg.theme);
#else
		s->selected[CAT_THEME] = g_cfg.theme;
#endif
	}
	s->page = page;
	rebuild(s);
    if (page == CAT_THEME) flist_snap(&s->list,s->selected[CAT_THEME],s->count,VISIBLE_ROWS);
    else s->list.init = false;
}
// Preview is volatile until A. B and every external exit restore the entry value.
static void theme_preview(settings_ui_t *s) {
#ifdef APP_BREADBOX
    if (theme_at_row(s->selected[CAT_THEME]) >= 0) g_cfg.theme = theme_at_row(s->selected[CAT_THEME]);
#else
    g_cfg.theme = s->selected[CAT_THEME];
#endif
    theme_menu_refresh();
}
static void theme_finish(settings_ui_t *s, bool accept) {
#ifdef APP_BREADBOX
    g_cfg.theme = accept && theme_at_row(s->selected[CAT_THEME]) >= 0 ? theme_at_row(s->selected[CAT_THEME]) : s->theme_before;
#else
    g_cfg.theme = accept ? s->selected[CAT_THEME] : s->theme_before;
#endif
    if (accept) config_user_changed("theme");
    theme_menu_refresh();
    open_page(s, parent(CAT_THEME));
}
static void sync_picture(void) { config_ws_sync(); }
// In a game, why Widescreen is missing or what it costs on this console.
static const char *screen_note(void)
{
	int policy = ws_console_policy();
	if (config_ws_fix())
		return policy == WS_TOO_SLOW ? g_new3ds ? "Widescreen lowers the speed a lot on New 3DS."
				: "Widescreen lowers the speed a lot on Old 3DS."
			: policy != WS_COSTLY ? NULL
			: g_new3ds ? "Widescreen can lower the speed on New 3DS."
			: "Widescreen can lower the speed on Old 3DS.";
	return "This game has no widescreen fix.";
}
// The Advanced page starts with a line about this game's tested settings.
static const char *advanced_header(settings_ui_t *s)
{
	if (s->page != CAT_ADVANCED)
		return NULL;
	return !s->game ? advanced_library : game_preset_tested() ? advanced_tested : advanced_untested;
}
static float list_top(settings_ui_t *s); /* shared presentation */
static float row_height(settings_ui_t *s); /* shared presentation */
static int list_rows(settings_ui_t *s); /* shared presentation */
// The category L or R moves to (Display .. Advanced, skipping empty ones).
static int neighbour_page(settings_ui_t *s, int dir)
{
	int page = s->page == CAT_DEBUG ? CAT_ADVANCED : s->page == CAT_BINDINGS ? CAT_CONTROLS : s->page;
	settings_ui_t probe = {.game = s->game};
	for (int tries = 0; tries < CAT_ADVANCED; tries++) {
		page += dir;
		if (page < CAT_DISPLAY)
			page = CAT_ADVANCED;
		if (page > CAT_ADVANCED)
			page = CAT_DISPLAY;
		build_page(&probe, page);
		if (probe.count)
			break;
	}
	return page;
}
// The touch buttons for the page and the selected row; unusable ones are left out.
static int settings_bar(settings_ui_t *s, ui_button_t *b)
{
	if (s->page == CAT_THEME) {
        b[0]=(ui_button_t){KEY_B,"Cancel",0,false};
        b[1]=(ui_button_t){KEY_A,"Select",UI_ICON_CHECK,false};
        return 2;
    }
	int n = 0, item = s->count ? s->items[s->selected[s->page]] : -1;
	b[n++] = (ui_button_t){KEY_B, "Back", 0, false};
	if (s->page != CAT_HOME && s->page != CAT_CHEATS && s->page != CAT_BINDINGS) {
		b[n++] = (ui_button_t){KEY_L, categories[neighbour_page(s, -1)], UI_ICON_LEFT, false};
		b[n++] = (ui_button_t){KEY_R, categories[neighbour_page(s, 1)], UI_ICON_RIGHT, false};
	}
	if (s->page == CAT_CHEATS)
		b[n++] = (ui_button_t){KEY_X, "Add", UI_ICON_PLUS, false};
	else if (s->page == CAT_BINDINGS) {
		if (s->game && item >= CONTROL_ROW && item < CHEAT_ROW && config_control_overridden(item - CONTROL_ROW))
			b[n++] = (ui_button_t){KEY_X, "Inherit", UI_ICON_RESET, false};
		b[n++] = (ui_button_t){s->game ? KEY_Y : KEY_X, "Reset", UI_ICON_RESET, false};
	} else if (s->game && item >= 0 && item < CONTROL_ROW && overridden(s, item))
		b[n++] = (ui_button_t){KEY_X, "Inherit", UI_ICON_RESET, false};
	if (item >= 0 && item != F_HEADING)
		b[n++] = item >= CATEGORY_ROW || item == F_FOLDER || item == F_THEME ? (ui_button_t){KEY_A, "Open", UI_ICON_OPEN, false}
			: s->page == CAT_CHEATS ? (ui_button_t){KEY_A, "On/off", UI_ICON_TOGGLE, false}
			: item == F_RECOMMENDED ? (ui_button_t){KEY_A, "Reset", UI_ICON_RESET, false}
			: (ui_button_t){KEY_A, "Change", UI_ICON_CHANGE, false};
	return n;
}
static void draw_settings(void *ud); /* rendered by the shared settings_ui component */
static void change(settings_ui_t *s, int item, int dir)
{
	if (item >= CATEGORY_ROW) {
		open_page(s, item - CATEGORY_ROW);
		return;
	}
	if (item >= CHEAT_ROW) {
		cheats_toggle(item - CHEAT_ROW);
		return;
	}
	if (item >= CONTROL_ROW) {
		if (!s->game) config_user_changed(input_control_key(item - CONTROL_ROW));
		input_control_cycle(item - CONTROL_ROW, dir);
		config_controls_save();
		return;
	}
	if (item >= CORE_ROW) {
		options_cycle(item - CORE_ROW, dir);
		return;
	}
	if (!s->game && item != F_THEME && override_key(item)) config_user_changed(override_key(item));
	switch (item) {
#ifdef APP_BREADBOX
    case F_IMPORT: breadbox_import_offer(false); break;
#endif
	case F_CONTROLLER:
		if (s->game) gamecfg_set_int("controller", !config_controller());
		else g_cfg.controller ^= 1;
		input_controller_apply(true);
		break;
	case F_RECOMMENDED:
		if (ui_confirm_action("Reset to recommended?",
				"Remove this game's changes to the Advanced options. Its tested settings apply again; "
				"Display, Controls and Saves are kept.", "A  Reset to recommended", true)) {
			const char *keys[ADVANCED_KEYS];
			int n = 0;
			for (int i = 0; i < ADVANCED_KEYS; i++)
				if (advanced_keys[i]) keys[n++] = options_game_key(advanced_keys[i]) ? strdup(options_game_key(advanced_keys[i])) : NULL;
			config_game_reset_advanced(keys, n);
			for (int i = 0; i < n; i++) free((void *)keys[i]);
		}
		break;
	case F_SCREEN:
		if (s->game) {
			// Without a widescreen fix the choices are Original and Stretch.
			int n = config_ws_fix() ? 3 : 2;
			config_game_set_screen((config_screen_mode() + dir + n) % n);
			sync_picture();
		} else {
			config_user_changed("stretch");
			config_user_changed("core.pcsx_rearmed_psx3d_widescreen");
			config_set_screen_mode((config_screen_mode() + dir + 3) % 3);
		}
		break;
	case F_SMOOTH:
		g_cfg.smooth ^= 1;
		break;
	case F_FPS:
		if (s->game) {
			statsview_cycle();   // steps forward; twice is one step back
			if (dir < 0)
				statsview_cycle();
		}
		else
			g_cfg.show_fps = (g_cfg.show_fps + dir + 3) % 3;
		break;
	case F_STEREO:
		g_cfg.stereo_screen ^= 1;
		config_sync_stereo();
		break;
	case F_FOLDER:
		// the caller opens the folder browser once Settings has closed
		folders_requested = resume_requested = true;
		break;
	case F_THEME:
		open_page(s, CAT_THEME);
		break;
#ifdef APP_BREADBOX
	case F_SKIN:
		g_cfg.hand_skin = (g_cfg.hand_skin + dir + HAND_SKINS) % HAND_SKINS;
		break;
	case F_GLOVES:
		g_cfg.hand_glove = (g_cfg.hand_glove + dir + HAND_GLOVES) % HAND_GLOVES;
		break;
#endif
	case F_SIDE:
		g_cfg.touch_click_left ^= 1;
		input_touch_reset();
		break;
	case F_REWIND:
		gamecfg_set_int("rewind", !config_rewind_on());
		options_mark_updated();
		if (config_rewind_on())
			rewind_start();
		else
			rewind_stop();
		break;
	case F_RECORD:
		audio_capture_start();
		resume_requested = true;
		break;
	case F_TONE:
		audio_test_tone_toggle();
		resume_requested = true;
		break;
	}
}
// The Settings list of the main menu and library: the selected category's
// picture, name and tip over the backdrop (not in a game: its pictures stay
// unloaded there). -1: none.
static int category_picture(settings_ui_t *s, int item)
{
	if (s->game || s->page != CAT_HOME)
		return -1;
	if (item == F_FOLDER)
		return ICON_FOLDER;
	switch (item - CATEGORY_ROW) {
	case CAT_DISPLAY: return ICON_DISPLAY;
	case CAT_CONTROLS: return ICON_CONTROLS;
	case CAT_SAVES: return ICON_SAVES;
	case CAT_ADVANCED: return ICON_ADVANCED;
	case CAT_RESET: return ICON_RESET;
	}
	return -1;
}
static void draw_category(int pic, int item); /* rendered by the shared settings_ui component */
// Top screen: the library or game picture, with the selected row's help.
static void (*draw_behind)(void *);
static void draw_help(void *ud); /* rendered by the shared settings_ui component */
static bool run_settings(bool game, int page, void (*draw_top)(void *))
{
	active = true;
#ifdef APP_BREADBOX
 ui_swap_enter();
#endif
	resume_requested = false;
	static settings_ui_t memory[2];
	settings_ui_t *s = &memory[game];
	s->game = game;
	open_page(s, page);
	while (aptMainLoop()) {
		if (dev_tick(game) || dev_ui_pending())
			break;
		input_poll();
		ui_button_t bar[UI_BAR_MAX];
		u32 k = LIST_BACK(ui_bar_input(bar, settings_bar(s, bar), input_keys_down()));

		int *sel = &s->selected[s->page];
#ifdef APP_BREADBOX
 int before=*sel;
 k=ui_list_input_sections(&s->list,s->count,sel,k,false,heading,s);
 if(*sel!=before)sfx_play(SFX_MOVE);
#else
		if (s->count && (k & (KEY_DOWN | KEY_UP))) {
			do *sel = (*sel + (k & KEY_DOWN ? 1 : s->count - 1)) % s->count;
			while (heading(*sel, s));
			sfx_play(SFX_MOVE);
		}
#endif
#ifdef APP_BREADBOX
        int picked=s->count ? s->items[*sel] : -1;
        u32 swaps=KEY_B;
        if(s->page!=CAT_HOME && s->page!=CAT_CHEATS && s->page!=CAT_THEME)swaps|=KEY_L|KEY_R;
        if(picked>=CATEGORY_ROW || picked==F_THEME || picked==F_FOLDER || picked==F_IMPORT || picked==F_CREDITS ||
           picked==F_RECOMMENDED || picked==F_RECORD || picked==F_TONE || s->page==CAT_THEME)swaps|=KEY_A;
        if(s->page==CAT_BINDINGS)swaps|=KEY_X|KEY_Y;
        k=ui_swap_keys(k,swaps); // touch has already become a row action
#endif
		if (k & KEY_B) {
			sfx_play(SFX_BACK);
            if (s->page == CAT_THEME) {
                theme_finish(s,false);
                k=0;
            } else {
			// Cheats is opened from the pause menu: B returns there.
			if (s->page == CAT_HOME || s->page == CAT_CHEATS)
				break;
			open_page(s, parent(s->page));
            }
			k = 0;
		}
		sel=&s->selected[s->page];
		if (s->page != CAT_HOME && s->page != CAT_CHEATS && s->page != CAT_THEME && (k & (KEY_L | KEY_R))) {
			open_page(s, neighbour_page(s, k & KEY_R ? 1 : -1));
			sel = &s->selected[s->page];
			k = 0;
		}
#ifndef APP_BREADBOX
		if (k & KEY_TOUCH) {
			touchPosition p;
			hidTouchRead(&p);
			int row = flist_row_at(&s->list, list_top(s), row_height(s), p.py);
			if (p.py >= list_top(s) && p.py < list_top(s) + list_rows(s) * row_height(s) &&
			    row >= 0 && row < s->count && !heading(row, s)) {
				if (row == *sel)
					k |= KEY_A;
				else
					*sel = row;
			}
		}
#endif
        if (s->page == CAT_THEME) {
            theme_preview(s);
            if (k & KEY_A) { theme_finish(s,true); sfx_play(SFX_SELECT); }
            k=0;
            sel=&s->selected[s->page];
        }
		int item = s->count ? s->items[*sel] : -1;
#ifdef APP_BREADBOX
		focused = item;
#endif
		if ((k & KEY_X) && s->page == CAT_CHEATS) {
			const char *result = cheats_add_dialog();
			if (result) {
				snprintf(message, sizeof(message), "%s", result);
				message_frames = 180;
			}
			sync_picture();
		} else if ((k & KEY_X) && game && item >= CONTROL_ROW && item < CHEAT_ROW) {
			config_control_clear(item - CONTROL_ROW);
		} else if ((k & (KEY_X | KEY_Y)) && s->page == CAT_BINDINGS) {
			if (ui_confirm("Reset controls?",
						   game ? "Use the inherited button and touch mappings for this game?"
								: "Restore this console's default button and touch mappings?",
						   game))
				config_controls_defaults();
		} else if ((k & KEY_X) && game && item < CONTROL_ROW && override_key(item)) {
			config_game_clear(override_key(item));
			if (item == F_REWIND)
				rewind_stop();
			sync_picture();
		}
		int dir = k & KEY_LEFT ? -1 : k & (KEY_RIGHT | KEY_A) ? 1 : 0;
		if (item >= 0 && dir && ((item < CATEGORY_ROW && item != F_RECOMMENDED && item != F_FOLDER && item != F_THEME
#ifdef APP_BREADBOX
            && item != F_IMPORT && item != F_CREDITS
#endif
            ) || (k & KEY_A))) {
#ifdef APP_BREADBOX
            if (item == F_CREDITS) {
                bb_credits_run();
                if (!aptMainLoop()) break;   // HOME > Close ended the dialog: no more drawing
            } else
#endif
			if (item == CATEGORY_ROW + CAT_RESET) {
				if (ui_confirm(game ? "Reset this game?" :
#ifdef APP_BREADBOX
                            system_settings?"Reset PlayStation defaults?":"Reset the theme?",
#else
                            "Reset all settings?",
#endif
							   game ? "Remove this game's settings and control overrides. Return to its "
									  "preset and inherited defaults. Saves and cheats are kept."
									:
#ifdef APP_BREADBOX
                        !system_settings ? "Restore the default theme. PlayStation and game settings, hands and saves are kept." :
                        "Restore the PlayStation defaults and button mappings. The app theme, hands, speed hotkeys, "
									  "game settings and saves are kept.",
#else
                        "Restore this console's defaults and control mappings. Game overrides "
									  "and saves are kept.",
#endif
							   game)) {
					#ifdef APP_BREADBOX
                    if(!game && !system_settings) {
                        g_cfg.theme=theme_default();theme_menu_refresh();
                    } else
#endif
                    if (game) {
						config_game_reset();
						rewind_stop();
						sync_picture();
					} else {
                        config_reset_defaults();   // BreadBox: PlayStation values only (config.c)
						music_play(false);
						music_play(true);
					}
				}
			} else {
				change(s, item, dir);
				config_game_store();   // so the row shows its * at once
			}
			sfx_play(SFX_SELECT);
		}
		if (message_frames > 0)
			message_frames--;
		if(resume_requested) break;
		rebuild(s);
		flist_update(&s->list, s->selected[s->page], s->count, list_rows(s));
		if (!game)
			music_update();

#ifdef APP_BREADBOX
        if(game)ui_pause_frame();
#endif
		ui_begin();
		draw_behind = draw_top;
		video_present_vsync(game, draw_help, draw_settings, s);
	}
	if (s->page == CAT_THEME) theme_finish(s,false);
	active = false;
	capture_debug = false;
	config_save();
	return resume_requested;
}

#ifdef APP_BREADBOX
bool ps1_settings_ui_run(bool game, void (*draw_top)(void *))
{
	bool r = run_settings(game, CAT_HOME, draw_top);
	focused = -1;
	return r;
}
#else
bool ps1_settings_ui_run(bool game, void (*draw_top)(void *)) { return run_settings(game, CAT_HOME, draw_top); }
#endif
#ifdef APP_BREADBOX
bool ps1_settings_ui_hands_row(void) { return room_show_hands && (focused == F_SKIN || focused == F_GLOVES); }
#endif
// Settings > Games folder was chosen: open the folder browser (asks once).
bool ps1_settings_ui_take_folders(void)
{
	bool r = folders_requested;
	folders_requested = false;
	return r;
}
#if defined(APP_BREADBOX) && defined(__3DS__)
#include "bb_feature_ui.h"
extern const bb_cheat_ops bb_legacy_cheats;
bool ps1_settings_ui_cheats(void) { return bb_cheats_ui(&bb_legacy_cheats,true); }
#else
bool ps1_settings_ui_cheats(void) { return run_settings(true, CAT_CHEATS, NULL); }
#endif

#include "settings_ui.inc"
