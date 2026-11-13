/* GBA fast-forward and rewind (gba_speed.c). Main thread only. */
#ifndef BREADBOX_GBA_SPEED_H
#define BREADBOX_GBA_SPEED_H
#include <stdbool.h>
#include <stdint.h>
enum { GBA_REWIND_OFF, GBA_REWIND_STEPPED, GBA_REWIND_END, GBA_REWIND_FAILED };
#define GBA_FF_EXTRA 3   /* more frames per shown frame while fast-forwarding (PS1) */
bool gba_speed_rewind_allowed(void);     /* New 3DS and the game's Rewind setting */
void gba_speed_begin(uint32_t held);     /* after the game (and its Continue state) loaded */
void gba_speed_end(void);
void gba_speed_pause(uint32_t held);     /* back from the pause menu */
void gba_speed_apply(void);              /* Rewind setting changed */
void gba_speed_touch(bool toggle,bool hold,bool rewind);
uint32_t gba_speed_input(uint32_t held); /* hotkeys; returns the game's buttons */
bool gba_speed_ff(void);
bool gba_speed_rewinding(void);
int gba_speed_rewind_frame(bool *restart); /* GBA_REWIND_*, once per shown frame */
void gba_speed_capture(void);            /* after each emulated frame */
void gba_speed_reset(void);              /* a state was loaded or the game reset */
unsigned gba_speed_history_kib(void);
void gba_speed_force(bool ff, bool rw);  /* dev smoke tests only */
/* Pause card: Fast forward starts when the menu closes; Rewind goes back about three seconds
 * (18 steps of the history, six a second) and returns the steps made, 0 = none to go back to,
 * -1 = failed (the game goes on from here), GBA_REWIND_RESTART = failed half-way (restart it). */
#define GBA_PAUSE_REWIND_STEPS 18
#define GBA_REWIND_RESTART (-2)
void gba_speed_ff_next(void);
int gba_speed_back(unsigned steps);
#endif
