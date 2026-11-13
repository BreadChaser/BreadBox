/* BreadBox system interface (ABI 13).
 * See the maintained architecture documentation for the design and plan.
 *
 * A system module (PS1, GBA, SNES...) fills one const bb_system_t. The shell
 * (home, systems page, favorites, library list, covers, settings pages,
 * pause shell, save service, dev remote) only talks to systems through it.
 *
 * The shell-side registry and session control live in bb_registry.h.
 *
 * Hot-path rule: nothing here is called per emulated frame by the shell.
 * The system's run() owns its frame loop and calls shell services directly
 * (video_present, audio, input, bb_session_tick). Descriptor calls happen on
 * scan (index worker thread), menu actions, launch/stop, APT events and dev
 * commands only. The registry is a const table: no allocation, no lookup on
 * any per-frame path.
 */
#ifndef BB_SYSTEM_H
#define BB_SYSTEM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BB_SYSTEM_ABI 13       /* bump when the struct below changes */
#define BB_KEY_LEN 32          /* content key incl. NUL: [A-Z0-9-] only */
#define BB_TITLE_LEN 96

typedef struct bb_system bb_system_t;
typedef struct {
 int (*count)(void);
 const char *(*label)(int);
 const char *(*value)(int);
 void (*toggle)(int);
 const char *(*add)(void);
} bb_cheat_ops;

/* ---- Console models ----------------------------------------------------- */
enum { BB_OLD = 0, BB_NEW = 1, BB_MODELS = 2 };
#define BB_MODEL_OLD (1u << BB_OLD)
#define BB_MODEL_NEW (1u << BB_NEW)
#define BB_MODEL_ALL (BB_MODEL_OLD | BB_MODEL_NEW)

/* ---- Identity ------------------------------------------------------------
 * One key per game per system; every per-game file uses it:
 *   saves/<id>/<key>.*, states/<id>/<key>.*, backups/<id>/<key>/,
 *   games/<id>/<key>.ini, playlog/favorites "<id>:<key>".
 * PS1: serial ("SLUS-00594", saveid suffix on collision).
 * GBA: game code + revision + size class ("AXVE-01-8M"); the owner record
 *      (saveid-style: header CRC + sampled ROM CRC) adds "-<crc>" on collision. */
typedef struct {
	char key[BB_KEY_LEN];
	uint32_t crc;              /* system-defined strong check, 0 = none */
} bb_content_id;

/* ---- Library scan (runs on the index worker thread, no UI/GPU) ---------- */
typedef enum { BB_PROBE_NO, BB_PROBE_MAYBE, BB_PROBE_YES } bb_probe_result;

typedef struct {
	const char *path, *name;   /* full path, file name */
	uint64_t size;             /* zero if probe_bytes=0 (adapter owns I/O) */
	const uint8_t *head;       /* first head_len bytes, read once by the worker */
	size_t head_len;           /* <= bb_system_t.probe_bytes */
	bool folder_match;         /* a parent folder matched this system's hints */
} bb_probe_input;

typedef struct {               /* what the shared index keeps per entry */
	char title[BB_TITLE_LEN];  /* clean display title */
	char key[BB_KEY_LEN];      /* quick key; launch confirms it (identify) */
	char cover[64];            /* cover lookup name in cover_db, "" = by title */
	uint32_t group;            /* entries with the same group show as one game (discs) */
	uint8_t part;              /* disc number inside the group, 0 = single */
	uint8_t flags;             /* BB_ENTRY_* */
} bb_entry;
#define BB_ENTRY_HIDDEN 1u     /* referenced by another entry (CUE track, M3U disc) */

typedef struct {               /* one game for the cover worker; strings are borrowed */
	const char *title;         /* display title */
	const char *key;           /* cover cache key "<id>/<name>" (or the PS1 No-Intro name) */
	const char *code;          /* product code for cover_candidates, NULL = none */
	const char *path;          /* the game file (first disc), NULL = none */
} bb_cover_item;

/* ---- Settings schema (one table drives system and game pages) ----------- */
typedef enum { BB_SET_CHOICE, BB_SET_TOGGLE, BB_SET_SLIDER, BB_SET_ACTION } bb_setting_kind;
enum { BB_CAT_DISPLAY, BB_CAT_AUDIO, BB_CAT_CONTROLS, BB_CAT_PERFORMANCE, BB_CAT_ADVANCED };
#define BB_SET_LIVE      1u    /* applies while playing, no reload */
#define BB_SET_GAME_ONLY 2u    /* not on the system page */
typedef struct {
	const char *key;           /* stable config/preset key */
	const char *label, *help;
	const char *const *values; /* CHOICE labels; NULL for TOGGLE/SLIDER */
	uint8_t kind, category, models, flags, nvalues;
	int16_t def[BB_MODELS];    /* built-in default per console model */
} bb_setting_desc;
/* Value order (shell-owned): game change > curated preset > system setting > def[]. */

/* ---- Memory declaration (checked by the shell before launch) ------------ */
typedef struct {
	uint32_t heap_kib;         /* worst game, after the menu released its buffers */
	uint32_t linear_kib;
	uint32_t exec_kib;         /* slice of the shared executable arena */
	uint32_t keep_kib;         /* still held after stop(): must be 0 */
} bb_budget;

/* ---- Launch / run --------------------------------------------------------- */
#define BB_SLOT_NONE (-1)
#define BB_SLOT_CONTINUE 10
#define BB_SLOT_UNDO 11
#define BB_SLOT_QUICK 12 /* private hotkey state, outside the postcard slots (bb_slots.h: those are 1..12 = files 0..9, 13, 14) */
typedef struct {
	const char *save_name, *title; /* PS1 legacy save/display names, optional */
	const char *path;          /* content path as listed, including archives/playlists */
	const bb_entry *entry;
	bb_content_id id;          /* confirmed by identify() */
	const char *save_dir;      /* "sdmc:/3ds/BreadBox/saves/<id>" */
	int slot;                  /* BB_SLOT_* or 0..9 */
	bool remote;               /* dev remote load: no prompts */
 bool resume;               /* shell Continue selection: no second prompt */
	int8_t stereo;             /* -1 = setting, else forced value */
} bb_launch;

typedef enum { BB_RUN_QUIT, BB_RUN_FAILED, BB_RUN_STALLED, BB_RUN_RELOAD } bb_run_result;

/* ---- Save service hooks (systems that use the shared save service) ------
 * RAM only: no file I/O, no GPU. The shell writes files with safefile,
 * keeps the history ring, checks owners (saveid) and never lets a state
 * roll back a newer battery save. */
typedef enum { BB_STATE_OK, BB_STATE_DAMAGED, BB_STATE_PARTLY, BB_STATE_OTHER } bb_state_result;
typedef struct {
	size_t (*state_size)(void);
	bool (*state_save)(void *buf, size_t cap, size_t *len);
	bb_state_result (*state_load)(const void *buf, size_t len, bool keep_battery);
	bool (*battery)(void **data, size_t *size);    /* live SRAM/EEPROM/flash, false = none */
	uint32_t (*battery_generation)(void);          /* changes when the game wrote it */
 /* Optional compatibility codec. NULL selects shell version-1 defaults.
  * Eight magic bytes; state payload is serialized core + battery snapshot.
  * RTC is RAM-only and opt-in, wrapped with magic and CRC by the shell.
 * RTC buffers passed to these ops are word-aligned. */
 const char *state_magic, *rtc_magic;
 size_t (*rtc_size)(void);
 bool (*rtc_get)(void *,size_t), (*rtc_set)(const void *,size_t);
 bool (*rtc_valid)(const void *,size_t);
 /* Optional: true when a smaller battery file of exactly this many bytes could be this cartridge's
  * own save as other emulators write it (its chip's size; a size that contradicts a save type the
  * core knows before the game runs is not). Never decides anything by itself: it only adds "Use
  * it" to the damaged-save question; on that answer the original goes into Backups and the file
  * is padded with battery_fill to the core's size. */
 bool (*battery_native)(size_t bytes);
 unsigned char battery_fill;
} bb_save_ops;

/* ---- Optional hooks ------------------------------------------------------- */
typedef struct {               /* dev remote; any member may be NULL */
	bool (*state)(int slot, bool save);
	bool (*key)(const char *name, unsigned frames);
	bool (*option)(const char *word); /* NULL resets per-launch diagnostic options */
	int (*status)(char *json, size_t cap); /* JSON members including leading comma */
	int (*sample)(char *line, size_t cap);         /* one stats line per second */
} bb_dev_ops;

typedef struct {               /* multiplayer (reserved; NULL until v0.6 work lands) */
	bool (*picture)(const void **pixels, unsigned *w, unsigned *h, unsigned *pitch);
	void (*set_player_input)(unsigned port, uint32_t buttons);
	bool (*step_deterministic)(void);
} bb_mp_ops;

typedef struct {               /* extra pause/settings rows owned by the system */
	const char *label;
	const char *(*value)(void);
	bool (*activate)(int dir); /* A: dir 1; arrows rows: left -1, right 1. true = close the pause menu */
	bool (*visible)(void);     /* NULL = always shown */
	bool (*enabled)(void);     /* NULL = enabled; false: drawn faint, A still calls activate to explain why */
	unsigned char icon, tone;  /* UI_ICON_*, UI_TONE_* of the pause card */
	bool arrows;               /* left / right change the value */
} bb_pause_row;

/* ---- Link kind -------------------------------------------------------------- */
enum { BB_LINK_STATIC, BB_LINK_CHAINLOAD };    /* chainload: separate executable,
                                                  e.g. a core with an incompatible licence */

typedef struct {
 char title[BB_TITLE_LEN], key[256], path[512];
 int cover_id;
 int discs;                   /* discs of the game when the library knows (PS1), 0 = unknown */
} bb_catalog_item;
/* Legacy library presentation may supply grouped rows while the shared index
 * remains the fallback for a new adapter. */
/* UI captures an owned path candidate without I/O; the index worker resolves
 * it without borrowing library arrays. Release only after worker publication. */
typedef struct {
 void *data;
 bool (*resolve)(void *data,char *path,size_t cap);
 void (*release)(void *data);
} bb_path_snapshot;
/* Transitional shell operations: existing libraries/settings stay inside
 * adapters until S6-S9 replace them with shared services. */
typedef struct {
 bool legacy_continue;  /* Continue reads the PS1 savestate metadata and thumbnail (states_auto_read) */
 bool (*continue_capture)(const char *title,bb_path_snapshot *out);
 bool (*poll_library)(bool refresh); /* UI thread: publish prepared index records without file I/O */
 bool (*catalog)(int row,bb_catalog_item *out);
 bool (*launch)(int row,char *path,size_t cap);
 bool (*init)(void);
 void (*deinit)(void);
 void (*release_menu)(void);
 void (*resume)(bool enabled);
 const char *(*title)(void);
 int (*count)(void);
 bool (*continue_path)(const char *path, unsigned *saved);
 bool (*resolve)(const char *title, char *path, size_t cap);
 bool (*parse_switch)(const char *arg);
 const char *(*autostart)(void);
} bb_shell_ops;

/* ---- Packaging: how the system's games stand on the BreadBox shelf --------- */
enum { BB_PACKAGE_CARTON, BB_PACKAGE_CASE };   /* cardboard box; clear case with a hinge */
typedef struct {
	float width, height, depth;  /* mm: cover width and height, spine thickness */
	uint8_t style;               /* BB_PACKAGE_* */
	uint32_t spine_head, band;   /* colours (r | g<<8 | b<<16 | a<<24): spine top, cover side band */
	const char *band_label;      /* printed down the cover's side band; NULL = no band */
} bb_package;

/* ---- The descriptor --------------------------------------------------------- */
struct bb_settings_ops;
struct bb_system {
 const struct bb_settings_ops *settings_model;
	const bb_shell_ops *shell;
 bool control_shoulders;
 bool controls; /* shared physical remaps/hotkeys; PS1 retains its extended controller */
 const bb_cheat_ops *cheats;
 const char *(*setting_source)(unsigned);
 void (*setting_clear)(unsigned);
 void (*settings_open)(const char *key); /* NULL = system defaults */
	unsigned char *arena_start, *arena_end; /* linker overlay for this system only */
	int (*setting_value)(unsigned row);
	void (*setting_change)(unsigned row,int dir);
	void (*tick)(void);
	unsigned abi;              /* BB_SYSTEM_ABI */
	const char *id;            /* stable lowercase namespace: dirs, keys, presets */
	const char *name, *short_name;                 /* display name and abbreviation */
	uint16_t release_year;     /* first release worldwide (e.g. Famicom 1983-07); the menus list */
	uint8_t release_month;     /* systems newest first through bb_system_display_order() */
	uint8_t models;            /* consoles it runs on */
	uint8_t link;
	uint8_t stereo;            /* BB_STEREO_* capability */
	bool (*stereo_available)(void); /* model and settings capability, menus only */

	/* library */
	const char *const *extensions;                 /* NULL-terminated, lower case */
	const char *const *folder_hints;               /* normalized content-folder hints */
	unsigned char scan_priority; /* cheaper adapter scans first; default0 */
	size_t probe_bytes;        /* header bytes the worker reads for probe() */
	bb_probe_result (*probe)(const bb_probe_input *in, bb_entry *out);
	/* optional second pass over this system's candidates (PS1 CUE/M3U/series) */
	int (*group)(bb_entry *entries, int count);
	/* confirm identity at launch (reads the content), fills id */
	bool (*identify)(const char *path, const bb_entry *e, bb_content_id *id,
	                 char *error, size_t cap);
	/* the game behind a library row for covers; its strings stay valid until the
	   library publishes again (the cover worker is drained first). NULL: the
	   shared index entry supplies title and cover name */
	bool (*cover_item)(int index, bb_cover_item *out);
	int (*cover_candidates)(const char *path,const char *code,char out[][256],int max);
	const char *cover_db;      /* libretro thumbnails folder name */
	const char *logo_url;      /* the system maker's mark for the shelf spines, NULL = generic mark. Never shipped in
	                              the app: fetched once through the cover downloader and cached on the SD card */

	/* settings and presets */
	const bb_setting_desc *settings;
	unsigned nsettings;
	const char *preset_dir;    /* "romfs:/presets/<id>" */

	/* memory */
	bb_budget budget[BB_MODELS];

	/* lifecycle: run() owns the frame loop and returns on quit/failure */
	bb_run_result (*run)(const bb_launch *l, char *error, size_t cap);
	bool (*continue_available)(const bb_content_id *id, uint32_t *saved_time);

	const bb_save_ops *save;   /* NULL: system keeps its own save code (PS1 for now) */
	const bb_pause_row *pause_rows;
	unsigned npause_rows;
	const bb_dev_ops *dev;
	const bb_mp_ops *mp;
	const bb_package *package;   /* optional (NULL = a plain carton); shelf theme only */
	void (*picture_box)(float *box);   /* the game's picture on the top screen (x, y, w, h); NULL = all of it */
};
enum { BB_STEREO_NONE, BB_STEREO_SCREEN, BB_STEREO_LAYERS };

/* ---- Shell services a system calls (direct calls, defined in the shell) -- */
const bb_system_t *bb_session_system(void);  /* the system now running, NULL in menus */
int bb_system_cover_id(const bb_system_t *s,int index);  /* cover id of a library row */
/* The system's logo (bb_system_t.logo_url) as straight-alpha RGBA8 (R in the top byte), or NULL
 * while it is not on the SD card yet (the first call asks the cover downloader; offline stays
 * NULL). Owned by the cover code. */
const uint32_t *covers_logo(const bb_system_t *s,int *w,int *h);   /* menus only */
const bb_system_t *bb_system_by_id(const char *id);   /* menus/dev only */
uint32_t bb_heap_available(void);
unsigned bb_platform_model(void);
void bb_platform_release_menu(void);
void bb_platform_restore_menu(void);
bool bb_platform_claim(const bb_system_t *system);
void bb_platform_drop(void);
int bb_platform_count(const bb_system_t *system);
bool bb_platform_budget(const bb_system_t *system);
bool bb_session_begin(const bb_system_t *s, const bb_launch *l);  /* releases menu buffers,
                                         checks budget, installs APT hooks */
void bb_session_tick(void);              /* optional adapter maintenance, called by its frame loop */
void bb_session_set_stereo(bool layers_want_3d);   /* session-owned presentation flag */
void bb_session_end(void);               /* releases the loan and restores menus; run() must finish saves first */
/* A thread that could not be stopped: a join timed out, or run() returned with its core still
 * running (PS1 HOME > Close: no GPU to tear it down). From then on nothing it can touch is
 * freed; bb_session_end keeps the loan and the exit ends the process at once (main.c). */
void bb_exit_abandon(void);
bool bb_exit_abandoned(void);
void *bb_exec_arena(size_t *bytes);      /* RWX region, valid between begin and end */
int bb_setting(const bb_system_t *s, unsigned row);  /* effective value for this game */

#endif
