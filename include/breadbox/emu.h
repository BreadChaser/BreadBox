// Running a game: start, messages, stats and errors (emu.c)
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include "safefile.h"

// Last completed stats interval, main-thread access for remote stats.
// All costs are ms per emulated vblank. Audio is nested in run; present
// includes required offscreen submission on held frames. Misc includes the
// previous interval's reporting/SD writes. Pacing is excluded from work.
typedef struct {
	unsigned emulated, elapsed_ms, presented, held, top, bottom;
	float run_ms, present_ms, audio_ms, misc_ms, pace_ms;
	float spu_ms, spu_wait_ms, pica_ms;
	float phase_ms[8], draw_fraction;
	double picture_mean, picture_square;
	unsigned picture_intervals;
	unsigned skip_level, drawn, skipped;
} emu_frontend_sample;
const emu_frontend_sample *emu_frontend_stats(void);

u64 emu_audio_ticks(void);

void emu_osd(const char *text);           // emu.c: short message in the game
void emu_osd_alert(const char *text);     // ... about the player's data: unmissable

safe_format_t emu_state_format(void);   // emu.c: what a good state file is

const char *emu_game_title(void);

// emu.c: why the last game didn't start, and what to do (a title line, then the advice)
const char *emu_load_error(void);   // browser.c: first run, find a folder with PS1 games

// emu.c
bool emu_init(void);
void emu_deinit(void);
bool emu_run_game(const char *path, const char *save_name, const char *title);
extern bool emu_continue_now;   // next game: load its Continue auto-save, no prompt
