// The PS1 game library: one entry per game, discs grouped (library.inc)
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>

// library.c - one entry per game, clean titles, discs grouped
typedef struct {
	char *title;        // "Metal Gear Solid"
	char *cover_key;    // No-Intro name of disc 1, used to find box art
	char *group;        // scan-time only
	u32 dir_id;         // scan-time only
	int ndiscs;
	char *discs[8];
	int disc_no[8];
} game_t;
typedef enum { LIBRARY_EMPTY, LIBRARY_LOADED, LIBRARY_CANCELLED } library_result;
library_result library_load_result(const char *root, bool rescan);
bool library_load(const char *root, bool rescan);
bool library_guess_dir(void);
bool library_is_ps1_image(const char *path);   // cheap check: not another system's disc
bool library_missing_file_cancel(const char *path, char *out, size_t len, bool (*cancel)(void *), void *arg);
bool library_missing_file(const char *path, char *out, size_t len);   // a disc or track that isn't there

int library_count(void);
const game_t *library_get(int i);
// NULL if the disc list of a multi-disc game could not be written
const char *library_launch_path(const game_t *g, char *buf, size_t len);
