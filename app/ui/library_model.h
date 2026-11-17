// Published catalog content shared by every library presentation.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <3ds.h>
#include "bb_system.h"

#define BB_LIBRARY_TITLE BB_TITLE_LEN
typedef struct {
	const bb_system_t *system;
	int row;                 // the system's catalog row
	char key[sizeof(((bb_catalog_item *)0)->key)]; // stable game identity; row indices can change during a hold
	int cover;               // covers.c game id
	float x;                 // spine centre along the shelf, mm
	float w, h, t;           // cover width, height, spine thickness, mm
	bool fav;
	u32 color;               // average cover colour (0 until known)
	u32 accent;              // the cover's dominant saturated colour (carton spines; 0 until known)
	int spine;               // spine atlas cell, -1 none
	u8 region;               // SHELF_REGION_*: the original-style spine
	u8 discs;                // discs in the case (0 unknown: one)
	char serial[12];         // product code from the file name ("SLPS-01234"), "" unknown
	u32 last, seconds;       // play history
	char title[BB_LIBRARY_TITLE];
	char tag[24];            // library label: short name and number
} bb_library_game;

typedef struct {
	const bb_system_t *system;   // NULL: Favorites
	const char *name;
	char plate[24];
	bb_library_game *box;
	int n;
	float base, end;             // board height, last spine edge (mm)
	float hmax, s;               // tallest box (mm); close-up scale, screen px per mm at the spines
} bb_library_row;

#define BB_LIBRARY_LAUNCH_GONE "BreadBox can't find this game in your games folder now. Hold Y in your collection to check it again."
#define BB_LIBRARY_LAUNCH_DISCS "BreadBox couldn't write this game's disc list. Is the SD card full or locked?"
enum { BB_LIBRARY_SORT_AZ, BB_LIBRARY_SORT_RECENT, BB_LIBRARY_SORT_MOST, BB_LIBRARY_SORTS };
bool bb_library_item(const bb_system_t *system, int row, bb_catalog_item *item);
void bb_library_key(const bb_system_t *system, bb_catalog_item *item);
int bb_library_resolve(const bb_library_game *game, bb_catalog_item *item);
void bb_library_fill(bb_library_game *game, const bb_system_t *system, int row, const bb_catalog_item *item);
void bb_library_sort(bb_library_row *row, int mode);
typedef void (*bb_library_prepare)(bb_library_game *, const bb_system_t *, int, const bb_catalog_item *, int);
int bb_library_build(bb_library_row *rows, int max_rows, bb_library_game *games, int max_games,
 int *nrows, int *total, char *last_title, size_t title_cap, bb_library_prepare prepare);
bool bb_library_favorite(bb_library_game *games, int count, const bb_library_game *game, int *live_row);
bool bb_library_launch(const bb_library_game *game, char *path, size_t cap, const char **why, int *live_row);

// The same controls are published for every library presentation.
#include "ui.h"
enum { BB_LIBRARY_SYSTEMS, BB_LIBRARY_GAMES, BB_LIBRARY_HELD };
#define BB_LIBRARY_ACTIONS 4
int bb_library_actions(int phase, bool favorite, const char *primary, ui_button_t *out, int capacity);
