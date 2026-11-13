/* Save slots as postcards pinned to a corkboard: one shared slot browser view.
 * The top screen shows the chosen card large (picture, stamp, postmark with the
 * date, the slot written by hand, play time); the bottom screen is the board
 * with every card and the actions. A Continue card stands out on the left.
 * Controllers (bb_pause.c for systems on the shared save service, PS1's
 * savestates.c) fill a pc_view; components/postcards.inc draws it from theme
 * roles and ui_style.h tokens only. Room themes get paper, cork and pins from
 * theme_room_t; other themes the same cards in their own roles.
 * Pictures are small textures made once when the browser opens: no per-frame
 * CPU work besides the draw calls. GPL-2.0-or-later. */
#ifndef POSTCARDS_H
#define POSTCARDS_H
#include "app_policy.h"
#include "ui.h"
#include "bb_slots.h"

#define PC_SLOTS BB_SLOTS            /* twelve save slots, shown 1..12 (bb_slots.h) */
#define PC_CONTINUE PC_SLOTS          /* card index of the Continue card */
#define PC_CARDS (PC_SLOTS + 1)
#define PC_BAR_MAX UI_BAR_MAX

typedef struct {
	bool used;               /* a save is in this slot */
	u32 saved, played;       /* unix time (3DS local clock), seconds played; 0 = unknown */
	C3D_Tex *tex;            /* the card's picture, NULL = none */
	float tw, th;            /* picture size in texels, from the texture's top-left */
	C3D_Tex own;             /* tex points here when the view made it */
	u16 *full;               /* full-size picture for the top screen (heap, may be NULL) */
	unsigned fw, fh;
	float lift, lift_v;      /* selection spring */
} pc_card;

enum { PC_ANIM_NONE, PC_ANIM_STAMP, PC_ANIM_TOSS };

typedef struct {
	const char *game;        /* written on the card */
	const char *stamp;       /* the stamp's short label (system), NULL = none */
	bool saving, has_continue;
	bool confirm_replace, confirm_delete;
	int sel;                 /* 0..11 a slot (shown as sel + 1), PC_CONTINUE */
	pc_card card[PC_CARDS];
	pc_card now;             /* the paused picture: preview for saving into an empty slot */
	C3D_Tex big;             /* the chosen card's picture at full size */
	bool big_ok;
	int big_of;              /* card shown in big (-1 none), now = PC_CARDS */
	float big_w, big_h;
	int anim, anim_card;     /* PC_ANIM_*: the stamp after a save, a tossed card */
	float anim_t;            /* 0..1 */
	bool anim_fresh;         /* first picture excludes blocking save/delete time */
	const char *notice;
	float notice_t;
	ui_button_t bar[PC_BAR_MAX];
	int nbar;
	float t;
} pc_view;

void pc_open(pc_view *v, bool saving, bool has_continue, const char *game, const char *stamp);
/* Make card's picture from RGB565 rows (copied; the caller keeps px). */
bool pc_picture(pc_card *c, const u16 *px, unsigned w, unsigned h);
void pc_card_clear(pc_card *c);
void pc_close(pc_view *v);
/* Navigation and touch; true when the chosen card was picked (A, or a tap on it). */
bool pc_input(pc_view *v, u32 *keys);
/* Springs, animations, the big picture of the chosen card. Once per frame. */
void pc_tick(pc_view *v, float dt);
void pc_notice(pc_view *v, const char *text);
void pc_animate(pc_view *v, int anim, int card);
bool pc_animating(const pc_view *v);
/* The Continue postcard: the room's Continue place and the Continue? question draw the Continue point as one
 * big postcard on the desk (bottom screen, bar not included): its picture in the game's own shape, the name on
 * up to three ruled lines (balanced, ui_fit_lines), "Left off <day>, <time>" printed at the foot, the system's
 * stamp with the postmark, "Continue" / "Continue?" handwritten in red. empty: a blank postcard. */
typedef struct {
	C3D_Tex *tex;            /* the Continue point's picture, NULL = none */
	float tw, th;            /* its size in texels from the texture's top-left */
	float band;              /* black rows above and below the game in it (a letterboxed last picture), cropped */
	const char *name;        /* the game's clean name */
	const char *stamp;       /* the system's short name on the stamp, NULL = a blank stamp */
	u32 saved;               /* "Left off" time (3DS local clock), 0 = unknown */
	const char *alert;       /* a red foot line instead of "Left off" (Start over failed), NULL = none */
	bool question, empty;    /* "Continue?" (the question) or "Continue" (the room); nothing to continue */
} pc_continue;
void pc_continue_card(const pc_continue *c);
/* Drawing (components/postcards.inc): video_present_vsync(..., postcards_top, postcards_bottom, v). */
void postcards_top(void *view);
void postcards_bottom(void *view);
#endif
