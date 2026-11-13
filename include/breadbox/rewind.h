// Rewind, per game, off by default (rewind.c)
#pragma once
#include <stdbool.h>

// rewind.c - per game, off by default
bool rewind_start(void);
void rewind_stop(void);
bool rewind_active(void);
void rewind_capture(void);
int rewind_step(void);   // 1 done, 0 used up, -1 failed (see rewind.c)
void rewind_reset(void);
