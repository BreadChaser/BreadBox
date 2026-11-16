// Text, dialogs, button bars and notices on the screens (ui.c)
#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>
#include "bb_settings.h"
#include "ui_fit.h"

// ui.c - text drawing on the bottom screen
void ui_init(void);
void ui_exit(void);
void ui_text(float x, float y, float size, u32 color, const char *fmt, ...);
void ui_rect(float x, float y, float w, float h, u32 color);
void ui_begin(void);
bool ui_confirm(const char *title, const char *message, bool show_game);
const char *ui_fit(const char *s, float size, float width);
const char *ui_fit_ex(C2D_Font font, const char *s, float size, float min, float width, float *out_size);   /* ui_fit.h policy: shrink to min, then "..." */
/* s on at most max (<= UI_FIT_LINES_MAX) lines, ui_fit.h ui_fit_lines_bounded: at size, or wrapped, at the floor min,
 * balanced, the last one cut; returns the lines in out, *out_size the size to draw them at */
#define UI_FIT_LINES_MAX 4
int ui_fit_lines(C2D_Font font, const char *s, float size, float min, float width, int max, ui_fit_line *out, float *out_size);
// width of s in font (NULL: the system font) at size
float ui_text_width(C2D_Font font, float size, const char *s);
bool ui_confirm_action(const char *title, const char *message, const char *action, bool show_game);
bool ui_confirm_pick_update(const char *, const char *, const char *, const char *, void (*update)(void *), void *);
bool ui_confirm_pick(const char *title, const char *message, const char *yes, const char *no);   // two tabs, left / right picks, the second one (no) is chosen first; A answers it, B is no
int ui_confirm_pick_n(const char *title, const char *message, const char *const *answers, int n, int lit);   // n tabs, `lit` (the safe one) chosen first; A answers its index, B / Close -1
bool ui_confirm_choice(const char *title, const char *message, const char *action, const char *back, bool show_game);
void ui_confirm_ok(const char *title, const char *message, bool show_game);   /* one answer: OK */
void ui_text_ex(C2D_Font font, float x, float y, float size, u32 color, u32 flags, const char *s);
/* The header row of every ruled card (this card; the room's place, Collection and game cards): the title in the
 * title font on the red rule from x, fitted from UI_CARD_TITLE_SIZE down to UI_CARD_TITLE_MIN, then cut; the
 * eyebrow in red capitals on the rule, right-aligned at right, left out when the title needs the row.
 * text draws one line with ui_text_ex's arguments (the room scene passes its own, for its depth and text buffer). */
typedef void (*ui_text_fn)(C2D_Font font, float x, float y, float size, u32 color, u32 flags, const char *s);
void ui_card_header(ui_text_fn text, float x, float right, float rule, const char *title, const char *eyebrow, u32 ink, u32 red);
void ui_text_pixels(C2D_Font font, float x, float baseline, float pixels, float spacing,
	float width, u32 color, u32 align, const char *s);
bool ui_input_symbol(int id, float x, float y, float size, u32 color);
u32 ui_input_color(int id);
void ui_menu_header(const char *title, const char *right);
u32 ui_header_back(u32 keys);   // header tap -> KEY_B
// touch button bar replacing the prompt lines (ui.c): B leftmost, A rightmost
// each tab shows its icon and the button's glyph, no words; no icon: B back, A check
enum { UI_ICON_NONE, UI_ICON_STAR, UI_ICON_SORT, UI_ICON_RESCAN, UI_ICON_LEFT, UI_ICON_RIGHT, UI_ICON_FOLDER,
	UI_ICON_BACK, UI_ICON_PLAY, UI_ICON_CHECK, UI_ICON_CROSS, UI_ICON_PLUS, UI_ICON_TRASH, UI_ICON_SAVE,
	UI_ICON_LOAD, UI_ICON_RESET, UI_ICON_UP, UI_ICON_OPEN, UI_ICON_CHANGE, UI_ICON_TOGGLE, UI_ICON_DISC, UI_ICON_EXIT,
	UI_ICON_TV, UI_ICON_BOOKS, UI_ICON_SCOPE, UI_ICON_QR, UI_ICON_PADS, UI_ICON_TOGETHER,   // the room's places (drawn, no glyph)
	UI_ICON_SLIDERS, UI_ICON_FF, UI_ICON_REWIND, UI_ICON_SUN,   // the pause card's rows (drawn)
	UI_ICON_PAUSE,   // the in-game menu button (drawn)
	UI_ICON_MOON,    // the Night room style (drawn; Day is the sun)
	UI_ICON_START }; // start over: back to the start (a bar and PLAY's triangle turned back); the last bar glyph (icons.c)
typedef struct { u32 key; const char *label; u8 icon; bool on; } ui_button_t;
#define UI_BAR_MAX 6
void ui_bar_draw(const ui_button_t *b, int n);           // bottom screen, BAR_TOP..240: icons and button glyphs
void ui_icon_draw(int icon, float cx, float cy, float s, u32 ink, u32 bg);   // one of the bar's drawn icons anywhere
u32 ui_bar_input(const ui_button_t *b, int n, u32 keys);  // tap -> its key; presses light it
void ui_tabs_draw(const ui_button_t *b, int n, int lit);   // style-7 tabs in this order, `lit` raised
void ui_tabs_icons(const ui_button_t *b, int n, int lit);     // the same, icons only: no words, the raised tab is highlighted
int ui_tabs_hit(int n, u32 keys);                           // the tab under a bar tap, -1
int ui_bar_parse(const char *hint, ui_button_t *out, int max);   // "A open    B back"
enum { UI_COL_BAR, UI_COL_RULE, UI_COL_NOTICE, UI_COL_STAR };
u32 ui_bar_color(int which);
void ui_star(float cx, float cy, float r, u32 c, u32 hollow, float z);
void ui_notice(const char *s, int frames);
void ui_notice_bar(const char *s, int frames);
void ui_first_letter(char *out, const char *title);   // out[8]: one whole UTF-8 letter   // top screen, fades over its last 20 frames
void ui_text_wrap(float x, float y, float size, u32 color, float width, const char *s);
void ui_button_text(float x, float y, float pixels, float spacing, u32 color, int id);
// The first/last cell rows holding ink of the letters of s at min_alpha (1..255); false: no A4/A8 sheet or no ink.
bool ui_font_ink_rows(C2D_Font font, const char *s, int min_alpha, int *top, int *bottom);

void ui_help_pages(const char *title,const char *text,bool show_game);
void ui_source_mark(bb_setting_source source,float x,float y,u32 ink,u32 paper);

int ui_value_card_pages(const char *label, const char *value);
void ui_value_card(const char *label, const char *value, int page); /* top screen: full text, SELECT pages */
void ui_dialog_draw(const char *title,const char *scope,const char *text,const ui_button_t *bar,int n);
void ui_dialog_draw_page(const char *title,const char *scope,const char *text,int page,const ui_button_t *bar,int n);
int ui_dialog_pages(const char *text);   /* pages of dialog text at the floor, above the bar */
// Top screen behind bottom-screen dialogs: the theme's backdrop, never black.
void ui_menu_top(void *ud);
void ui_progress_draw(const char *title,const char *message,int percent);
void ui_progress_card_draw(const char *title,const char *message,int percent);
void ui_game_notice(const char *text,bool alert);
void ui_speed_badge(int dir);   // ▶▶ / ◀◀ on a game's top screen
void ui_game_blank(void *unused);
void ui_companion_title(const char *title,const char *subtitle);
void ui_companion_title_at(const char *title,const char *subtitle,float y,u8 alpha);

void ui_help_panel(float y0, const char *title, const char *impact, const char *text);
