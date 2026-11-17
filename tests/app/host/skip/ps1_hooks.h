// Stand-in for host tests that bring their own declarations (tests/app/host/skip):
// the emu.c and savestates.c parts are skipped as emu.h and savestates.h are;
// the stereo hooks stay real, as gpustereo.h was never skipped.
#pragma once
#include <stdbool.h>
void gpustereo_after_present(void);
void gpustereo_game_end(void);
void *gpustereo_right_for(void *picture_tex);
void gpustereo_frame_end(void);
extern volatile bool gpustereo_copy_bypass;
int gpustereo_mode(void);
float gpustereo_flat(void);
void gpustereo_status(char *out, int n);
