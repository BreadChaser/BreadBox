// BreadBox room walls (owner 2026-10-30): a BreadBox poster on the left wall and,
// on the right, a corkboard that fills up as you play: one postcard per save state
// and Continue point (the game's picture, its name and the date) and one ticket per
// game played (its cover), pinned at slightly random angles, newest in front, at
// most ROOM_BOARD_MAX cards (the oldest drops off).
//
// The room file holds the frames, the poster's backing and the cork (anchors `poster`,
// `board`); the poster's picture and the cards are drawn over them with their own
// textures and the room style's light there (its probes; one draw each, after the room's).
//
// room_board.c is portable (the board's cards, its file, the cards' pictures and
// the walls' mesh; host test tests/room/walls.c); room_walls.c is the 3DS side.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "room.h"

// ---- the board's cards -------------------------------------------------------
#define ROOM_BOARD_MAX 15              // cards on the board (the 16th cell of the atlas: pins, paper, shadow)
#define ROOM_BOARD_CELL 64             // every card is drawn into a 64x64 cell of one 256x256 RGB565 atlas
#define ROOM_BOARD_ATLAS 256
#define ROOM_BOARD_PATH_LEN 256
#define ROOM_BOARD_TITLE_LEN 48
enum { ROOM_CARD_STATE, ROOM_CARD_CONTINUE, ROOM_CARD_PLAYED };
typedef struct {
	uint8_t kind, slot;                // ROOM_CARD_*, the state's slot (0-9)
	bool has_cell;                     // postcards carry their picture; tickets get theirs from the cover
	uint8_t spot;                      // its place on the board and its atlas cell (0..ROOM_BOARD_MAX-1): kept for life
	uint32_t time;                     // when it was saved / played (3DS local clock)
	char game[ROOM_BOARD_PATH_LEN];    // the game's file: a card's identity is (kind, slot, game)
	char title[ROOM_BOARD_TITLE_LEN];
	uint16_t cell[ROOM_BOARD_CELL * ROOM_BOARD_CELL];   // RGB565 rows, top first
} room_card;
typedef struct {
	int n;
	room_card card[ROOM_BOARD_MAX];    // newest first
} room_board;

// A card in: the same (kind, slot, game) is replaced (it keeps its spot), the list stays newest
// first and the oldest drops off past ROOM_BOARD_MAX (the new card takes its spot), else the
// first free spot from the board's middle out. False: older than every card of a full board.
bool room_board_put(room_board *b, const room_card *c);
// That card's time, 0 when the board does not have it.
uint32_t room_board_time(const room_board *b, int kind, int slot, const char *game);
// The board's file: "BBRD", u16 version, u16 count, then per card u8 kind, slot,
// has_cell, spot; u32 time; char game[256]; char title[48]; with has_cell its 64x64 RGB565 cell.
size_t room_board_size(const room_board *b);
size_t room_board_write(const room_board *b, uint8_t *out, size_t cap);   // bytes written, 0 too small
bool room_board_read(room_board *b, const uint8_t *data, size_t size);    // false: not a board file (b empty)

// The cards' pictures, RGB565 into a 64x64 cell (rows top first). A postcard is
// ROOM_POSTCARD_W x ROOM_POSTCARD_H: the picture (centre-cropped, box-filtered),
// the game's name and a line with the slot / Continue and the date. A ticket is
// ROOM_TICKET_W x ROOM_TICKET_H: "ADMIT ONE", the cover, the name. px: rows of w x h.
#define ROOM_POSTCARD_W 64
#define ROOM_POSTCARD_H 52
#define ROOM_TICKET_W 46
#define ROOM_TICKET_H 64
// a source picture: plain rows (stride texels per row) or a PICA-tiled texture (tiled = its width)
typedef struct {
	const uint16_t *px;
	int w, h;        // the picture's part (from the top left)
	int stride;      // texels per row; for a tiled texture its width
	bool tiled;
} room_picture;
void room_board_postcard(uint16_t *cell, const room_picture *pic, const char *title, int kind, int slot, uint32_t time);
void room_board_ticket(uint16_t *cell, const room_picture *cover, const char *title, uint32_t time);
// the 16th cell: paper white, a card shadow, four pin heads (texel rects for the mesh)
void room_board_utility(uint16_t *cell);
// A 3x5 pixel font (upper case, digits, a few signs) into an RGB565 cell; returns the x after the text.
int room_board_text(uint16_t *cell, int stride, int x, int y, int maxx, const char *s, uint16_t ink);

// ---- the walls' mesh (room_vertex, palette slot 0, lit by the room style's probes) -----
typedef struct {
	float plane[6];      // anchor `poster` / `board`: plane x, centre y, centre z, inner width, inner height, normal x
	float light[3];      // the room style's light on it (1.0 = neutral)
	float unit;          // the style's vertex colour bytes per 1.0 of light (room_light_unit)
	bool ok;
} room_wall;
// the wall's plane and its light in the style in use (made again when the style changes)
bool room_wall_of(const room_file *f, const char *name, room_wall *w);
// a point on the wall: s 0..1 left to right as seen from the room, t 0..1 top to bottom; off: mm towards the room.
// Its colour is the wall's light at 128 = 1.0 (the meshes' unlit scale; room_walls_relight puts it in the style's unit)
void room_wall_point(const room_wall *w, float s, float t, float off, room_vertex *v);
// The poster: one quad (6 vertices) over its inner area, uv = the picture's texels in its texture (u1, v1).
int room_poster_mesh(const room_wall *w, float u1, float v1, room_vertex *out);
// The walls' meshes are made unlit (light 1, 128 = 1.0) and lit here: out = unlit x the wall's light x its unit / 128,
// the first nposter vertices the poster's, the rest the board's. A room style change relights from the unlit copy
// (no drift).
void room_walls_relight(const room_vertex *unlit, room_vertex *out, int n, int nposter, const room_wall *poster, const room_wall *board);
// Where a card sits (by its spot): centre (s, t), size (mm), angle (radians), its pin colour (0-3).
typedef struct { float s, t, w, h, angle; int pin; } room_card_place;
void room_board_place(const room_wall *w, const room_card *c, room_card_place *p);
// Every card back to front (oldest first): shadow, card, pin, 18 vertices each. A card uses
// the atlas cell of its spot; the 16th cell holds the shadow and the pins. Returns the vertex count.
#define ROOM_BOARD_VERTS (ROOM_BOARD_MAX * 18)
int room_board_mesh(const room_wall *w, const room_board *b, room_vertex *out, int cap);

// ---- room_walls.c (3DS) ---------------------------------------------------------
#ifdef __3DS__
#include <citro3d.h>
#include "bb_system.h"
void room_walls_open(const room_file *f);  // room_scene.c, after the room has loaded: textures, the board's file
void room_walls_release(void);             // room_scene.c, with the room
void room_walls_restyle(const room_file *f);   // room_scene.c, after a room style change: the walls' new light
void room_walls_frame(void);               // once per frame before drawing: finished reads, a card per frame
// Draws the poster and the cards (room_scene.c, inside the room's draw: its program, attributes,
// combiner stage 0 are set); u_texscale: the shader's uv scale uniform.
void room_walls_draw(int u_texscale);
// main.c, after every game: its new postcards and its ticket, made after the game's own
// saves on the later thread (bb_later), then the board's file is written.
void room_walls_game_ended(const bb_system_t *system, const char *path, const char *title);
#endif
