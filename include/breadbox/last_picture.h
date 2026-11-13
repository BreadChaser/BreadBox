// The last game's picture the room's TV shows (breadbox_picture.c)
#pragma once
#include <3ds.h>
#include <stdbool.h>

// The last game's picture (breadbox_picture.c): the room's TV shows where you left off.
#define BB_PICTURE_W 256
#define BB_PICTURE_H 192
void bb_last_picture_save(const char *game_path, const void *rgb565, int w, int h, int pitch);   // at quit
bool bb_last_picture_read(const char *game_path, u16 *px, bool (*cancel)(void *), void *arg);
bool bb_last_picture_load(const char *game_path, u16 *px);   // BB_PICTURE_W x BB_PICTURE_H, that game's only; NULL: whichever game
void bb_last_picture_release(void);   // a game starts: the newest picture's memory (its file is written)

void bb_last_picture_from_screen(const char *game_path);    // the top screen's last picture, after a game

// Black rows above and below the game in a last picture (bb_last_picture_save centres a wider game, e.g. a GBA's
// 3:2, in black): the fewer of the two all-black runs, at most a quarter of the height (a dark scene stays whole
// down to 2:1). The Continue postcard crops them to show the game in its own shape.
static inline int bb_picture_band(const u16 *px)
{
	if (!px) return 0;
	int top = 0, bottom = 0;
	for (; top < BB_PICTURE_H / 4; top++) {
		const u16 *row = px + top * BB_PICTURE_W;
		int x = 0;
		while (x < BB_PICTURE_W && !row[x]) x++;
		if (x < BB_PICTURE_W) break;
	}
	for (; bottom < top; bottom++) {
		const u16 *row = px + (BB_PICTURE_H - 1 - bottom) * BB_PICTURE_W;
		int x = 0;
		while (x < BB_PICTURE_W && !row[x]) x++;
		if (x < BB_PICTURE_W) break;
	}
	return bottom;
}
