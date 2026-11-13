// Host checks for the room walls' portable half (app/ui/room_board.c) against
// the shipped room file: the board's cards (order, places kept, the cap), its file,
// the cards' pictures, the walls' anchors, poses and mesh.
#include "room_walls.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void *slurp(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	void *p = aligned_alloc(16, (size_t)(n + 15) & ~(size_t)15);
	if (p && fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); p = NULL; }
	fclose(f);
	*size = (size_t)n;
	return p;
}

static float project(const float m[16], const float q[3], float *sx, float *sy)
{
	float c[4];
	for (int r = 0; r < 4; r++) c[r] = m[r * 4] * q[0] + m[r * 4 + 1] * q[1] + m[r * 4 + 2] * q[2] + m[r * 4 + 3];
	*sx = 200 + 200 * (-c[1] / c[3]);
	*sy = 120 - 120 * (c[0] / c[3]);
	return c[3];
}

static room_card card(int kind, int slot, const char *game, uint32_t t)
{
	room_card c;
	memset(&c, 0, sizeof(c));
	c.kind = (uint8_t)kind;
	c.slot = (uint8_t)slot;
	c.time = t;
	c.has_cell = kind != ROOM_CARD_PLAYED;
	snprintf(c.game, sizeof(c.game), "%s", game);
	snprintf(c.title, sizeof(c.title), "Game %s", game);
	for (int i = 0; i < ROOM_BOARD_CELL * ROOM_BOARD_CELL; i++) c.cell[i] = (uint16_t)(t * 7 + i);
	return c;
}

static bool spots_unique(const room_board *b)
{
	unsigned used = 0;
	for (int i = 0; i < b->n; i++) {
		if (b->card[i].spot >= ROOM_BOARD_MAX || (used >> b->card[i].spot & 1)) return false;
		used |= 1u << b->card[i].spot;
	}
	return true;
}

static void ppm(const char *path, const uint16_t *cell)
{
	FILE *o = fopen(path, "wb");
	if (!o) return;
	fprintf(o, "P6 %d %d 255\n", ROOM_BOARD_CELL * 4, ROOM_BOARD_CELL * 4);
	for (int y = 0; y < ROOM_BOARD_CELL * 4; y++)
		for (int x = 0; x < ROOM_BOARD_CELL * 4; x++) {
			uint16_t c = cell[y / 4 * ROOM_BOARD_CELL + x / 4];
			fputc((c >> 11) * 255 / 31, o); fputc((c >> 5 & 63) * 255 / 63, o); fputc((c & 31) * 255 / 31, o);
		}
	fclose(o);
}

int main(int argc, char **argv)
{
	static room_board b, b2;
	// ---- cards: newest first, a re-save keeps its spot and moves to the front, the cap
	room_card c = card(ROOM_CARD_PLAYED, 0, "a.gba", 100);
	CHECK(room_board_put(&b, &c) && b.n == 1 && b.card[0].spot == 7, "the first card sits in the middle (spot %d)", b.card[0].spot);
	c = card(ROOM_CARD_STATE, 3, "a.gba", 90);
	CHECK(room_board_put(&b, &c) && b.n == 2 && b.card[1].time == 90 && b.card[0].time == 100, "older card behind");
	int spot3 = b.card[1].spot;
	c = card(ROOM_CARD_STATE, 3, "a.gba", 200);
	CHECK(room_board_put(&b, &c) && b.n == 2 && b.card[0].time == 200 && b.card[0].spot == spot3, "a re-save replaces its card in place");
	CHECK(room_board_time(&b, ROOM_CARD_STATE, 3, "a.gba") == 200 && !room_board_time(&b, ROOM_CARD_STATE, 4, "a.gba"), "card times");
	c = card(ROOM_CARD_CONTINUE, 7, "a.gba", 201);   // Continue: one per game whatever the slot
	room_board_put(&b, &c);
	c = card(ROOM_CARD_CONTINUE, 0, "a.gba", 202);
	room_board_put(&b, &c);
	CHECK(b.n == 3 && room_board_time(&b, ROOM_CARD_CONTINUE, 5, "a.gba") == 202, "one Continue card per game");
	for (int i = 0; i < 30; i++) {
		char g[16];
		snprintf(g, sizeof(g), "g%d.bin", i);
		c = card(ROOM_CARD_STATE, i % 10, g, 300 + (uint32_t)i);
		CHECK(room_board_put(&b, &c), "a newer card goes in");
		CHECK(b.n <= ROOM_BOARD_MAX && spots_unique(&b), "cap and unique spots after %d", i);
	}
	CHECK(b.n == ROOM_BOARD_MAX && b.card[0].time == 329 && b.card[ROOM_BOARD_MAX - 1].time == 315, "the newest 15 stay");
	for (int i = 1; i < b.n; i++) CHECK(b.card[i - 1].time >= b.card[i].time, "newest first");
	c = card(ROOM_CARD_STATE, 1, "old.bin", 5);
	CHECK(!room_board_put(&b, &c) && b.n == ROOM_BOARD_MAX, "a card older than a full board is refused");

	// ---- the file: a round trip, a cut file, a damaged record
	size_t n = room_board_size(&b);
	uint8_t *d = malloc(n);
	CHECK(room_board_write(&b, d, n) == n && !room_board_write(&b, d, n - 1), "write");
	CHECK(room_board_read(&b2, d, n) && b2.n == b.n, "read back %d", b2.n);
	for (int i = 0; i < b.n && i < b2.n; i++)
		CHECK(!memcmp(&b.card[i], &b2.card[i], sizeof(room_card)), "card %d differs after the round trip", i);
	CHECK(!room_board_read(&b2, d, n - 100) && b2.n == 0, "a cut file is refused");
	d[8 + 3] = d[8 + 8 + 256 + 48 + 8192 + 3];   // the first record takes the second's spot: the second is left out
	CHECK(room_board_read(&b2, d, n) && b2.n == b.n - 1 && spots_unique(&b2), "a spot taken twice: %d cards", b2.n);
	d[0] = 'X';
	CHECK(!room_board_read(&b2, d, n), "wrong magic");
	free(d);

	// ---- pictures: a plain and a tiled source, both in their cells
	static uint16_t px[256 * 192], tiled[64 * 64], cell[ROOM_BOARD_CELL * ROOM_BOARD_CELL];
	for (int i = 0; i < 256 * 192; i++) px[i] = 0xF800;   // red
	room_picture pic = { px, 256, 192, 256, false };
	room_board_postcard(cell, &pic, "Crash Bandicoot", ROOM_CARD_STATE, 3, 1761800000);
	CHECK(cell[20 * ROOM_BOARD_CELL + 30] == 0xF800, "the picture fills the postcard's middle");
	CHECK(cell[0] != cell[4], "the airmail edge");
	if (getenv("ROOM_WALLS_PPM")) { char p[512]; snprintf(p, sizeof(p), "%s-postcard.ppm", getenv("ROOM_WALLS_PPM")); ppm(p, cell); }
	for (int i = 0; i < 64 * 64; i++) tiled[i] = 0x001F;   // blue
	room_picture cov = { tiled, 40, 64, 64, true };
	room_board_ticket(cell, &cov, "Advance Wars", 1761800000);
	CHECK(cell[30 * ROOM_BOARD_CELL + 23] == 0x001F, "the cover fills the ticket's middle");
	if (getenv("ROOM_WALLS_PPM")) { char p[512]; snprintf(p, sizeof(p), "%s-ticket.ppm", getenv("ROOM_WALLS_PPM")); ppm(p, cell); }
	room_board_ticket(cell, NULL, NULL, 0);   // no cover, no title, no time
	room_board_postcard(cell, NULL, "A very long title that does not fit on one card", ROOM_CARD_CONTINUE, 0, 0);
	room_board_utility(cell);
	CHECK(cell[0] == 0xFFFF, "the utility cell's paper is white");
	CHECK(room_board_text(cell, ROOM_BOARD_CELL, 60, 0, 64, "ABC", 0) == 64, "text stops at its edge");

	// ---- the walls in the shipped room file
	const char *path = argc > 1 ? argv[1] : "assets/romfs/room/room.bbr";
	size_t size;
	void *data = slurp(path, &size);
	if (!data) { printf("FAIL: cannot read %s\n", path); return 1; }
	room_file f;
	char err[64];
	CHECK(room_parse(&f, data, size, err, sizeof(err)), "parse: %s", err);
	room_wall pw, bw;
	CHECK(room_wall_of(&f, "poster", &pw) && pw.plane[5] == 1, "the poster on the left wall");
	CHECK(room_wall_of(&f, "board", &bw) && bw.plane[5] == -1, "the board on the right wall");
	// the walls take the room style's light (its probes), and a style change changes it
	CHECK(pw.light[0] > .1f && bw.light[0] > .1f && pw.light[0] < 2.1f, "wall light %.2f %.2f", pw.light[0], bw.light[0]);
	{
		room_wall nw;
		CHECK(room_style_use(&f, 1) && room_wall_of(&f, "board", &nw) && fabsf(nw.light[0] - bw.light[0]) > .01f, "the night board's light");
		room_style_use(&f, 0);
	}
	if (fails) { free(data); return 1; }
	// both walls are seen whole from their poses, and not at all from home
	struct { const char *pose; room_wall *w; float min_px; } views[] = { { "poster", &pw, 100 }, { "board", &bw, 200 } };
	for (int k = 0; k < 2; k++) {
		room_pose p;
		CHECK(room_pose_of(&f, views[k].pose, &p), "pose.%s", views[k].pose);
		float m[16], r[3], u[3], at[3], x0 = 1e9f, x1 = -1e9f;
		room_camera(&p, 0, m, r, u, at);
		for (int i = 0; i < 4; i++) {
			room_vertex v;
			room_wall_point(views[k].w, i & 1, i >> 1, 0, &v);
			float q[3] = { v.x, v.y, v.z }, sx, sy;
			CHECK(project(m, q, &sx, &sy) > 0 && sx >= 0 && sx <= 400 && sy >= 0 && sy <= 240, "%s corner %d off screen (%.0f, %.0f)", views[k].pose, i, sx, sy);
			x0 = fminf(x0, sx); x1 = fmaxf(x1, sx);
		}
		CHECK(x1 - x0 >= views[k].min_px, "%s only %.0f px wide", views[k].pose, x1 - x0);
		room_pose h;
		room_pose_of(&f, "home", &h);
		room_camera(&h, 0, m, r, u, at);
		// the approved Home view (round 2 stills) has no wall pictures: no part of either, frame included, in it
		for (int i = 0; i < 4; i++) {
			room_vertex v;
			room_wall_point(views[k].w, (i & 1) ? 1.15f : -.15f, (i >> 1) ? 1.15f : -.15f, 0, &v);
			float q[3] = { v.x, v.y, v.z }, sx, sy;
			CHECK(project(m, q, &sx, &sy) <= 0 || sx < 0 || sx > 400, "%s corner %d in the home view (%.0f)", views[k].pose, i, sx);
		}
	}
	// the glides: to a wall and back, the focus stays on its place
	room_view v;
	room_view_init(&v, &f, ROOM_CONTINUE, -1);
	room_view_go(&v, ROOM_POSTER);
	CHECK(v.focus == ROOM_CONTINUE && v.place == ROOM_POSTER, "a wall keeps the focus");
	for (int i = 0; i < 100; i++) room_view_step(&v, 1 / 60.0f);
	CHECK(room_view_arrived(&v), "at the poster");
	room_pose at_p = room_view_pose(&v), want;
	room_pose_of(&f, "poster", &want);
	CHECK(fabsf(at_p.pos[0] - want.pos[0]) < 1 && fabsf(at_p.look[2] - want.look[2]) < 1, "the poster's pose");
	room_view_back(&v);
	for (int i = 0; i < 100; i++) room_view_step(&v, 1 / 60.0f);
	CHECK(v.mode == ROOM_HOME && v.focus == ROOM_CONTINUE, "back home");
	room_view_go(&v, ROOM_BOARD + 1);
	CHECK(v.mode == ROOM_HOME, "no place past the walls");

	// the mesh: the poster's quad on its plane, every card inside the cork, lit by the wall's light
	room_vertex out[6 + ROOM_BOARD_VERTS];
	CHECK(room_poster_mesh(&pw, 192, 256, out) == 6, "poster quad");
	for (int i = 0; i < 6; i++) CHECK(fabsf(out[i].x - (pw.plane[0] + 2)) <= 1, "the poster's picture 2 mm off its backing");
	int nv = room_board_mesh(&bw, &b, out, ROOM_BOARD_VERTS);
	CHECK(nv == b.n * 18, "18 vertices a card (%d)", nv);
	float smin = 1e9f, smax = -1e9f, ymin = 1e9f, ymax = -1e9f;
	for (int i = 0; i < nv; i++) {
		float s = (out[i].z - bw.plane[2]) / (bw.plane[5] * bw.plane[3]) + .5f;
		float y = out[i].y;
		smin = fminf(smin, s); smax = fmaxf(smax, s); ymin = fminf(ymin, y); ymax = fmaxf(ymax, y);
		CHECK(abs(out[i].g - (int)lroundf(128 * bw.light[1])) <= 1 || abs(out[i].g - (int)lroundf(160 * bw.light[1])) <= 1 || out[i].g == 255,
			"card vertex %d not lit by the wall (%d)", i, out[i].g);
	}
	CHECK(smin >= -.01f && smax <= 1.01f, "cards along the board %.3f..%.3f", smin, smax);
	CHECK(ymin >= bw.plane[1] - bw.plane[4] / 2 - 2 && ymax <= bw.plane[1] + bw.plane[4] / 2 + 2, "cards inside the board's height");
	// relighting (review m3): unlit x light gives the directly lit mesh, and Night -> Day -> Night (any number of style
	// changes, lights over 1.59 that clamp a card's 160 shade) ends exactly where it started
	{
		static room_vertex unlit[ROOM_BOARD_VERTS], lit[ROOM_BOARD_VERTS], direct[ROOM_BOARD_VERTS];
		room_wall one = bw, bright = bw;
		one.light[0] = one.light[1] = one.light[2] = 1;
		one.unit = bright.unit = 128;   // the PS1's unit: relit = directly lit
		bright.light[0] = 1.9f; bright.light[1] = 1.7f; bright.light[2] = .4f;
		int n1 = room_board_mesh(&one, &b, unlit, ROOM_BOARD_VERTS), n2 = room_board_mesh(&bright, &b, direct, ROOM_BOARD_VERTS);
		CHECK(n1 == n2 && n1 > 0, "meshes %d %d", n1, n2);
		for (int round = 0; round < 3; round++) {
			room_walls_relight(unlit, lit, n1, 0, &one, &bw);
			room_walls_relight(unlit, lit, n1, 0, &one, &bright);
		}
		CHECK(!memcmp(lit, direct, n1 * sizeof(room_vertex)), "relit walls drift from the directly lit mesh");
		// V3 fix round: Day's light range is 4 (255 / 4 = 1.0 in vertex bytes): the relit cards carry the wall's
		// light in that unit, as the room's own vertices do (they were 128 = 1.0: twice as bright in Day)
		CHECK(fabsf(bw.unit - 63.75f) < .01f, "day board unit %.2f", bw.unit);
		room_walls_relight(unlit, lit, n1, 0, &one, &bw);
		int checked = 0;
		for (int i = 0; i < n1; i++)
			for (int c = 0; c < 3; c++) {
				uint8_t u = (&unlit[i].r)[c], o = (&lit[i].r)[c];
				float want = u * bw.light[c] * 63.75f / 128;
				if (want < 250) { checked++; CHECK(fabsf(o - want) <= 1, "day card vertex %d channel %d: %u, want %.1f", i, c, o, want); }
			}
		CHECK(checked > n1, "only %d day channels below the clamp", checked);
		room_wall nb;
		CHECK(room_style_use(&f, 1) && room_wall_of(&f, "board", &nb) && nb.unit == 128, "night board unit %.2f", nb.unit);
		room_style_use(&f, 0);
	}
	// the oldest card is drawn first (its shadow first of all), the newest last
	room_card_place first, last;
	room_board_place(&bw, &b.card[b.n - 1], &first);
	room_board_place(&bw, &b.card[0], &last);
	room_vertex q;
	room_wall_point(&bw, last.s, last.t, 3, &q);
	float mz = 0, my = 0;
	for (int i = nv - 12; i < nv - 6; i++) { mz += out[i].z / 6.0f; my += out[i].y / 6.0f; }
	CHECK(fabsf(mz - q.z) < 2 && fabsf(my - q.y) < 2, "the newest card last (%.0f %.0f vs %d %d)", mz, my, q.z, q.y);
	// no two places of a full board share their middle
	for (int i = 0; i < b.n; i++)
		for (int j = i + 1; j < b.n; j++) {
			room_card_place a, e;
			room_board_place(&bw, &b.card[i], &a);
			room_board_place(&bw, &b.card[j], &e);
			CHECK(fabsf(a.s - e.s) * bw.plane[3] > 60 || fabsf(a.t - e.t) * bw.plane[4] > 60, "cards %d and %d on top of each other", i, j);
		}
	free(data);
	printf(fails ? "room walls: %d FAILED\n" : "room walls: all passed\n", fails);
	return fails != 0;
}
