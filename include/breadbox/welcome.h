// The first-launch welcome, variant A "Tour" (devtools ONBOARDING-CARDS.md): notebook cards on the bottom screen
// while the room's camera visits what each one talks about. welcome.c holds the shown-once flag and the words;
// the tour itself runs in the room (app/ui/home_welcome.inc).
#pragma once
#include <stdbool.h>
#include <stddef.h>

// Start up, right after config_load(): had_config = BreadBox had its own config.ini before this start (checked
// before the PSX3D settings import made one). A truly first start leaves the welcome to show; an update from a
// BreadBox without the welcome marks it seen. brought: PSX_IMPORT_* kinds known now (the games folder that came
// with PSX3D's settings). Writes config.ini when the flag changes.
void bb_welcome_boot(bool had_config, unsigned brought);
bool bb_welcome_due(void);              // the first start's tour is still to show
void bb_welcome_seen(void);             // finished or skipped: never shown by itself again (saved)
unsigned bb_welcome_boot_kinds(void);   // what bb_welcome_boot was told came over

// Settings > Help > Welcome: the tour again, from the room Settings borrows (does not clear the seen flag).
// True when the player pressed X on the games card: the caller opens the games-folder picker as its own
// Games folder row does; the tour comes back on that card after it.
bool bb_welcome_open(void);

// ---- pure (host tests) ----
// config.ini "welcome" at a start: key -1 (no key), 0 (to show), 1 (seen); the value to keep.
int bb_welcome_boot_key(bool had_config, int key);
int bb_welcome_pages(unsigned brought);   // 2, or 3 when something came from PSX3D
enum { BB_WELCOME_KEY_A = 1, BB_WELCOME_KEY_B = 2, BB_WELCOME_KEY_X = 4, BB_WELCOME_KEY_START = 8 };
enum { BB_WELCOME_STAY, BB_WELCOME_NEXT, BB_WELCOME_BACK, BB_WELCOME_DONE, BB_WELCOME_SKIP, BB_WELCOME_FOLDERS };
int bb_welcome_step(int page, int pages, unsigned keys);
typedef float (*bb_welcome_measure)(const char *s, void *ud);   // a line's width (NULL: no limit)
// The games card (card 2): count < 0 while the library is still being read (then the "Add games" form). Its folder
// line (l1, and l2 when "Put them in" and the folder's name need two lines) fits box; returns the lines used.
int bb_welcome_games(int count, const char *games_dir, char *title, size_t title_cap, char *l1, char *l2, size_t cap,
	bb_welcome_measure measure, void *ud, float box);
// "Brought from PSX3D": what was copied, in the order Games, saves, postcards, settings, cheats, BIOS, on one line
// of box (measure) when it fits, else "Games, saves and settings" when all three came; then the same over two
// lines (l2, broken after a word); last, the first ones "and more". Returns the lines used (l2 "" for one).
int bb_welcome_brought(char *l1, char *l2, size_t cap, unsigned kinds, bb_welcome_measure measure, void *ud, float box);

// The cards' fixed words (tests/ui_fit/strings.py measures them)
#define BB_WELCOME_TITLE "Welcome"
#define BB_WELCOME_SYSTEMS_1 "PlayStation and"
#define BB_WELCOME_SYSTEMS_2 "Game Boy Advance."
#define BB_WELCOME_RESCAN "Hold \xee\x80\x83 to look again"
#define BB_WELCOME_ADD "Add games"
#define BB_WELCOME_PSX3D "Brought from PSX3D"
#define BB_WELCOME_UNTOUCHED "PSX3D is untouched."
