#ifndef BB_PAUSE_H
#define BB_PAUSE_H
#include "bb_system.h"
typedef struct {
 const bb_system_t *system;
 const char *title;
 void (*draw_game)(void *);
 void (*pause)(bool);
 void (*rebase)(void);
 void (*reset)(void);
 bool (*settings)(void); /* true = application closing */
 unsigned (*rows)(void); /* system pause rows shown for this game (the first n), NULL = all */
} bb_pause_config;
void bb_pause_begin(const bb_pause_config *);
void bb_pause_notice(const char *);
bool bb_pause_confirm(const char *,const char *,const char *);
int bb_pause_prompt(void);   /* the Continue question: BB_CONTINUE_YES, _OVER or _BACK (below) */
bool bb_pause_run(void); /* true = quit */
bool bb_pause_active(void);
void bb_pause_reset(void); /* the game restarts (a rewind that failed half-way) */
/* "Select + R" for hotkey 0 (fast forward) or 1 (rewind), "" when off. */
const char *bb_pause_hotkey(int i);
/* Private quick state; loads first save Undo. Pauses audio and rebases on return. */
bool bb_pause_quick(bool save);
/* After a game quit from the pause card: its own last picture goes to the room's TV (last_picture.h);
 * false when it did not quit from the card (the top screen still holds the game then). */
bool bb_pause_last_picture(const char *game_path);
/* Main-thread APT hooks call these; never submits a GPU command. */
bool bb_pause_suspend(bool make_continue);
void bb_pause_restore(void);
/* One pause card for every system (deep UI test F16): the same rows in the same order with the same words,
 * icons and values, the same bar, tag and SAVED postmark. Systems keep their own save models and only list
 * the rows they have; a row a system does not have is absent. PS1 (systems/ps1/emu.c) and the shared
 * libretro/GBA controller below both draw through bb_pause_present_top / bb_pause_present_card. */
#include "ui.h"
#include "ui_pause.h"
enum { BB_PAUSE_RESUME, BB_PAUSE_UNDO, BB_PAUSE_SAVE, BB_PAUSE_LOAD, BB_PAUSE_SETTINGS, BB_PAUSE_CONTROLS, BB_PAUSE_CHEATS,
 BB_PAUSE_SYSTEM, BB_PAUSE_RESET, BB_PAUSE_QUIT, BB_PAUSE_KINDS };
const char *bb_pause_row_name(int kind);
unsigned char bb_pause_row_icon(int kind);
/* The postcards as the card shows them: the first free card (-1 all used), how many are used, the card just
 * saved (-1 none) and until when it shows "Saved". Card indexes are 0..11 (bb_slots.h). */
typedef struct { int free_card, used, saved_card; u64 saved_until; } bb_pause_slots;
/* A shared row's value and tone: Save shows the X glyph and the next number, "Saved" just after, "All 12
 * used"; Load an arrow (dim with none); the rows that open a screen an arrow. */
void bb_pause_row_value(int kind, const bb_pause_slots *slots, ui_card_row_t *out);
/* X saved a postcard in card `card`: the value turns to "Saved" and the postmark is stamped for a moment. */
void bb_pause_slots_saved(bb_pause_slots *slots, int card);
/* The bar: B Resume, X Save, A Select (nbar 3); a system may replace A's action (PS1's disc row). */
extern const ui_button_t bb_pause_bar[3];
/* Top: the paused game in the CRT, a tag with its clean name (no "(USA)", no "Paused"/system lines) and,
 * just after X saved, the SAVED postmark on the picture. Bottom: the card and the bar. */
void bb_pause_present_top(const char *title, const float *game_box, void (*game)(void *), const bb_pause_slots *slots);
void bb_pause_present_card(const flist_t *list, int n, int sel, void (*row)(int, void *, ui_card_row_t *), void *ud,
 const char *title, const char *notice, const ui_button_t *bar, int nbar);
/* The Continue question at a game's start, for every system: A plays on from the Continue point, X starts the
 * game over after one more question, Y (PS1, when older auto-saves exist) lists them, B goes back to the shelf
 * with nothing played (a reflex B used to throw the player's spot away). Until it is answered no Continue point
 * is made (bb_save_continue_hold; PS1: continue_frozen); a closed app is a Back. */
enum { BB_CONTINUE_ASK, BB_CONTINUE_YES, BB_CONTINUE_OVER, BB_CONTINUE_OLDER, BB_CONTINUE_BACK };
#define BB_CONTINUE_BUTTONS(older) ((older) ? 4 : 3)   /* B Back, X Start over, A Continue (, Y Older) */
/* Back at the question: the shell leaves the room's TV picture and corkboard as they were (main.c takes it). */
void bb_continue_went_back(void);
bool bb_continue_went_back_take(void);
/* The paused game for screens opened from the pause card (Cheats, Button mappings): set while the card is open
 * (NULL game: none). bb_pause_scene_top draws it with `tag` on its tag (NULL: the game's name); false: none. */
void bb_pause_scene_set(const char *title, void (*box)(float *), void (*game)(void *));
bool bb_pause_scene_top(const char *tag);
extern const ui_button_t bb_continue_bar[4];   /* BB_CONTINUE_BUTTONS of them */
int bb_continue_input(u32 keys, bool older);   /* a key on the question: BB_CONTINUE_* */
/* Top: the Continue picture in the CRT, the game's name and "Left off <date>" on the tag (tex NULL: none). */
void bb_continue_present_top(const char *title, C3D_Tex *tex, float tw, float th, u32 saved);
/* Bottom: the Continue point as the postcard on the desk (postcards.h pc_continue), "Continue?" in red, and the bar;
 * stamp the system's short name; over_failed: Start over could not move the old spot (the red foot line). */
void bb_continue_present_card(const char *title, C3D_Tex *tex, float tw, float th, u32 saved, const char *stamp, bool older, bool over_failed);
#endif
