// Interface sounds and library music (uisound.c)
#pragma once
#include <3ds.h>
#include <stdbool.h>

// uisound.c - interface sounds and library music
// One event per thing the player does; uisound.c maps each to its file(s) in one table (sfx_events).
// The first four keep their values: systems/ps1/emu.c's pinned run_game passes SFX_BACK.
enum {
	SFX_MOVE, SFX_SELECT, SFX_BACK, SFX_LAUNCH,
	SFX_VALUE,        // left/right changes a setting
	SFX_TOGGLE,       // an on/off setting flips
	SFX_TAB,          // L/R or a touch changes tab or page
	SFX_JUMP,         // a whole section at once (a list page)
	SFX_DIALOG,       // a question card comes up
	SFX_CONFIRM,      // yes on a question
	SFX_DENY,         // no / cancel on a question
	SFX_SAVE,         // the postmark stamps a postcard
	SFX_TOSS,         // a postcard is thrown away
	SFX_LOAD,         // a postcard is taken off the board
	SFX_PAUSE_OPEN,   // the pause card comes up
	SFX_PAUSE_CLOSE,  // the pause card goes away
	SFX_ERROR,        // the press is understood but not possible here (a note says why)
	SFX_COUNT
};
void uisound_init(void);
void uisound_exit(void);
bool uisound_stop_reads(u64 timeout);   // after a self-update's commit, before uisound_exit; false: still reading
void music_unload(void);

void music_reload(void);
void music_reload_step(void);   // BreadBox: the music read back in parts after a game (music_update does it too)
void music_stream(void);   // BreadBox cold start: the later thread reads the music prepared at start (after the first frame)
/* Menu sounds go through one slot per frame: sfx_play() names this action's sound (the latest call wins over the
 * gate's A/B default, so one press never makes two sounds); it plays at the next input gate (ui_swap_keys), screen
 * entry (ui_swap_enter) or pause close. A press that did nothing calls sfx_cancel(). SFX_LAUNCH plays at once.
 * Nothing plays with "Menu sounds" off, after HOME > Close, or inside a game (the pause card cancels what is left). */
void sfx_play(int event);
void sfx_default(int event);   /* the gate's sound for A/B: only if the frame has none yet */
void sfx_now(int event);       /* the shared list/card/board moves: at once, never replaced */
void sfx_cancel(void);         /* forget the frame's sound: a dead press, or a game takes over */
void sfx_flush(void);
void sfx_card_closes(void);   /* a postcard board's save/load sound stands for the pause card closing after it */
bool sfx_close_said(void);    /* ui_pause_close: true once, when a board already made the sound */
void music_play(bool on);
void music_update(void);
