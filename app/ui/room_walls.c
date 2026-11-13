// BreadBox room walls, 3DS side (room_walls.h): the poster's texture, the board's
// atlas and mesh, the board's file read in the background, tickets from the covers,
// and the job after every game that pins its new postcards and its ticket.
// Nothing here reads the SD card on the menu thread. GPL-2.0-or-later.
#include <3ds.h>
#include <citro3d.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tex3ds.h>
#include <math.h>
#include <stddef.h>
#include "safefile.h"
#include "app_paths.h"
#include "app_log.h"
#include "app_config.h"
#include "covers.h"
#include "bb_index.h"
#include "bb_registry.h"
#include "bb_save.h"
#include "bb_slots.h"
#include "room_walls.h"
#include "safefile.h"

#define BOARD_FILE APP_SD_ROOT "/board.dat"
#define POSTER_FILE "romfs:/gfx/room_poster.t3x"   // assets/gfx/room_poster.png: swap the picture there
#define VERTS (6 + ROOM_BOARD_VERTS)

static struct {
	bool open;
	room_wall poster_w, board_w;
	C3D_Tex atlas, poster;
	Tex3DS_Texture poster_t3x;
	float pu1, pv1;                 // the poster's picture in its texture (texels)
	room_vertex *vb[2];
	room_vertex unlit[VERTS];          // the meshes at light 1 (relight)
	C3D_BufInfo buf[2];
	int cur, nposter, nboard;
	bb_later_file board_rd, poster_rd;
	bool board_reading, poster_reading;
	unsigned loaded_gen;
	// tickets still waiting for their game's cover: its cover id from the library index (-2 not looked up yet,
	// -1 not in the library), then the cover itself
	struct { int spot; char game[ROOM_BOARD_PATH_LEN], title[ROOM_BOARD_TITLE_LEN]; uint32_t time; int cover, tries; } wait[ROOM_BOARD_MAX];
	int nwait;
	struct { bool on; unsigned sys, row, gen; } look;   // the walk through the library index, a little each frame
	unsigned frame;
} W;

static volatile unsigned board_gen = 1;   // bumped by the job after it wrote the board's file

// one 64x64 cell into the tiled atlas (rows top first, t = 1 at the top as tile16 in room_scene.c)
static void cell_upload(int cell, const uint16_t *px)
{
	u16 *dst = W.atlas.data;
	int x0 = cell % 4 * ROOM_BOARD_CELL, y0 = cell / 4 * ROOM_BOARD_CELL;
	for (int y = 0; y < ROOM_BOARD_CELL; y++)
		for (int x = 0; x < ROOM_BOARD_CELL; x++) {
			int tx = x0 + x, ty = y0 + y;
			u32 i = (((ty >> 3) * (ROOM_BOARD_ATLAS >> 3) + (tx >> 3)) << 6) |
				(tx & 1) | ((ty & 1) << 1) | ((tx & 2) << 1) | ((ty & 2) << 2) | ((tx & 4) << 2) | ((ty & 4) << 3);
			dst[i] = px[y * ROOM_BOARD_CELL + x];
		}
}

static void flush_atlas(void) { GSPGPU_FlushDataCache(W.atlas.data, W.atlas.size); }

// the walls' meshes unlit (light 1), kept for relighting; then lit into the other buffer (the frame in
// flight may still read this one)
static void relight(void)
{
	int k = W.cur ^ 1;
	room_walls_relight(W.unlit, W.vb[k], W.nposter + W.nboard, W.nposter, &W.poster_w, &W.board_w);
	GSPGPU_FlushDataCache(W.vb[k], VERTS * sizeof(room_vertex));
	W.cur = k;
}

static void mesh(const room_board *b)
{
	room_wall pw = W.poster_w, bw = W.board_w;
	pw.light[0] = pw.light[1] = pw.light[2] = bw.light[0] = bw.light[1] = bw.light[2] = 1;
	W.nposter = pw.ok ? room_poster_mesh(&pw, 256, 256, W.unlit) : 0;   // uv 0..256: scaled to the picture at draw
	W.nboard = b && bw.ok ? room_board_mesh(&bw, b, W.unlit + W.nposter, VERTS - W.nposter) : 0;
	relight();
}

void room_walls_open(const room_file *f)
{
	if (W.open) return;
	memset(&W, 0, sizeof(W));
	room_wall_of(f, "poster", &W.poster_w);
	room_wall_of(f, "board", &W.board_w);
	if (!W.poster_w.ok && !W.board_w.ok) return;   // a room file without the walls
	for (int i = 0; i < 2; i++) {
		W.vb[i] = linearAlloc(VERTS * sizeof(room_vertex));
		if (!W.vb[i]) { room_walls_release(); return; }
		BufInfo_Init(&W.buf[i]);
		BufInfo_Add(&W.buf[i], W.vb[i], sizeof(room_vertex), 3, 0x210);   // room_vertex: S.attr's 3 attributes (tests/room/loaders.py)
	}
	if (W.board_w.ok) {
		if (!C3D_TexInit(&W.atlas, ROOM_BOARD_ATLAS, ROOM_BOARD_ATLAS, GPU_RGB565)) { room_walls_release(); return; }
		C3D_TexSetFilter(&W.atlas, GPU_LINEAR, GPU_NEAREST);
		C3D_TexSetWrap(&W.atlas, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		static uint16_t util[ROOM_BOARD_CELL * ROOM_BOARD_CELL];
		room_board_utility(util);
		cell_upload(ROOM_BOARD_MAX, util);
		flush_atlas();
		W.board_rd = (bb_later_file){ BOARD_FILE, NULL, 0, 0, NULL };
		bb_later_read(&W.board_rd);
		W.board_reading = true;
		W.loaded_gen = board_gen;
	}
	if (W.poster_w.ok) {
		W.poster_rd = (bb_later_file){ POSTER_FILE, NULL, 0, 0, NULL };
		bb_later_read(&W.poster_rd);
		W.poster_reading = true;
	}
	W.open = true;
	mesh(NULL);
}

// The walls in another room style's light: relit from their unlit meshes.
void room_walls_restyle(const room_file *f)
{
	if (!W.open) return;
	room_wall_of(f, "poster", &W.poster_w);
	room_wall_of(f, "board", &W.board_w);
	relight();
}

void room_walls_release(void)
{
	if (W.board_reading) { bb_later_cancel(&W.board_rd); free(W.board_rd.buf); }
	if (W.poster_reading) { bb_later_cancel(&W.poster_rd); free(W.poster_rd.buf); }
	if (W.poster_t3x) Tex3DS_TextureFree(W.poster_t3x);
	if (W.poster.data) C3D_TexDelete(&W.poster);
	if (W.atlas.data) C3D_TexDelete(&W.atlas);
	for (int i = 0; i < 2; i++) if (W.vb[i]) linearFree(W.vb[i]);
	memset(&W, 0, sizeof(W));
}

// Finds the waiting tickets' cover ids: rows of the library index (the cheap per-row read, no catalog
// rebuild), for at most budget ticks a frame, resumed next frame; one walk for all tickets. Games not in
// the library are looked for again when the index changes.
static void look_step(u64 budget)
{
	unsigned gen = bb_index_generation();
	if (!W.look.on) {
		bool want = false;
		for (int k = 0; k < W.nwait; k++) {
			if (W.wait[k].cover == -1 && gen != W.look.gen) W.wait[k].cover = -2;
			want |= W.wait[k].cover == -2;
		}
		if (!want) return;
		W.look.on = true;
		W.look.sys = W.look.row = 0;
		W.look.gen = gen;
	}
	const bb_system_t *order[BB_MAX_SYSTEMS];
	unsigned ns = bb_system_display_order(order, BB_MAX_SYSTEMS, true);
	u64 t0 = svcGetSystemTick();
	char path[ROOM_BOARD_PATH_LEN];
	while (W.look.sys < ns && svcGetSystemTick() - t0 < budget) {
		const bb_system_t *sys = order[W.look.sys];
		if ((int)W.look.row >= bb_system_library_count(sys)) { W.look.sys++; W.look.row = 0; continue; }
		bb_entry e;
		int row = (int)W.look.row++;
		if (!bb_index_get(sys, row, path, sizeof(path), &e)) continue;
		for (int k = 0; k < W.nwait; k++)
			if (W.wait[k].cover == -2 && !strcmp(path, W.wait[k].game)) W.wait[k].cover = bb_system_cover_id(sys, row);
	}
	if (W.look.sys >= ns) {   // walked it all: the rest are not in the library (for now)
		W.look.on = false;
		for (int k = 0; k < W.nwait; k++)
			if (W.wait[k].cover == -2) W.wait[k].cover = -1;
	}
}

// a ticket's cover: the small copy if the covers have it, else the full one once it has loaded
static bool ticket_try(int k)
{
	if (W.wait[k].cover < 0) return false;
	Tex3DS_SubTexture sub;
	C3D_Tex *t = covers_small(W.wait[k].cover, &sub);
	if (!t) t = covers_get(W.wait[k].cover, &sub);
	if (!t || t->fmt != GPU_RGB565 || !t->data) return false;
	room_picture pic = { t->data, (int)((sub.right - sub.left) * t->width + .5f), (int)((sub.top - sub.bottom) * t->height + .5f), t->width, true };
	static uint16_t cell[ROOM_BOARD_CELL * ROOM_BOARD_CELL];
	room_board_ticket(cell, &pic, W.wait[k].title, W.wait[k].time);
	cell_upload(W.wait[k].spot, cell);
	flush_atlas();
	return true;
}

static void board_loaded(const uint8_t *data, size_t size)
{
	room_board *b = malloc(sizeof(room_board));
	if (!b) return;
	if (!room_board_read(b, data, size) && data) log_printf("room: %s is not a board file\n", BOARD_FILE);
	static uint16_t cell[ROOM_BOARD_CELL * ROOM_BOARD_CELL];
	W.nwait = 0;
	for (int i = 0; i < b->n; i++) {
		const room_card *c = &b->card[i];
		if (c->has_cell) { cell_upload(c->spot, c->cell); continue; }
		room_board_ticket(cell, NULL, c->title, c->time);   // its cover comes later (ticket_try)
		cell_upload(c->spot, cell);
		W.wait[W.nwait].spot = c->spot;
		memcpy(W.wait[W.nwait].game, c->game, sizeof(c->game));
		memcpy(W.wait[W.nwait].title, c->title, sizeof(c->title));
		W.wait[W.nwait].time = c->time;
		W.wait[W.nwait].cover = -2;
		W.wait[W.nwait++].tries = 0;
	}
	flush_atlas();
	mesh(b);
	log_printf("room: board %d cards (%d tickets waiting for covers)\n", b->n, W.nwait);
	free(b);
}

void room_walls_frame(void)
{
	if (!W.open) return;
	W.frame++;
	if (W.poster_reading) {
		int st = bb_later_state(&W.poster_rd);
		if (st == BB_LATER_DONE || st == BB_LATER_FAILED) {
			W.poster_reading = false;
			if (st == BB_LATER_DONE)
				W.poster_t3x = Tex3DS_TextureImport(W.poster_rd.buf, W.poster_rd.size, &W.poster, NULL, false);
			free(W.poster_rd.buf);
			W.poster_rd.buf = NULL;
			if (W.poster_t3x) {
				const Tex3DS_SubTexture *sub = Tex3DS_GetSubTexture(W.poster_t3x, 0);
				W.pu1 = sub->right * W.poster.width;
				W.pv1 = (1 - sub->bottom) * W.poster.height;
				C3D_TexSetFilter(&W.poster, GPU_LINEAR, GPU_LINEAR);
				C3D_TexSetWrap(&W.poster, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
			} else log_printf("room: no poster (%s)\n", POSTER_FILE);
		}
		return;   // one thing per frame
	}
	if (W.board_reading) {
		int st = bb_later_state(&W.board_rd);
		if (st == BB_LATER_DONE || st == BB_LATER_FAILED) {
			W.board_reading = false;
			board_loaded(st == BB_LATER_DONE ? W.board_rd.buf : NULL, st == BB_LATER_DONE ? W.board_rd.size : 0);
			free(W.board_rd.buf);
			W.board_rd.buf = NULL;
		}
		return;
	}
	if (W.loaded_gen != board_gen) {   // a game's job wrote the board since: read it again
		W.loaded_gen = board_gen;
		W.board_rd = (bb_later_file){ BOARD_FILE, NULL, 0, 0, NULL };
		bb_later_read(&W.board_rd);
		W.board_reading = true;
		return;
	}
	if (!W.nwait) return;
	look_step(SYSCLOCK_ARM11 / 1000);   // at most 1 ms a frame
	if (W.frame % 20 == 0) {   // tickets with a cover id: one look at the covers every third of a second
		int k = W.frame / 20 % W.nwait;
		if (ticket_try(k) || (W.wait[k].cover >= 0 && ++W.wait[k].tries > 90))   // a cover that never comes: the plain ticket stays
			W.wait[k] = W.wait[--W.nwait];
	}
}

void room_walls_draw(int u_texscale)
{
	if (!W.open || !(W.nposter + W.nboard)) return;
	C3D_SetBufInfo(&W.buf[W.cur]);
	C3D_CullFace(GPU_CULL_NONE);
	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_COLOR);   // painted back to front over the cork and the backing
	if (W.nposter && W.poster_t3x) {
		C3D_FVUnifSet(GPU_VERTEX_SHADER, u_texscale, W.pu1 / (16 * 256.0f * W.poster.width), -W.pv1 / (16 * 256.0f * W.poster.height), 0, 1);
		C3D_TexBind(0, &W.poster);
		C3D_DrawArrays(GPU_TRIANGLES, 0, W.nposter);
	}
	if (W.nboard && W.atlas.data) {
		C3D_FVUnifSet(GPU_VERTEX_SHADER, u_texscale, 1.0f / (16 * ROOM_BOARD_ATLAS), -1.0f / (16 * ROOM_BOARD_ATLAS), 0, 1);
		C3D_TexBind(0, &W.atlas);
		C3D_DrawArrays(GPU_TRIANGLES, W.nposter, W.nboard);
	}
	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
}

// ---------------------------------------------------------------- after a game (the later thread)

typedef struct {
	bool ps1;                        // savestates.c's names (<prefix>.stN.thm, .sta.thm), else the save service's
	char base[512];                  // <base>.stateN.thm (bb_slots.h: Continue is .state10)
	char game[ROOM_BOARD_PATH_LEN], title[ROOM_BOARD_TITLE_LEN];
	uint32_t ended;
} scan_job;

#define PIC_MAX (256 * 240)

// a state's picture: its time, and its pixels into px (w x h rows) when want. slot: the board's card
// index (0..11, shown as slot + 1), BB_SLOTS for Continue; the files behind them are bb_slots.h's
static bool thm_read(const scan_job *j, int slot, uint32_t *saved, uint16_t *px, int *w, int *h, bool want)
{
	char path[600];
	if (j->ps1) {
		if (slot == BB_SLOTS) snprintf(path, sizeof(path), "%s.sta.thm", j->base);
		else snprintf(path, sizeof(path), "%s.st%d.thm", j->base, bb_ps1_slot_file(slot));
	} else snprintf(path, sizeof(path), "%s.state%d.thm", j->base, slot == BB_SLOTS ? BB_SLOT_CONTINUE : bb_slot_file(slot));
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	bool ok;
	if (j->ps1) {   // savestates.c: u32 "P3TH", u16 w, h, u32 saved
		uint8_t hd[12];
		ok = fread(hd, 1, 12, f) == 12 && !memcmp(hd, "P3TH", 4);
		*w = hd[4] | hd[5] << 8;
		*h = hd[6] | hd[7] << 8;
		*saved = hd[8] | hd[9] << 8 | hd[10] << 16 | (uint32_t)hd[11] << 24;
	} else {        // bb_save.c: "BBTHUMB1", u32 crc, saved, played, u16 w, h
		uint8_t hd[24];
		ok = fread(hd, 1, 24, f) == 24 && !memcmp(hd, "BBTHUMB1", 8);
		*saved = hd[12] | hd[13] << 8 | hd[14] << 16 | (uint32_t)hd[15] << 24;
		*w = hd[20] | hd[21] << 8;
		*h = hd[22] | hd[23] << 8;
	}
	ok = ok && *w > 0 && *h > 0 && *w * *h <= PIC_MAX && *saved;
	if (ok && want) ok = fread(px, 2, (size_t)(*w * *h), f) == (size_t)(*w * *h);
	fclose(f);
	return ok;
}

static void scan(void *arg)
{
	scan_job *j = arg;
	u64 t0 = svcGetSystemTick();
	room_board *b = malloc(sizeof(room_board));
	room_card *c = malloc(sizeof(room_card));
	if (!b || !c) goto out;
	b->n = 0;
	safe_recover(BOARD_FILE);
	FILE *f = fopen(BOARD_FILE, "rb");
	if (f) {
		fseek(f, 0, SEEK_END);
		long n = ftell(f);
		fseek(f, 0, SEEK_SET);
		uint8_t *d = n > 0 ? malloc((size_t)n) : NULL;
		if (d && fread(d, 1, (size_t)n, f) == (size_t)n) room_board_read(b, d, (size_t)n);
		free(d);
		fclose(f);
	}
	int added = 0;
	memset(c, 0, offsetof(room_card, cell));
	c->kind = ROOM_CARD_PLAYED;
	c->time = j->ended;
	snprintf(c->game, sizeof(c->game), "%s", j->game);
	snprintf(c->title, sizeof(c->title), "%s", j->title);
	added += room_board_put(b, c);
	if (j->base[0]) {
		uint16_t *px = malloc(PIC_MAX * 2);
		for (int slot = 0; slot <= BB_SLOTS && px; slot++) {
			int kind = slot == BB_SLOTS ? ROOM_CARD_CONTINUE : ROOM_CARD_STATE, w, h;
			uint32_t saved;
			if (!thm_read(j, slot, &saved, px, &w, &h, false) || saved <= room_board_time(b, kind, slot, j->game)) continue;
			if (!thm_read(j, slot, &saved, px, &w, &h, true)) continue;
			memset(c, 0, offsetof(room_card, cell));
			c->kind = (uint8_t)kind;
			c->slot = (uint8_t)(slot == BB_SLOTS ? 0 : slot);
			c->has_cell = true;
			c->time = saved;
			snprintf(c->game, sizeof(c->game), "%s", j->game);
			snprintf(c->title, sizeof(c->title), "%s", j->title);
			room_picture pic = { px, w, h, w, false };
			room_board_postcard(c->cell, &pic, c->title, kind, c->slot, saved);
			added += room_board_put(b, c);
		}
		free(px);
	}
	size_t n = room_board_size(b);
	uint8_t *d = malloc(n);
	FILE *o = d ? safe_begin(BOARD_FILE) : NULL;
	bool ok = o && room_board_write(b, d, n) == n && fwrite(d, 1, n, o) == n;
	if (o) ok = safe_finish(o, BOARD_FILE, ok);
	free(d);
	if (ok) __atomic_add_fetch(&board_gen, 1, __ATOMIC_RELEASE);
	log_printf("room: board +%d cards, %d in all, %s (%u ms)\n", added, b->n, ok ? "written" : "NOT written",
		(unsigned)((svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000)));
out:
	free(c);
	free(b);
	free(j);
}

// "Crash Bandicoot (USA)" -> "Crash Bandicoot"; no title: the file's name
static void clean_title(char *out, size_t cap, const char *title, const char *path)
{
	const char *s = title && title[0] ? title : (strrchr(path, '/') ? strrchr(path, '/') + 1 : path);
	snprintf(out, cap, "%s", s);
	if (!(title && title[0])) { char *dot = strrchr(out, '.'); if (dot) *dot = 0; }
	char *p = strstr(out, " (");
	if (!p) p = strstr(out, " [");
	if (p && p > out) *p = 0;
}

void room_walls_game_ended(const bb_system_t *system, const char *path, const char *title)
{
	if (!system || !path || !path[0]) return;
	scan_job *j = calloc(1, sizeof(scan_job));
	if (!j) return;
	snprintf(j->game, sizeof(j->game), "%s", path);
	clean_title(j->title, sizeof(j->title), title, path);
	j->ended = (uint32_t)time(NULL);
	if (system->save) {   // the shared save service: the session's own state files
		const char *base = bb_save_last_base();
		if (base && base[0]) snprintf(j->base, sizeof(j->base), "%s", base);
	} else if (!strcmp(g_cfg.last_game_path, path) && g_cfg.last_game_state[0]) {   // PS1
		j->ps1 = true;
		snprintf(j->base, sizeof(j->base), "%s/%s", PSX3D_STATE_DIR, g_cfg.last_game_state);
	}
	bb_later(scan, j);   // after the game's own saves (one thread, in order)
}
