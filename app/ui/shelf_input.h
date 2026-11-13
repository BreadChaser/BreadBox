// Physical shelf controls. The caller supplies wall time, never a display-frame count.
#pragma once
#include <stdbool.h>
typedef struct { unsigned direction; float next; } shelf_repeat;
static inline int shelf_browse_step(shelf_repeat *r, unsigned down, unsigned held, float now)
{
 const unsigned prev = KEY_LEFT | KEY_UP | KEY_CPAD_LEFT | KEY_CPAD_UP;
 const unsigned next = KEY_RIGHT | KEY_DOWN | KEY_CPAD_RIGHT | KEY_CPAD_DOWN;
 unsigned bits = (down | held) & (prev | next);
 unsigned dir = (bits & prev ? 1u : 0u) | (bits & next ? 2u : 0u);
 if (!dir) { r->direction = 0; return 0; }
 if (dir == 3) return 0;
 bool fresh = dir != r->direction;
 r->direction = dir;
 if (!fresh && now < r->next) return 0;
 r->next = now + (fresh ? .4f : .1f);
 return dir == 1 ? -1 : 1;
}
// The box in the hand turns all the way round (owner 2026-11-12: "when you spin the game disc around it should
// loop around"): side 0 front, 1 spine, 2 back, 3 the open edge. R turns it a quarter on (front, spine, back,
// edge, front...), L a quarter the other way; never a dead stop. steps: the turn just made, in quarters (+ R, - L).
#define SHELF_SIDES 4
static inline int shelf_turn_steps(unsigned keys) { return !!(keys & KEY_R) - !!(keys & KEY_L); }
static inline int shelf_turn_side(int side, int steps) { return ((side + steps) % SHELF_SIDES + SHELF_SIDES) % SHELF_SIDES; }
// Back to the front (A on "Front") by the shortest turn: the edge a quarter on, the spine a quarter back, the back
// half round back (the way it came round from the front).
static inline int shelf_turn_home(int side) { return side == 3 ? 1 : -side; }

// A deliberate one-second hold; release is required before another action.
typedef struct { float started; bool active, fired; } shelf_hold;
static inline bool shelf_hold_action(shelf_hold *s, bool held, float now)
{
 if (!held) { s->active = s->fired = false; return false; }
 if (!s->active) { s->active = true; s->started = now; }
 if (s->fired || now - s->started < 1) return false;
 s->fired = true; return true;
}
