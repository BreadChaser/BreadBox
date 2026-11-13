// Platform save moments and the waited GPU texture copy (bb_platform.c)
#pragma once
#include <3ds.h>

// the last game's saves (bb_save_resolve): before a game starts / at exit (waits) / each menu frame
enum { BB_SAVES_MENU, BB_SAVES_START, BB_SAVES_EXIT };
enum { BB_SAVES_GO, BB_SAVES_ASKED, BB_SAVES_CLOSE = -1 };   // CLOSE: the app is closing, stop now
int bb_platform_saves(int moment);

void bb_sync_texture_copy(u32 *in, u32 indim, u32 *out, u32 outdim, u32 size, u32 flags);   // BreadBox: a GPU copy really waited for
