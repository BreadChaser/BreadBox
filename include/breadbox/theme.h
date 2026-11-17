// Theme data: semantic UI roles, plus the artwork palettes owned by the disc
// placeholder and the controls. Room lighting and list looks share the same semantic roles.
#pragma once
#include <stdbool.h>
#include <3ds.h>
#include "look_theme.h"

#define TH_RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))
#define TH_RGB(r, g, b) TH_RGBA(r, g, b, 255)

#include "game_palette.h"

// Unread since the cover placeholder disc went; theme_t keeps the slot for the member offsets
// pinned per-frame code reads (see theme_t). Drop it when the layout baseline moves.
typedef struct {
	u32 title, rim, inner, outer, mirror[2], hub[2], hole, rainbow[6];
} theme_disc_t;

// Controller-button colours: touch layout, input pictures, gameplay controls.
typedef struct {
	u32 face[4];                 // X, A, B, Y
	u32 shoulder, stick, other, hide;
} theme_input_t;


// The menus' room colours (2D: the bottom screen's desk and paper, shelf labels, postcards).
typedef struct {
	u32 wall, floor, skirting;               // room surfaces
	u32 wood, back, upright;                 // shelf materials
	u32 brass, plate_ink;                    // name plates on the shelf lips
	u32 desk, paper, paper_edge, ink, ink_soft, rule, rule_soft, stamp; // bottom-screen desk and card
	u32 light, shade;                        // lamp light; dark corners (alpha = strength)
	u32 sleeve;                              // the hand's sleeve
	u32 leaf[2], wax;                        // a leaf and wax colour the paper and postcards use
	u32 cork, pin[4], postmark;              // save slots: the corkboard, its pins, the postmark ink
	u32 card_focus_icon;                    // focused card row's margin icon
	float card_dim_opacity;                   // disabled card text opacity (0..1)
} theme_room_t;

typedef struct {
	const char *id, *name;       // config.ini value, name shown in Settings (the mock's palette name)
	// Semantic UI roles: every screen and shared component colours itself from these.
	u32 surface;                 // menu pages (bottom screen, plain top screens)
	u32 heading, heading_scope;  // page title; scope text beside it
	u32 accent[4];               // rule under page titles, row edges, cover accents
	u32 select_glow, select_top, select_bottom;   // selection bar
	int row_dim[3], row_lit[3];  // row text, unselected -> selected
	u32 value, section, section_line; // value column, section heading and its rule
	u32 text_muted, text_bright; // tips and footers; scope line
	u32 hint_lib, hint_menu;     // footers: library, pause menu
	u32 message, danger;         // notices, failures
	u32 scrollbar;
	u32 help_bg, help_bar, help_title, help_impact, help_text;   // top-screen help panel
	u32 banner_bg, banner_text;  // warning banner ("fast mode is off")
	u32 game_title, game_sub;    // library title and its second line; pause menu game name
	u32 veil;                    // over the paused game
	u32 touch_face, touch_border;     // touch button panel
	u32 card_top, card_bottom;   // cover placeholder cards, unselected main menu tiles
	// Member order below keeps the offsets that pinned per-frame code reads
	// (core-first.ld: emu.o menu_draw_top reads shadow); add new members last.
	u32 light;                   // white highlights
	u32 card_stack;              // cards stacked behind a series' cover (darker per card)
	u32 version_text;            // "v0.5.1" in the corner
	u32 wordmark, wordmark_accent;    // the app wordmark's two words
	u32 folder;                  // folder browser: the games folder line
	// Icons (icons.c, the UI mock's js/icon-tokens.js): the selected picture's
	// gradient from top-left to bottom-right, and its glow (alpha = strength,
	// 0 = none). Unselected icons use theme_icon_dim().
	u32 icon_a, icon_b, icon_glow;
	theme_disc_t disc;
	game_palette_t game;         // gameplay colours (game_palette.h)
	theme_input_t input;
	u32 fx[2];                   // the animated scene's two effect colours, also the theme list swatch
	u32 shadow;                  // drop shadows and fades
	// Optional roles (0 = derived as before): touch bar strip, the soft copy
	// under large titles, and the color unselected covers fade toward.
	u32 bar, title_shadow, cover_dim;
	const theme_room_t *room;    // a room theme: the menus are a bookshelf in this room
	unsigned char icon;          // the room style's picture in the picker (UI_ICON_SUN, UI_ICON_MOON)
	unsigned char look;
} theme_t;

#define THEME_TITLE_SHADOW(t) ((t)->title_shadow ? (t)->title_shadow : (t)->shadow)
#define THEME_COVER_DIM(t) ((t)->cover_dim ? (t)->cover_dim : (t)->shadow)

// Room uses Day/Night IDs; PSX3D and Simple use their own saved IDs.
// The look a fresh BreadBox starts with.
#define THEME_DEFAULT_ID "day"
int theme_default(void);
static inline const theme_room_t *theme_room(const theme_t *t) { return t->room; }
// A saved theme id: its style, or Day for an unknown or retired one (old configs).
int theme_from_saved_id(const char *id);

int theme_count(void);
const theme_t *theme_get(int i);
const theme_t *theme(void);          // the one in use (g_cfg.theme)
int theme_from_id(const char *id);   // -1 when unknown

// c with alpha a (0..255) in place of its own
u32 theme_alpha(u32 c, int a);
// row text colour for lift 0..1 and opacity a
u32 theme_row(const theme_t *t, float lift, int a);
// unselected icons: the dim row colour a little toward the background
u32 theme_icon_dim(const theme_t *t, int a);
