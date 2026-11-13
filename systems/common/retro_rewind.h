/* Rewind history for libretro-hosted systems. GPL-2.0-or-later.
 * Same engine as PS1's systems/ps1/rewind.c (XOR run deltas, ring of about
 * `budget` bytes, comparing/compressing on a worker core), with the state
 * calls supplied by the system. Main thread only, except the worker. */
#ifndef BB_RETRO_REWIND_H
#define BB_RETRO_REWIND_H
#include <stdbool.h>
#include <stddef.h>
#include "bb_system.h"
#define BB_REWIND_INTERVAL 10   /* emulated frames between captures (6 per second) */
typedef struct {
 size_t (*size)(void);
 bool (*save)(void *buf,size_t n);
 bb_state_result (*load)(const void *buf,size_t n);   /* BB_STATE_PARTLY: half-restored */
} bb_rewind_ops;
/* false: not enough memory or no worker core (rewind stays off) */
bool bb_rewind_start(const bb_rewind_ops *ops,size_t budget,int core);
void bb_rewind_stop(void);
bool bb_rewind_active(void);
void bb_rewind_capture(void);   /* after each emulated frame */
/* one step back: 1 done, 0 the history is used up, -1 the step could not be
 * loaded (history dropped; *result says whether the game was half-restored) */
int bb_rewind_step(bb_state_result *result);
void bb_rewind_reset(void);     /* a state was loaded or the game reset */
size_t bb_rewind_bytes(void);   /* history held now */
#endif
