// The main menu (home.c)
#pragma once
#include <stdbool.h>
#include <stddef.h>

// home.c - the main menu BreadBox opens to
typedef struct {
	char save[256];    // save name for emu_run_game ("" = NULL)
	char title[256];   // title for emu_run_game ("" = NULL)
	bool resume;       // load the Continue auto-save without asking
} home_launch_t;
enum { HOME_QUIT, HOME_AGAIN, HOME_LIBRARY, HOME_PLAY, HOME_FOLDERS };
int home_run(bool have_games, char *path, size_t len, home_launch_t *out);

void home_dawn(void);                // home.c: the first picture, drawn before the room exists (cold start)
// Library with no games: 0 quit, 1 main menu, 2 folder browser, 3 look again
int home_empty_library_run(void);

void home_note_game(const char *path, const char *save_name, const char *title, const char *state_name);
