// The pause card: CRT, hang tag, notebook card and the dim reveal (ui.c)
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include "bb_settings.h"
#include "ui_style.h"
#include "ui_list.h"

void ui_pause_title(const char *title);
void video_game_box(float *box);   /* PS1: where the paused picture sits in a full-screen draw (x, y, w, h) */
void video_draw_game(void);        /* the paused PS1 picture, as the game draws it (menus: show_game false) */
/* The pause card (ui_style.h UI_CRT_*, UI_TAG_*, UI_CARD_*): the paused game in a CRT on the wall with its hang
 * tag (top screen), the menu on a notebook card (bottom screen). Colors are theme roles. */
typedef struct {
	const char *title;                    /* the tag's heading (wrapped over up to UI_TAG_TITLE_LINES lines) */
	const char *line[UI_TAG_LINES];       /* small lines under the rule; NULL ends the list */
	int hot;                              /* the line written in the stamp color, -1 none */
	const char *body;                     /* a sentence wrapped by width under the lines (UI_TAG_BODY_LINES) */
	float w;                              /* the tag's width, 0: UI_TAG_W */
} ui_tag_t;
/* A paper hang tag whose top-left is (x, y) on the top screen. Reusable for any info label. */
void ui_tag(const ui_tag_t *tag, float x, float y);
bool ui_tag_body_fits(const char *body, float w);   /* the whole sentence fits a tag w wide (0: UI_TAG_W) */
/* Wall, CRT and tag. game_box: where the game puts its picture in a full-screen draw (x, y, w, h, NULL = the
 * whole screen); game() draws it and is moved and scaled into the glass. */
bool ui_pause_open(void);
bool ui_pause_close(void);
void ui_pause_frame(void);
extern u32 ui_resume_pending;
float ui_resume_sample(u32 state); /* one alpha for the whole present */
void ui_pause_motion(float progress); /* shared CRT, tag and card transition, 0 closed .. 1 open */
#define UI_SWAP_REMOTE (1u << 23) /* not a physical pad bit */
/* The swap mask names the keys that hand over to or from a game (launch, resume, quit): those dim out and back
 * in. Every other press acts on the frame it arrives, so a menu-to-menu swap passes UI_SWAP_MENU: no dim, no wait.
 * The gate still holds presses back while a game hand-over is dimmed. */
#define UI_SWAP_MENU 0u
u32 ui_swap_pause_keys(u32 keys);
u32 ui_swap_confirm_keys(u32 keys, u32 swap_mask); /* a new press after the dialog has been up UI_MOTION_CONFIRM_S */
u32 ui_swap_keys(u32 keys, u32 swap_mask); /* after touch hit testing; buffers edges */
void ui_swap_hold(void); /* confirmed app exit stays dim during its final glance */
void ui_swap_release(void); /* a held dim lifts: the reveal clock begins at the next presented frame */
void ui_swap_enter(void); /* reveal clock begins at the first destination frame */
void ui_motion_shade(float alpha, int width);
void ui_pause_scene(const ui_tag_t *tag, const float *game_box, void (*game)(void *), void *ud);
void ui_pause_stamp(float k, const char *when);   /* the SAVED postmark on the paused picture: 0..1 fades it in, 0 none */
enum { UI_TONE_SOFT, UI_TONE_RED, UI_TONE_BLUE, UI_TONE_GREEN };
typedef struct {
	const char *label, *value;
	u8 icon, tone;
	bool dim;                             /* not available now: drawn faint (A still explains why) */
	bool arrows;                          /* left / right change the value: arrows around it */
	bool heading;                         /* a section heading: red margin ink, never selected */
	bb_setting_source source;             /* optional adapter-supplied provenance */
} ui_card_row_t;
typedef struct {
	const char *eyebrow, *eyebrow_right;  /* small red caps above the rule */
	const char *title;                    /* the card's heading (the game) */
	const char *notice;                   /* a line of red handwriting along the card's foot */
} ui_card_t;
/* The next ui_card_draw writes its notice in ink, not red: good news (the Updates card's
 * "BreadBox is up to date"). One draw only; a setter keeps ui_card_t (and pinned callers) as is. */
void ui_card_notice_calm(void);

void ui_card_update(flist_t *f, int sel, int n);
void ui_card_draw(const flist_t *f, int n, int sel, void (*row)(int, void *, ui_card_row_t *), void *ud, const ui_card_t *card);
u32 ui_card_input(const flist_t *f, int n, int *sel, u32 keys);   /* the list's keys, touch on the card's own rows */
bool ui_card_title_hit(void);                                    /* the touch is on the card's header row (title) */
bool ui_card_row_cut(const ui_card_row_t *r);                    /* the card cuts this focused row ("...") */
