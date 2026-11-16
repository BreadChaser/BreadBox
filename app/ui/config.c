#include "bb_writers.h"
#include "app_policy.h"
// Settings file: sdmc:/3ds/PSX3D/config.ini
//   lines "name=value" set frontend settings,
//   lines "core.<option>=value" override PCSX-ReARMed core options.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stddef.h>
#include "safefile.h"
#include "theme.h"
#include "bb_settings.h"
#include "app_paths.h"
#include "app_log.h"
#include "app_config.h"
#include "whatsnew.h"
#include "cold_code.h"
#include "core_options.h"
#include "dev_switches.h"
#include "gamecfg.h"
#include "cheat_codes.h"
#include "app_input.h"
#include "presets.h"
#include "hand_styles.h"
#include "games_dir.h"
#include "app_main.h"
#include "control_model.h"
#include "safefile.h"
#include "widescreen.h"
#include "audio_resample.h"
#include "theme.h"
#include "system_3d.h"

#define MAX_OVERRIDES 128
static const char seen_key[] __attribute__((section(".gnu.linkonce.r.bb_seen_key"))) = "seen_version";
static const char seen_format[] __attribute__((section(".gnu.linkonce.r.bb_seen_format"))) = "seen_version=%s\n";

psx3d_config g_cfg;
char config_seen_version[BB_SEEN_VERSION_CAP];

static struct { char *key, *value; } ovr[MAX_OVERRIDES];
static int novr;

// Presence is intent. Legacy dense files cannot distinguish a deliberate default
// from an automatic write: preserve every loaded key, including default values.
// New files record only settings changed by the player (or changed from factory).
static char user_keys[256][64];
static int nuser;
static bool user_has(const char *key)
{
	for (int i = 0; i < nuser; i++) if (!strcmp(user_keys[i], key)) return true;
	return false;
}
void config_user_changed(const char *key)
{
	if (key && !user_has(key) && nuser < 256) snprintf(user_keys[nuser++], 64, "%s", key);
}
static void user_forget(const char *key)
{
	for (int i = 0; i < nuser; i++) if (!strcmp(user_keys[i], key)) {
		memmove(user_keys[i], user_keys[i+1], (--nuser-i) * sizeof(user_keys[0])); return;
	}
}
void config_set_user_core_override(const char *key, const char *value)
{
	char name[64]; snprintf(name, sizeof(name), "core.%s", key);
	config_user_changed(name);
	config_set_core_override(key, value);
}

// While a game runs its own screen mode is in effect; config.ini keeps the
// default for games that don't have one (see config_game_begin).
static bool in_game;
bool config_in_game(void) { return in_game; }
static int default_screen;
static int default_maps[INPUT_REMAP_N], default_touch_stick;
static int default_hotkeys[INPUT_HOTKEY_N];
static int default_controller;   // the all-games Controller choice
#define WS_KEY "pcsx_rearmed_psx3d_widescreen"
#define WS_Q16 "52429"   // 0.8 in 16.16 fixed point (config_set_widescreen)

// Frontend settings a game keeps for itself when changed in its menu
// (games/<game>.ini retains explicit choices, including values equal to defaults).
static int *const game_fields[] = { &g_cfg.smooth, &g_cfg.show_fps, &g_cfg.stereo_screen };
static const char *const game_field_keys[] = { "smooth", "show_fps", "stereo_style" };
#define GAME_FIELDS 3
static int default_fields[GAME_FIELDS];
static void game_fields_store(void)
{
	for (int i = 0; i < GAME_FIELDS; i++) {
		if (*game_fields[i] == default_fields[i] && !gamecfg_get(game_field_keys[i])) continue;
		else gamecfg_set_int(game_field_keys[i], *game_fields[i]);
	}
}
void config_game_store(void)
{
	if (in_game)
		game_fields_store();
}
static void game_fields_restore(void)
{
	for (int i = 0; i < GAME_FIELDS; i++) *game_fields[i] = default_fields[i];
	config_sync_stereo();
	input_touch_reset();
}

// Starting values per console come from defaults.ini (docs/settings.md).
static int factory(const char *key)
{
	static const char *const words[][2] = {
		{ "smooth", "off|on" }, { "show_fps", "off|fps|detailed" }, { "stereo", "off|screen|real" }, { "stereo_style", "real|screen" },
		{ "sounds", "off|on" }, { "music", "off|on" }, { "touch_click_left", "right|left" },
		{ "screen", "original|stretch|widescreen" }, { "controller", "digital|analog" }, { "rewind", "off|on" },
	};
	for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
		if (!strcmp(words[i][0], key))
			return defaults_word(key, words[i][1], 0);
	return 0;
}
// Retired settings: their saved values are ignored and never written again.
static bool retired(const char *k)
{
	return !strcmp(k, "show_all_settings") || !strcmp(k, "audio_stretch") || !strcmp(k, "fast_forward") || !strcmp(k, "theme_room") ||
	       !strcmp(k, "last_title") || !strcmp(k, "library_view") ||   // the 2D library's selection and Sort view
	       !strcmp(k, "stereo") || !strcmp(k, "sounds") || !strcmp(k, "music") ||
	       !strcmp(k, "core.pcsx_rearmed_psx3d_stereo") || !strcmp(k, "core.preset_fast_forward") || !strcmp(k, "core.psx3d_auto_floor");
}
// A core option fixed in defaults.ini: no player value is kept for it.
static bool forced_core(const char *key)
{
	bool forced;
	defaults_core(key, &forced);
	return forced;
}

static void clear_overrides(void)
{
	for (int i = 0; i < novr; i++) {
		free(ovr[i].key);
		free(ovr[i].value);
	}
	novr = 0;
}

// BreadBox starts in the Day room.
#define THEME_FACTORY() theme_default()

bool bb_games_dir_gone;
char bb_unreadable_dir[PATH_LEN];
bool bb_games_dir_session;

const char *const hand_skin_id[HAND_SKINS] = { "light", "medium", "dark", "deep" };
const char *const hand_skin_name[HAND_SKINS] = { "Light", "Medium", "Dark", "Deep" };
const char *const hand_glove_id[HAND_GLOVES] = { "none", "knit", "leather", "fingerless", "work", "mittens" };
const char *const hand_glove_name[HAND_GLOVES] = { "No gloves", "Knit", "Leather", "Fingerless", "Work gloves", "Mittens" };

static void room_defaults(void)
{
 g_cfg.theme=THEME_FACTORY();
 g_cfg.hand_skin=1;g_cfg.hand_glove=0;
}
static void set_defaults(void)
{
	memset(&g_cfg, 0, sizeof(g_cfg));
	strcpy(g_cfg.last_dir, "sdmc:/");
	g_cfg.smooth = factory("smooth");
	room_defaults();
	theme_menu_refresh();
	g_cfg.show_fps = factory("show_fps");
	g_cfg.stereo = factory("stereo");
	g_cfg.stereo_screen = factory("stereo_style");
	g_cfg.sounds = factory("sounds");
	g_cfg.music = factory("music");
	g_cfg.touch_click_left = factory("touch_click_left");
	g_cfg.stretch = factory("screen") == 1;
	g_cfg.controller = factory("controller");
	g_cfg.bottom_off = 1;
	// Forced on (defaults.ini): only used when sound runs low.
	g_cfg.audio_stretch = defaults_word("audio_stretch", "off|on", 1) ? AUDIO_STRETCH_MAX : 0;
	g_cfg.touch_stick = -1;
	input_map_reset();
	if (factory("screen") == 2)
		config_set_core_override(WS_KEY, WS_Q16);
}

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
		*--e = 0;
	return s;
}

static bool last_save_ok;
int config_welcome = -1;
/* PSX3D settings come over once (config_import_psx3d): an odd file must not stall the first start */
#define CONFIG_FILE_MAX (64 * 1024)
#define CONFIG_LINES_MAX 2048
/* A PSX3D core option comes over only when it is one of the core's options with one of its values
 * (options_core_list, loaded by config_import_psx3d), or the widescreen switch BreadBox keeps
 * there. Every other line is read as BreadBox reads its own. */
static bool importable(const char *k, const char *v)
{
	if (strncmp(k, "core.", 5))
		return true;
	k += 5;
	if (!strcmp(k, WS_KEY))
		return !strcmp(v, "0") || !strcmp(v, WS_Q16);
	return options_supported(k, v);
}

/* One settings file into g_cfg: leading/trailing spaces, tabs and CR trimmed, '#'/';' comments,
 * key=value. A line longer than the buffer is dropped whole (its tail is never read as a line).
 * import: PSX3D's file (core values: importable), at most CONFIG_FILE_MAX bytes / CONFIG_LINES_MAX lines. */
static unsigned config_parse(FILE *f, bool import)
{
	char line[PATH_LEN + 64];
	unsigned taken = 0, lines = 0;
	long bytes = 0;
	while (fgets(line, sizeof(line), f)) {
		size_t len = strlen(line);
		bytes += (long)len;
		if (import && (bytes > CONFIG_FILE_MAX || ++lines > CONFIG_LINES_MAX))
			break;
		if (len == sizeof(line) - 1 && line[len - 1] != '\n') {   // too long: skip to its end
			int c;
			while ((c = fgetc(f)) != EOF && c != '\n')
				if (import && ++bytes > CONFIG_FILE_MAX)
					break;
			continue;
		}
		char *l = trim(line), *eq = strchr(l, '=');
		if (*l == '#' || *l == ';' || !eq)
			continue;
		*eq = 0;
		char *k = trim(l), *v = trim(eq + 1);
		if (retired(k) || (!strncmp(k, "core.", 5) && forced_core(k + 5)) || (import && !importable(k, v)))
			continue;
		if (!strcmp(k, seen_key)) {
			if (!import && strlen(v) < sizeof(config_seen_version)) snprintf(config_seen_version, sizeof(config_seen_version), "%s", v);
			continue;
		}
		if (!strcmp(k, "welcome")) {   // the first-launch welcome's flag (welcome.c): not a setting
			if (!import) config_welcome = atoi(v) ? 1 : 0;
			continue;
		}
		if (strcmp(k, "touch_slots") && strcmp(k, "settings_format")) config_user_changed(k);

		if (strncmp(k, "core.", 5) == 0)
			config_set_core_override(k + 5, v);
		else if (strncmp(k, "map_", 4) == 0) {
			for (int i = 0; i < INPUT_REMAP_N; i++)
				if (!strcmp(k + 4, input_map_key_name(i))) {
					// Dense legacy files do not record intent. Preserve every
					// represented binding, even an old factory-valued choice.
					input_map_set(i, atoi(v));
				}
		} else if (strcmp(k, "hotkey_ff") == 0)
			input_hotkey_load(0, v);
		else if (strcmp(k, "hotkey_rewind") == 0)
			input_hotkey_load(1, v);
		else if (strcmp(k, "touch_stick") == 0)
			g_cfg.touch_stick = atoi(v);
		else if (strcmp(k, "touch_click_left") == 0)
			g_cfg.touch_click_left = atoi(v) == 1;
		else if (strcmp(k, "last_dir") == 0)
			snprintf(g_cfg.last_dir, sizeof(g_cfg.last_dir), "%s", v);
		else if (strcmp(k, "games_dir") == 0)
			snprintf(g_cfg.games_dir, sizeof(g_cfg.games_dir), "%s", v);
		else if (strcmp(k, "last_game") == 0)
			snprintf(g_cfg.last_game, sizeof(g_cfg.last_game), "%s", v);
		else if (strcmp(k, "last_game_save") == 0)
			snprintf(g_cfg.last_game_save, sizeof(g_cfg.last_game_save), "%s", v);
		else if (strcmp(k, "last_game_state") == 0)
			snprintf(g_cfg.last_game_state, sizeof(g_cfg.last_game_state), "%s", v);
		else if (strcmp(k, "last_game_path") == 0)
			snprintf(g_cfg.last_game_path, sizeof(g_cfg.last_game_path), "%s", v);
		else if (strcmp(k, "stretch") == 0)
			g_cfg.stretch = atoi(v) != 0;
		else if (strcmp(k, "bottom_off") == 0)
			g_cfg.bottom_off = atoi(v) != 0;
		else if (strcmp(k, "controller") == 0)
			g_cfg.controller = atoi(v) == 1;
		else if (strcmp(k, "debug") == 0)
			g_cfg.debug = atoi(v) != 0;
		else if (strcmp(k, "smooth") == 0)
			g_cfg.smooth = atoi(v) != 0;
		else if (strcmp(k, "stereo_style") == 0)
			g_cfg.stereo_screen = atoi(v) == 1;
		else if (strcmp(k, "show_fps") == 0)
			g_cfg.show_fps = atoi(v) >= 0 && atoi(v) <= 2 ? atoi(v) : 0;
		else if (!strcmp(k, "hand_skin") || !strcmp(k, "hand_gloves")) {
			bool skin = k[5] == 's';
			for (int i = 0; i < (skin ? HAND_SKINS : HAND_GLOVES); i++)
				if (!strcmp(v, skin ? hand_skin_id[i] : hand_glove_id[i])) *(skin ? &g_cfg.hand_skin : &g_cfg.hand_glove) = i;
		}
		else if (strcmp(k, "theme") == 0) {
			g_cfg.theme = theme_from_saved_id(v);   // a room style; retired themes (rustic-room, dark, ...) are Day
		}

		taken++;
	}
	if (g_cfg.touch_stick < -1 || g_cfg.touch_stick > 1) g_cfg.touch_stick = -1;
	return taken;
}

/* One cold scan preserves legacy per-game and per-system handedness. */
static bool legacy_left_file(const char *path)
{
 FILE *f=fopen(path,"r");if(!f)return false;
 char line[160],key[64];int value;bool left=false;
 while(fgets(line,sizeof(line),f))if(sscanf(line,"%63[^=]=%d",key,&value)==2 && value &&
  (!strcmp(key,"touch_click_left") || !strcmp(key,"touch_side")))left=true;
 fclose(f);return left;
}
static bool legacy_left_dir(const char *path)
{
 DIR *d=opendir(path);if(!d)return false;struct dirent *entry;bool left=false;
 while((entry=readdir(d))) {
  size_t n=strlen(entry->d_name);if(n<4 || strcmp(entry->d_name+n-4,".ini"))continue;
  char file[PATH_LEN+512];snprintf(file,sizeof(file),"%s/%s",path,entry->d_name);
  if(legacy_left_file(file)){left=true;break;}
 }
 closedir(d);return left;
}
static void migrate_touch_side(void)
{
 const char *marker=APP_SD_ROOT "/config/touch-side-migrated-v1";
 FILE *f=fopen(marker,"r");if(f){fclose(f);return;}
 bool left=g_cfg.touch_click_left || legacy_left_file(PSX3D_CONFIG) || legacy_left_dir(APP_SD_ROOT "/games") || legacy_left_dir(APP_SD_ROOT "/config");
 if(left){g_cfg.touch_click_left=1;config_user_changed("touch_click_left");config_save();}
 mkdir(APP_SD_ROOT "/config",0777);f=safe_begin(marker);
 if(f){fputs("1\n",f);safe_finish(f,marker,true);}
}

void config_load(void)
{
	FILE *f;

	clear_overrides();
	nuser = 0;
	set_defaults();
	config_welcome = -1;
	config_seen_version[0] = 0;
	safe_recover(PSX3D_CONFIG);
	f = fopen(PSX3D_CONFIG, "r");
	if (!f) {migrate_touch_side();return;}
	config_parse(f, false);
	fclose(f);
	theme_menu_refresh();
	g_cfg.bottom_off = 1;
	migrate_touch_side();
	config_sync_stereo();
}

/* J-06: a PSX3D player's app-wide settings come over on BreadBox's very first start: PSX3D's
 * config.ini is only read, never changed. It is read like BreadBox's own (config_parse), minus its
 * Continue lines (the save import brings Continue), and BreadBox writes its own file from what it
 * understood (config_save): unknown keys and values stay behind. Never when BreadBox has settings
 * of its own; a file over CONFIG_FILE_MAX is left alone. */
PSX3D_COLD bool config_present(void)
{
	static const char *const mine[] = { PSX3D_CONFIG, PSX3D_CONFIG ".bak", PSX3D_CONFIG ".tmp" };
	struct stat st;
	for (unsigned i = 0; i < 3; i++)
		if (!stat(mine[i], &st))
			return true;
	return false;
}

bool config_import_psx3d(void)
{
	struct stat st;
	if (config_present())
		return false;
	FILE *in = fopen(APP_LEGACY_PSX3D_ROOT "/config.ini", "r");
	if (!in)
		in = fopen(APP_LEGACY_PSX3D_ROOT "/config.ini.bak", "r");
	if (!in)
		return false;
	if (!fstat(fileno(in), &st) && st.st_size > CONFIG_FILE_MAX) {
		fclose(in);
		log_printf("config: PSX3D app settings NOT copied (%ld bytes)\n", (long)st.st_size);
		return false;
	}
	clear_overrides();
	nuser = 0;
	set_defaults();
	config_welcome = -1;   // BreadBox's own first start decides it (welcome.c), never PSX3D's file
	options_core_list();
	unsigned n = config_parse(in, true);
	/* PSX3D's Continue never comes over here (the save import brings it, validated): the four
	 * Continue fields, last_game through last_game_path (app_config.h), are cleared at once */
	_Static_assert(offsetof(psx3d_config, last_game_path) - offsetof(psx3d_config, last_game) == 3 * sizeof(g_cfg.last_game),
		"Continue fields are contiguous");
	memset(g_cfg.last_game, 0, offsetof(psx3d_config, last_game_path) + sizeof(g_cfg.last_game_path) - offsetof(psx3d_config, last_game));
	bool ok = !ferror(in);
	fclose(in);
	if (ok) {
		config_save();
		ok = last_save_ok;
	}
	log_printf("config: PSX3D app settings %s (%u settings)\n", ok ? "copied" : "NOT copied", n);
	return ok;
}

bool config_last_save_ok(void) {return last_save_ok;}
void config_save(void)
{
 last_save_ok=false;
 if(!bb_writer_begin())return;
	if (sw_o3ds) {bb_writer_end();return;}   // test mode: leave the real settings alone
	// In a game these belong to the game; config.ini keeps the all-games values.
	int game_values[GAME_FIELDS] = {0};
	if (in_game) {
		game_fields_store();
		for (int i = 0; i < GAME_FIELDS; i++) {
			game_values[i] = *game_fields[i];
			*game_fields[i] = default_fields[i];
		}
	}
	FILE *f = safe_begin(PSX3D_CONFIG);
	if (!f)
		goto done;
	fprintf(f, "# " APP_NAME " settings\nsettings_format=3\ntouch_slots=1\n");
	fprintf(f, "last_dir=%s\n", g_cfg.last_dir);
	if (g_cfg.games_dir[0])
		fprintf(f, "games_dir=%s\n", bb_games_dir_session && bb_unreadable_dir[0] ? bb_unreadable_dir : g_cfg.games_dir);   // a session fallback never replaces the saved folder
	if (g_cfg.last_game_state[0])
		fprintf(f, "last_game=%s\nlast_game_save=%s\nlast_game_state=%s\nlast_game_path=%s\n",
			g_cfg.last_game, g_cfg.last_game_save, g_cfg.last_game_state, g_cfg.last_game_path);
	int screen = in_game ? default_screen : config_screen_mode();
#define SAVE_INT(key, value, factory) do { int v_ = (value); if (user_has(key) || v_ != (factory)) fprintf(f, key "=%d\n", v_); } while (0)
	SAVE_INT("smooth", g_cfg.smooth, factory("smooth"));
	SAVE_INT("show_fps", g_cfg.show_fps, factory("show_fps"));
	SAVE_INT("stereo_style", g_cfg.stereo_screen, factory("stereo_style"));
	if (g_cfg.theme != THEME_FACTORY())   // a look, not an intent: the default is never written
		fprintf(f, "theme=%s\n", theme_get(g_cfg.theme)->id);
	if (g_cfg.hand_skin != 1 && (unsigned)g_cfg.hand_skin < HAND_SKINS) fprintf(f, "hand_skin=%s\n", hand_skin_id[g_cfg.hand_skin]);
	if (g_cfg.hand_glove > 0 && g_cfg.hand_glove < HAND_GLOVES) fprintf(f, "hand_gloves=%s\n", hand_glove_id[g_cfg.hand_glove]);
	SAVE_INT("stretch", screen == 1, factory("screen") == 1);
	SAVE_INT("touch_stick", in_game ? default_touch_stick : g_cfg.touch_stick, -1);
	SAVE_INT("touch_click_left", g_cfg.touch_click_left, factory("touch_click_left"));
	SAVE_INT("controller", in_game ? default_controller : g_cfg.controller, factory("controller"));
	for (int i = 0; i < INPUT_HOTKEY_N; i++) {
		int v = in_game ? default_hotkeys[i] : input_hotkey_get(i);
		if (user_has(input_hotkey_key_name(i)) || v != input_hotkey_factory(i))
			fprintf(f, "%s=%s\n", input_hotkey_key_name(i), input_hotkey_combo_name(v));
	}
	for (int i = 0; i < INPUT_REMAP_N; i++) {
		char key[64]; snprintf(key, sizeof(key), "map_%s", input_map_key_name(i));
		int v = in_game ? default_maps[i] : input_map_raw(i);
		int factory = i >= INPUT_PHYSICAL_N ? INPUT_BIND_PRESET : input_map_factory(i);
		if (user_has(key) || v != factory) fprintf(f, "%s=%d\n", key, v);
	}
#undef SAVE_INT
	if (g_cfg.debug)
		fprintf(f, "debug=1\n");
	if (config_seen_version[0]) fprintf(f, seen_format, config_seen_version);
	if (config_welcome >= 0)   // kept by every reset: it is not a setting
		fprintf(f, "welcome=%d\n", config_welcome);
	for (int i = 0; i < novr; i++) {
		char key[64]; snprintf(key, sizeof(key), "core.%s", ovr[i].key);
		if (retired(key) || !user_has(key) || forced_core(ovr[i].key)) continue; // internal synchronization is never user intent
		if (strcmp(ovr[i].key, WS_KEY) == 0)
			fprintf(f, "core.%s=%s\n", ovr[i].key, screen == 2 ? WS_Q16 : "0");
		else fprintf(f, "core.%s=%s\n", ovr[i].key, ovr[i].value);
	}
	last_save_ok=safe_finish(f, PSX3D_CONFIG, true);
done:
 bb_writer_end();
	if (in_game)
		for (int i = 0; i < GAME_FIELDS; i++) *game_fields[i] = game_values[i];
}

const char *config_core_override(const char *key)
{
	for (int i = 0; i < novr; i++)
		if (strcmp(ovr[i].key, key) == 0)
			return ovr[i].value;
	return NULL;
}

void config_set_core_override(const char *key, const char *value)
{
	// Retired settings never influence Auto or get written back to config.ini.
	if (!strcmp(key, "pcsx_rearmed_frameskip_threshold") ||
	    !strcmp(key, "pcsx_rearmed_frameskip_interval")) return;
	if (!strcmp(key, "pcsx_rearmed_frameskip_type") &&
	    (!strcmp(value, "auto_threshold") || !strcmp(value, "fixed_interval"))) value = "auto";
	for (int i = 0; i < novr; i++) {
		if (strcmp(ovr[i].key, key) == 0) {
			char *next = strdup(value);
			if (!next) return;
			free(ovr[i].value);
			ovr[i].value = next;
			return;
		}
	}
	if (novr < MAX_OVERRIDES) {
		char *next_key = strdup(key), *next_value = next_key ? strdup(value) : NULL;
		if (!next_value) { free(next_key); return; }
		ovr[novr].key = next_key;
		ovr[novr].value = next_value;
		novr++;
	}
}

// Reset settings while keeping the library location and selection. Core option
// lookup supplies the console-specific defaults in options.c once overrides go.
void config_reset_defaults(void)
{
	if (in_game) return; // global reset is offered in the library
	psx3d_config old = g_cfg;
	char app_intent[16][64];int nintent=0;
 for(int i=0;i<nuser && nintent<16;i++) {
  const char *k=user_keys[i];bool keep=!strcmp(k,"theme") || !strcmp(k,"hand_skin") || !strcmp(k,"hand_glove") || !strcmp(k,"sounds") || !strcmp(k,"music") || !strcmp(k,"debug") || !strcmp(k,"touch_click_left");
  for(int h=0;h<INPUT_HOTKEY_N;h++)if(!strcmp(k,input_hotkey_key_name(h)))keep=true;
  if(keep)snprintf(app_intent[nintent++],64,"%s",k);
 }
 int hotkeys[INPUT_HOTKEY_N];
	for (int i = 0; i < INPUT_HOTKEY_N; i++)
		hotkeys[i] = input_hotkey_get(i);
	clear_overrides();
	nuser = 0;
	set_defaults();
	strcpy(g_cfg.last_dir, old.last_dir);
	strcpy(g_cfg.games_dir, old.games_dir);
	strcpy(g_cfg.last_game, old.last_game);
	strcpy(g_cfg.last_game_save, old.last_game_save);
	strcpy(g_cfg.last_game_state, old.last_game_state);
	strcpy(g_cfg.last_game_path, old.last_game_path);
	// This is the PlayStation reset: the app's own values (look, hands, interface sound and music, developer tools)
	// and the fast-forward / rewind hotkeys every system shares are not PlayStation settings and stay.
	for(int i=0;i<nintent;i++)config_user_changed(app_intent[i]);
	g_cfg.theme = old.theme;
	g_cfg.hand_skin = old.hand_skin;
	g_cfg.hand_glove = old.hand_glove;
	g_cfg.sounds = old.sounds;
	g_cfg.music = old.music;
	g_cfg.debug = old.debug;
 g_cfg.touch_click_left=old.touch_click_left;
	for (int i = 0; i < INPUT_HOTKEY_N; i++)
		input_hotkey_set(i, hotkeys[i]);
	theme_menu_refresh();
	config_set_screen_mode(0);
	config_sync_stereo();
	options_mark_updated();
	config_save();
}

// Widescreen hack: the GTE squeezes 3D geometry horizontally by 4:3 / 5:3 =
// 0.8, so games draw a wider view that fills the 400x240 top screen.

bool config_widescreen(void)
{
	const char *v = config_core_override(WS_KEY);
	return v && strcmp(v, "0") != 0;
}

void config_set_widescreen(bool on)
{
	config_set_core_override(WS_KEY, on ? WS_Q16 : "0");
	options_mark_updated();
}

// Real 3D needs the core to record vertex depths, which costs a few ms per
// frame, so it's only on while the 3D slider is up (emu.c follows the slider).
static bool stereo_active;

void config_sync_stereo(void)
{
	// Enforce the console limit for defaults, legacy files, resets and debug.
	// video.c consumes this same field, so Old 3DS never draws a second eye.
	// No 3D setting: the slider alone turns Real 3D on (New 3DS, defaults.ini).
	g_cfg.stereo = factory("stereo");
	if (!ps1_3d_available()) g_cfg.stereo = 0;
	// The 3D setting: Screen depth puts the whole picture behind the screen
	// (v0.5.0's 3D) instead of Real 3D; the slider still sets the strength.
	if (g_cfg.stereo == 2 && g_cfg.stereo_screen) g_cfg.stereo = 1;
	config_set_core_override("pcsx_rearmed_psx3d_stereo",
		stereo_active ? "enabled" : "disabled");
	options_mark_updated();
}

void config_set_stereo_active(bool on)
{
	if (on == stereo_active)
		return;
	stereo_active = on;
	config_sync_stereo();
}

// Screen: 0 = 4:3, 1 = stretched to 400x240, 2 = widescreen (the game draws
// a wider view, shown full width). Widescreen comes only from the game's own
// widescreen fix: config_ws_fix() is the one test (today its cheat code; the
// curated bundles replace cheats_widescreen() there). Games without a fix show
// 4:3, and the GTE squeeze is never sent to the core (options.c).
static bool ws_fix, ws_selected;
static int legacy_mode = -1;   // games.ini (0.1.1) screen mode of this game
bool config_ws_fix(void) { return ws_fix; }

// After the game's cheats load and whenever its screen mode changes.
void config_ws_sync(void)
{
	// The preset rules widescreen out (hidden: 2D games) or marks its cost.
	// "off" (WS_TOO_SLOW, a large speed cost) still offers the fix, but only
	// an explicit per-game choice turns it on: the all-games choice does not.
	int policy = ws_console_policy();   // the loaded bundle carries the preset's value
	if (policy < 0) policy = game_capabilities().widescreen;
	ws_policy_override(policy == WS_TOO_SLOW);
	bool chosen = in_game && (gamecfg_int("screen", legacy_mode) == 2 || gamecfg_int("preset_widescreen", 0));
	ws_selected = policy == WS_OFFERED || policy == WS_COSTLY || chosen;
	bool ok = in_game && ws_available() && policy != WS_HIDDEN;
	ws_fix = cheats_widescreen(ok && ws_selected && config_widescreen()) && ok;
}

int config_screen_mode(void)
{
	int mode = config_widescreen() ? 2 : (g_cfg.stretch ? 1 : 0);
	return mode == 2 && in_game && (!ws_fix || !ws_selected) ? 0 : mode;
}

void config_set_screen_mode(int mode)
{
	g_cfg.stretch = mode == 1;
	config_set_widescreen(mode == 2);
}

// ---------------------------------------------------------------- per game
// sdmc:/3ds/PSX3D/games.ini: "<game>=<screen mode>". Widescreen only suits
// some games (others cull what they think is off-screen and the edges come
// up empty), so the screen mode is remembered for each game.
#define GAMES_INI PSX3D_DIR "/games.ini"

static int game_screen_get(const char *game)
{
	char line[PATH_LEN + 16];
	int mode = -1;
	safe_recover(GAMES_INI);
	FILE *f = fopen(GAMES_INI, "r");
	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		char *eq = strrchr(line, '=');
		if (!eq)
			continue;
		*eq = 0;
		if (strcmp(trim(line), game) == 0)
			mode = atoi(eq + 1) % 3;
	}
	fclose(f);
	return mode;
}

static char cur_game[PATH_LEN];
static bool legacy_screen_override;

// a game starts: use its own screen mode if it has one
void config_game_begin(const char *game)
{
	snprintf(cur_game, sizeof(cur_game), "%s", game);
	default_screen = config_screen_mode();
	for (int i = 0; i < INPUT_REMAP_N; i++) default_maps[i] = input_map_raw(i);
 control_bindings_snapshot();
	default_touch_stick = g_cfg.touch_stick;
	default_controller = g_cfg.controller;
	for (int i = 0; i < INPUT_HOTKEY_N; i++) default_hotkeys[i] = input_hotkey_get(i);
	for (int i = 0; i < GAME_FIELDS; i++) default_fields[i] = *game_fields[i];
	in_game = true;
	gamecfg_load(game);
	for (int i = 0; i < GAME_FIELDS; i++) *game_fields[i] = gamecfg_int(game_field_keys[i], default_fields[i]);
	config_sync_stereo();
	for (int i = 0; i < INPUT_HOTKEY_N; i++) {
		const char *name = gamecfg_get(input_hotkey_key_name(i));
		if (name) input_hotkey_load(i, name);
	}
	for (int i = 0; i < INPUT_REMAP_N; i++) {
		char key[32];
		snprintf(key, sizeof(key), "map_%s", input_map_key_name(i));
		input_map_set(i, gamecfg_int(key, default_maps[i]));
	}
	g_cfg.touch_stick = gamecfg_int("touch_stick", default_touch_stick);
	if(g_cfg.touch_stick < -1 || g_cfg.touch_stick > 1) g_cfg.touch_stick = -1;
	input_touch_begin();
	int mode = gamecfg_int("screen", -1);
	legacy_screen_override = false;
	legacy_mode = -1;
	if (mode < 0) {
		mode = legacy_mode = game_screen_get(game);   // 0.1.1 kept it in games.ini
		legacy_screen_override = mode >= 0;
	}
	if (mode >= 0 && mode != default_screen)
		config_set_screen_mode(mode);
	log_printf("screen: %d (%s)\n", config_screen_mode(), mode >= 0 ? "this game" : "default");
}

// the screen mode changed in the menu: remember it for this game
void config_game_set_screen(int mode)
{
	config_set_screen_mode(mode);
	if (in_game) {
		gamecfg_set_int("screen", mode);
		gamecfg_unset("preset_widescreen");   // written by the old Show all settings
	}
}

// back to the library: the default screen mode again
void config_game_end(void)
{
	if (!in_game)
		return;
	config_set_stereo_active(false);
	in_game = false;
	gamecfg_end();
	game_fields_restore();
	input_touch_begin();
	for (int i = 0; i < INPUT_REMAP_N; i++) input_map_set(i, default_maps[i]);
	for (int i = 0; i < INPUT_HOTKEY_N; i++) input_hotkey_set(i, default_hotkeys[i]);
	g_cfg.touch_stick = default_touch_stick;
	g_cfg.controller = default_controller;
	input_touch_reset();
	ws_fix = false;
	if (config_screen_mode() != default_screen)
		config_set_screen_mode(default_screen);
}

// Store only differences, so untouched controls continue to inherit globals.
void config_controls_save(void)
{
	if (!in_game) { config_save(); return; }
	gamecfg_unset("touch_controls"); // replaced by independent slot bindings
	gamecfg_unset("map_TOUCH_SELECT"); // obsolete: SELECT is a physical button
	for (int i = 0; i < INPUT_HOTKEY_N; i++) {
		const char *key = input_hotkey_key_name(i);
		if (input_hotkey_get(i) == default_hotkeys[i]) gamecfg_unset(key);
		else gamecfg_set(key, input_hotkey_combo_name(input_hotkey_get(i)));
	}
	for (int i = 0; i < INPUT_REMAP_N; i++) {
		char key[32];
		snprintf(key, sizeof(key), "map_%s", input_map_key_name(i));
		if (input_map_raw(i) == default_maps[i]) gamecfg_unset(key);
		else gamecfg_set_int(key, input_map_raw(i));
	}
	if (g_cfg.touch_stick == default_touch_stick) gamecfg_unset("touch_stick");
	else gamecfg_set_int("touch_stick", g_cfg.touch_stick);
}

void config_controls_defaults(void)
{
	if (in_game) {
		for (int i = 0; i < INPUT_REMAP_N; i++) input_map_set(i, default_maps[i]);
		for (int i = 0; i < INPUT_HOTKEY_N; i++) input_hotkey_set(i, default_hotkeys[i]);
		g_cfg.touch_stick = default_touch_stick;

	} else {
		for (int i = 0; i < INPUT_REMAP_N; i++) { char key[64]; snprintf(key, sizeof(key), "map_%s", input_map_key_name(i)); user_forget(key); }
		for (int i = 0; i < INPUT_HOTKEY_N; i++) user_forget(input_hotkey_key_name(i));
		user_forget("touch_stick"); user_forget("touch_click_left");
		input_map_reset();
		g_cfg.touch_stick = -1;
		g_cfg.touch_click_left = 0;
	}
	config_controls_save();
	input_touch_reset();
}

// Remove the pre-0.2 screen entry too, otherwise it would reappear next launch.
static bool forget_legacy_screen(void)
{
	safe_recover(GAMES_INI);
	FILE *in = fopen(GAMES_INI, "r");
	if (!in) return errno == ENOENT;
	FILE *out = safe_begin(GAMES_INI);
	if (!out) { fclose(in); return false; }
	char line[PATH_LEN + 16];
	bool ok = true, found = false;
	while (fgets(line, sizeof(line), in)) {
		char copy[sizeof(line)];
		strcpy(copy, line);
		char *eq = strrchr(copy, '=');
		if (eq) *eq = 0;
		if (eq && !strcmp(trim(copy), cur_game)) found = true;
		else if (fputs(line, out) < 0) ok = false;
	}
	if (ferror(in)) ok = false;
	fclose(in);
	// leave games.ini alone without an entry
	return safe_finish(out, GAMES_INI, ok && found) || (ok && !found);
}

bool config_game_reset(void)
{
	if (!in_game || !forget_legacy_screen() || !gamecfg_reset()) return false;
	legacy_screen_override = false;
	legacy_mode = -1;
	config_set_screen_mode(default_screen);
	game_fields_restore();
	options_mark_updated();
	config_controls_defaults();
	return true;
}

// Clear one override without changing global values or unrelated game keys.
bool config_game_clear(const char *key)
{
	if (!in_game)
		return false;
	if (!strcmp(key, "screen")) {
		if (!forget_legacy_screen())
			return false;
		gamecfg_unset(key);
		gamecfg_unset("preset_widescreen");
		legacy_screen_override = false;
		legacy_mode = -1;
		config_set_screen_mode(default_screen);
	} else if (!strcmp(key, "rewind") || !strcmp(key, "controller")) {
		gamecfg_unset(key); options_mark_updated();
	}
	else if (!strncmp(key, "o.", 2)) {
		gamecfg_unset(key);
		options_mark_updated();
	} else {
		int i = 0;
		while (i < GAME_FIELDS && strcmp(key, game_field_keys[i])) i++;
		if (i == GAME_FIELDS)
			return false;
		gamecfg_unset(key);
		*game_fields[i] = default_fields[i];
		config_sync_stereo();
		input_touch_reset();
	}
	return true;
}

void config_control_clear(int row)
{
	// Input rows depend on console/preset visibility. Match their persistent keys through
	// the public row helper to avoid changing that visibility or saved formats.
	const char *key = input_control_key(row);
	if (!in_game || !key)
		return;
	gamecfg_unset(key);
	if (!strcmp(key, "touch_stick"))
		g_cfg.touch_stick = default_touch_stick;
	for (int i = 0; i < INPUT_REMAP_N; i++) {
		char name[32];
		snprintf(name, sizeof(name), "map_%s", input_map_key_name(i));
		if (!strcmp(key, name))
			input_map_set(i, default_maps[i]);
	}
	for (int i = 0; i < INPUT_HOTKEY_N; i++)
		if (!strcmp(key, input_hotkey_key_name(i)))
			input_hotkey_set(i, default_hotkeys[i]);
	input_touch_reset();
}

bool config_game_overridden(const char *key)
{
	return in_game && (gamecfg_get(key) || (!strcmp(key, "screen") && legacy_screen_override));
}

// Controller (technical layer): the player's per-game choice, the game's
// tested value, the all-games choice, then defaults.ini. 1 = analog.
int config_controller(void)
{
	if (in_game && gamecfg_get("controller"))
		return gamecfg_int("controller", 0) == 1;
	if (in_game && game_capabilities().controller >= 0)
		return game_capabilities().controller;
	return in_game ? default_controller : g_cfg.controller;
}
// Rewind (New 3DS, per game): on unless the player turned it off for this game.
bool config_rewind_on(void)
{
	return g_new3ds && in_game && gamecfg_int("rewind", factory("rewind")) != 0;
}
// Technical overrides only: Display, Sound, Controls and Saves stay.
void config_game_reset_advanced(const char *const *keys, int n)
{
	if (!in_game) return;
	for (int i = 0; i < n; i++)
		gamecfg_unset(keys[i]);
	options_mark_updated();
}


int control_side(void) {return g_cfg.touch_click_left;}
void control_side_set(int left) {g_cfg.touch_click_left=!!left;config_save();}

bool config_setting_changed(const char *key){return key && user_has(key);}
void config_user_forget(const char *key){user_forget(key);}
int config_factory_value(const char *key){return factory(key);}
void config_core_default(const char *key)
{
 for(int i=0;i<novr;i++)if(!strcmp(ovr[i].key,key)) {
  free(ovr[i].key);free(ovr[i].value);
  memmove(ovr+i,ovr+i+1,(--novr-i)*sizeof(ovr[0]));break;
 }
 options_mark_updated();
}
void config_room_defaults(void)
{
 room_defaults();
 user_forget("theme");user_forget("hand_skin");user_forget("hand_gloves");
 theme_menu_refresh();config_save();
}

const char *bb_settings_hotkey_exception(int slot)
{return in_game && slot>=0 && slot<INPUT_HOTKEY_N?gamecfg_get(input_hotkey_key_name(slot)):NULL;}
int bb_settings_hotkey_get(int slot)
{ return slot>=0 && slot<INPUT_HOTKEY_N ? (in_game?default_hotkeys[slot]:input_hotkey_get(slot)):0; }
static void shared_hotkey_change(int slot,int direction,bool reset)
{
 if(slot<0 || slot>=INPUT_HOTKEY_N)return;
 int effective=input_hotkey_get(slot);
 bool exception=in_game && gamecfg_get(input_hotkey_key_name(slot));
 input_hotkey_set(slot,bb_settings_hotkey_get(slot));
 if(reset)input_hotkey_set(slot,input_hotkey_factory(slot));else input_hotkey_cycle(slot,direction);
 if(in_game){default_hotkeys[slot]=input_hotkey_get(slot);if(exception)input_hotkey_set(slot,effective);}
 config_user_changed(input_hotkey_key_name(slot));config_save();
}
void bb_settings_hotkey_cycle(int slot,int direction){shared_hotkey_change(slot,direction,false);}
void bb_settings_hotkey_default(int slot){shared_hotkey_change(slot,0,true);}

