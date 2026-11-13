// BreadBox bookshelf home (room themes, theme.h theme_room_t). The shelf is a
// fixed layout: one bookcase in a room, a shelf per system plus Favorites,
// game boxes spine-out; the chosen box is pulled out, turned and held up.
// shelf.c is the controller (content, navigation, launch); shelf_scene.c
// paints the room, the bookcase, the boxes and the desk (artwork, like
// scene.c); components/shelf.inc composes the screens with the shared bar.
// Approved design: UI mock js/breadbox-r8.js, variant A (the hand).
#pragma once
#include "app_policy.h"
#include <stdbool.h>
#include <stddef.h>
#include <3ds.h>
#include "bb_system.h"
#include "theme.h"
#include "room.h"

#define SHELF_TITLE BB_TITLE_LEN
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
	char title[SHELF_TITLE];
	char tag[24];            // library label: short name and number
} shelf_box;

typedef struct {
	const bb_system_t *system;   // NULL: Favorites
	const char *name;
	char plate[24];
	shelf_box *box;
	int n;
	float base, end;             // board height, last spine edge (mm)
	float hmax, s;               // tallest box (mm); close-up scale, screen px per mm at the spines
} shelf_row;
enum { SHELF_REGION_US, SHELF_REGION_PAL, SHELF_REGION_JP };
// Jewel case thickness (mm, round 8): North America / Japan one disc, PAL one disc, two or more discs.
#define SHELF_CASE_SINGLE 10.5f
#define SHELF_CASE_PAL 17.0f
#define SHELF_CASE_MULTI 25.0f
// Close-up: the row's tallest box fills about 200 px, a PS1 spine is 22 px.
// Spine edges and sizes sit on that scale's pixel grid (lay_out), so a
// settled camera shows every spine at whole pixels.
#define SHELF_CLOSE_PX 200.0f
#define SHELF_CLOSE_MAX 1.6f

enum { SHELF_ALL, SHELF_ONE };

enum { SHELF_SORT_AZ, SHELF_SORT_RECENT, SHELF_SORT_MOST, SHELF_SORTS };

typedef struct {
	shelf_row *rows;
	int nrows;
	float row_gap, case_w, case_top;
	// navigation and motion (seconds, slots)
	float t;
	int level, row;          // SHELF_ALL / SHELF_ONE, the shelf in use
	int hsel;                // shelf picked in the all-shelves view
	float hcur, hcv;
	int sel[8];              // chosen box per shelf
	float cur, cv;           // camera position along the shelf (slot units)
	float zoom, zfrom, zto, zt0;
	float pull, pfrom, pto, pt0, pdur;
	int prow, pidx;          // the pulled box
	bool arm;                // take the box out once the shelf settles
	float last_move, play_t0;
	int sort, total;
	char last_title[SHELF_TITLE];
	const char *notice;      // one-line card notice ("Collection updated")
	float notice_t;
	int side, turn_from;     // the box in the hand: 0 front, 1 spine, 2 back, 3 the open edge (L / R, round and round)
	int turn_steps;          // the turn from turn_from to side, in quarters: + R, - L (2 / -2: the back to the front)
	float turn_t0;
	int rev;                 // the take-out (1) or put-back (2) under way played backwards from rev_t (s)
	float rev_t;
	bool sped;              // the rest of a take-out at 1.5x
	int turn_queued;        // L / R pressed during the lift or a turn, added up (-2..2): turned once the box is still
	bool turn_home;         // Front pressed during a turn: back to the front once it is still
	unsigned layout;         // changes whenever the boxes move (shelf_scene rebuilds its far shelves)
} shelf_view;


int shelf_run(char *path, size_t cap);
const void *shelf_picked(void);
void shelf_release(void);
// A game of `s` was just played (path as launched): the next shelf_run opens that game's shelf
// with the game selected, never the shelf the camera was left at.
void shelf_played(const bb_system_t *s, const char *path);
void shelf_build_now(void);   // the shelves from the library now (the first visit then opens without that wait)
// The Library inside a room's bookcase (room file with a case): one scene with the room.
// shelf_preload_step() builds what the Library needs, one part per call (the shelves,
// the spine atlases, the hands), so opening it never waits; true once all is there.
bool shelf_preload_step(void);
void shelf_preload_end(bool stay_in_menu);
void shelf_library_changed(void);
bool shelf_wall_pose(room_pose *out);   // the bookcase's wall view now (false: not in a room / not built)

// shelf_scene.c
const shelf_box *shelf_pulled_box(const shelf_view *v);
void shelf_scene_top(const shelf_view *v);
void shelf_scene_bottom(const shelf_view *v);
void shelf_scene_backdrop(bool bottom);      // other menus in a room theme
void shelf_scene_desk(bool load);            // the room cards' desk alone, bottom screen (the Continue postcard); load: its textures if needed
// The room's bottom screen: the desk and one paper index card (UI mock R9/R10).
// Data only; every place of the room fills one (home.inc) and this draws it.
#define SHELF_CARD_ROWS 6
typedef struct { const char *label, *value; bool on, star; u8 icon; } shelf_card_row;   // icon: a UI_ICON_* in the margin
typedef struct {
	const char *title, *hint;      // the big line, the small line at the bottom
	shelf_card_row row[SHELF_CARD_ROWS];
	int nrows;
	bool list;                     // rows to pick from (big names, counts) instead of label / value fields
	C3D_Tex *picture;              // a save picture (256x192 in 256x256), clipped to the card's top right
	const char *picture_label;     // over the picture ("PAUSE")
	u8 page, pages;                // page dots on the foot line, right (pages > 1): page 1..pages is filled
} shelf_card;
void shelf_scene_card(const shelf_card *c);
float shelf_scene_line_width(const char *s);   // a card line's width at its size (what fits UI_ROOM_TEXT_R - UI_ROOM_TEXT_X)
// The cold start's picture on the top screen (k 0..1): the room fades up out of the dark, the lamp first.
void shelf_scene_intro(float k);
void shelf_scene_dawn(bool top);   // the picture before the room exists (home_dawn)
int shelf_scene_strip_hit(const shelf_view *v, float x, float y);   // box under a strip touch, -1
int shelf_scene_card_hit(const shelf_view *v, float x, float y);    // catalogue row, -1
void shelf_scene_frame(const shelf_view *v);  // after covers_pump(): spine baking at the next frame start
void shelf_scene_release(void);
bool shelf_scene_prepare(void);              // the spine atlases and the desk's textures
bool shelf_scene_wall_pose(const shelf_view *v, room_pose *out);
bool shelf_scene_in_room(void);               // the room is loaded with its bookcase (else no Library: shelf_run leaves)
void shelf_scene_begin(const shelf_view *v);  // the Library comes on (shelf_run)
bool shelf_scene_hands(void);                // the 3D hands hold the box (room theme with hands.bbh)
bool shelf_scene_case_ready(const shelf_view *v);   // the chosen box's cover, back and case texture are made (A waits for it)
float shelf_scene_launch_seconds(const shelf_view *v);   // Play to the game: the case opening, then black
// the hands' clip length for the box (action "pick" / "putback"; dflt without hands)
float shelf_scene_clip_seconds(const shelf_view *v, const char *action, float dflt);
// Enqueue the owned kept-box read before longer Continue metadata probes.
void shelf_restore_request(void);
