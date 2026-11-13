// Curated per-game presets and defaults.ini (presets.c)
#pragma once
#include <stdbool.h>
#include "app_input.h"

// presets.c: curated per-game data files (romfs:/presets/*.ini, docs/auto-update.md),
// resolved per console. Fields use -1 to inherit the uncurated console defaults.
// needs_l3r3/needs_right_stick: shown only while the game uses analog mode.
#define NEEDS_ANALOG 2
typedef struct {
	int fast_forward, rewind, touch_controls;
	int needs_l2r2, needs_l3r3, needs_right_stick;
	int touch_bindings[INPUT_TOUCH_N]; // -2 inherits needs_* shorthand; -1 hides.
	int frameskip; // -1: console default Auto, 0: Off, 1: Auto.
	int widescreen; // WS_* policy (widescreen.h); default offered when a fix exists.
	// PS1 CPU clock in % (pcsx_rearmed_psxclock), 0 inherits. Applied only with
	// PSX3D_PS1_OVERCLOCK_PRESETS; never over a core database override.
	int psxclock;
	int auto_floor; // pictures/s; absent = 15
	// Technical settings in preset_schema.def order (word index, -1 unset).
	int controller, alt_flip, gpu_slow_llists, cd_turbo, nostalls, nolddelay;
	int gteregsunneeded, nogteflags, nosmccheck, icache_emulation, exception_emulation, nocompathacks;
	int tested_with; // defaults_version these values were tested with; -1 untested
} game_preset_options_t;
typedef struct {
	const char *serial;
	game_preset_options_t old3ds, new3ds;
	bool curated; // the preset owns the game's option list (frameskip hidden)
} game_preset_t;
// Variadic initializer allows designated fields and comma-separated values.
#define GAME_PRESET_SAME(disc, ...) { .serial = disc, .old3ds = __VA_ARGS__, .new3ds = __VA_ARGS__, .curated = true }
void game_presets_init(const char *builtin_dir, const char *sd_dir);
bool game_presets_validate(const char *builtin_dir, const char *dir, int version, unsigned files);
void game_presets_moved(const char *sd_dir);
int game_presets_pack(void);
int game_preset_version(void);
const char *game_preset_source(void);
game_preset_options_t game_capabilities(void);
bool game_preset_curated(void); // legacy metadata only; never gates options
bool game_preset_sets(const char *key);
const char *game_preset_evidence(const char *key);
int game_auto_floor(void);
bool game_preset_tested(void);   // this console's values were tested (tested_with)
// The preset's value for a technical core option, as a core value; NULL when unset.
const char *game_preset_core_value(const char *core_key);
// defaults.ini (docs/settings.md section 6): this console's default and forced
// values, in the preset vocabulary. defaults_core() maps them to core values.
void defaults_init(const char *path);
int defaults_version(void);
const char *defaults_get(const char *key);
const char *defaults_forced(const char *key);
const char *defaults_core(const char *core_key, bool *forced);
int defaults_word(const char *key, const char *words, int fallback);
void game_presets_reset(void);
void game_presets_invalidate(void);
void game_presets_disc_log(const char *message);
#ifdef PSX3D_PS1_OVERCLOCK_PRESETS
void game_presets_core_log(const char *message);
#endif
