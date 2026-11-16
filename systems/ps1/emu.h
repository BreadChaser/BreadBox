// Running a game: start, messages, stats and errors (emu.c)
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include "safefile.h"
#include "ps1_hooks.h"

void emu_osd(const char *text);           // emu.c: short message in the game
void emu_osd_alert(const char *text);     // ... about the player's data: unmissable

safe_format_t emu_state_format(void);   // emu.c: what a good state file is

const char *emu_game_title(void);

// emu.c: why the last game didn't start, and what to do (a title line, then the advice)
const char *emu_load_error(void);   // browser.c: first run, find a folder with PS1 games

// emu.c
bool emu_init(void);
void emu_deinit(void);
// Prepared entry: the adapter drains background work and handles failure first.
/* The prepared call keeps its original no-result ABI. */
extern void covers_stop_prepared(void) __asm__("covers_stop_wait");
bool emu_run_game(const char *path, const char *save_name, const char *title);
