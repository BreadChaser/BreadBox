// The box in the hand turns round and round (owner 2026-11-12: "when you spin the game disc around it should loop
// around"): shelf_input.h's sides for L / R / Front press sequences, and the case's yaw drawn every 60 Hz frame
// from hands_for_shelf + hands_case_turn (as room_hands_draw applies it) with the shipped clips, both files
// (case-motion.bbh, the default; hands.bbh with the hands), PS1 cases (jewel, PAL, multi-disc) and the GBA carton:
// - R: front, spine, back, edge, front; L the other way; Front from each side by the shortest turn;
// - every frame of a turn moves the case the way pressed (never back), with no jump, including across the wrap;
// - each side rests at its own yaw (the edge: the spine's half round), four R or four L make one whole turn.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#define BIT(n) (1u << (n))
#define KEY_RIGHT BIT(4)
#define KEY_LEFT BIT(5)
#define KEY_UP BIT(6)
#define KEY_DOWN BIT(7)
#define KEY_R BIT(8)
#define KEY_L BIT(9)
#define KEY_CPAD_RIGHT BIT(28)
#define KEY_CPAD_LEFT BIT(29)
#define KEY_CPAD_UP BIT(30)
#define KEY_CPAD_DOWN BIT(31)
#include "shelf_input.h"
#include "hands.h"
#include "ui_style.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static const float PI = 3.14159265f, DEG = 3.14159265f / 180;

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
static float wrap(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }

// the shelf's turn state (shelf.c turn(): one turn at a time)
static int side, from, steps;
static float t0 = -10, now;
static void turn(int n) { if (!n) return; from = side; side = shelf_turn_side(side, n); steps = n; t0 = now; }

// the case's yaw as drawn now
static float drawn(const hands_file *f, int box)
{
	hands_shelf_state s = { .t = now, .pull = 1, .pull_to = 1, .pull_t0 = -20, .pull_seconds = 1, .play_t0 = -1,
		.side = side, .turn_from = from, .turn_steps = steps, .turn_t0 = t0 };
	hands_shelf_clip c = hands_for_shelf(f, box, false, &s);
	if (!c.clip) return NAN;
	hands_frame fr;
	hands_sample(f, c.clip, c.t, &fr);
	if (c.yaw_mode != HANDS_YAW_NONE) hands_case_turn(&fr, c.yaw_mode == HANDS_YAW_SET ? c.yaw - hands_case_yaw(&fr) : c.yaw);
	return hands_case_yaw(&fr);
}

// press, then follow the turn frame by frame; returns the unwrapped yaw change
static float press(const hands_file *f, int box, int n, const char *what, float *rest)
{
	float y0 = drawn(f, box), y = y0, total = 0;
	turn(n);
	int dir = steps > 0 ? -1 : 1;   // R turns the case's yaw down, L up
	for (int i = 1; i <= (int)(UI_MOTION_TURN_S * 60) + 6; i++) {
		now += 1 / 60.0f;
		float y1 = drawn(f, box), d = wrap(y1 - y);
		CHECK(fabsf(d) < 40 * DEG, "%s box %d: a jump of %.1f deg in frame %d", what, box, d / DEG, i);
		CHECK(d * dir > -1.5f * DEG, "%s box %d: frame %d turns back %.1f deg", what, box, i, d / DEG);
		total += d; y = y1;
	}
	now += 1;   // resting
	float y1 = drawn(f, box);
	CHECK(fabsf(wrap(y1 - y)) < 3 * DEG, "%s box %d: moves %.1f deg after the turn", what, box, wrap(y1 - y) / DEG);
	CHECK(fabsf(total) > 60 * DEG * abs(steps) && fabsf(total) < 120 * DEG * abs(steps), "%s box %d: turned %.0f deg for %d quarters", what, box, total / DEG, steps);
	if (rest) *rest = y1;
	(void)y0;
	return total;
}

static void run(const hands_file *f, int box, const char *file)
{
	side = from = steps = 0; t0 = -10; now = 0;
	float rest[4], r;
	rest[0] = drawn(f, box);
	// R all the way round: spine, back, edge, front
	float total = 0;
	for (int i = 1; i <= 4; i++) { total += press(f, box, shelf_turn_steps(KEY_R), "R", &r); CHECK(side == i % 4, "R %d: side %d", i, side); if (i < 4) rest[i] = r; else CHECK(fabsf(wrap(r - rest[0])) < 3 * DEG, "R x4 is not the front again"); }
	CHECK(fabsf(total + 2 * PI) < 10 * DEG, "%s box %d: four R turned %.0f deg, not one whole turn", file, box, total / DEG);
	CHECK(fabsf(wrap(rest[3] - rest[1] - PI)) < 3 * DEG, "%s box %d: the edge is not the spine half round (%.0f vs %.0f)", file, box, rest[3] / DEG, rest[1] / DEG);
	// L all the way round the other way: edge, back, spine, front
	total = 0;
	for (int i = 1; i <= 4; i++) { total += press(f, box, shelf_turn_steps(KEY_L), "L", &r); CHECK(side == (4 - i) % 4, "L %d: side %d", i, side); CHECK(fabsf(wrap(r - rest[side])) < 3 * DEG, "L %d: side %d at %.0f, R left it at %.0f", i, side, r / DEG, rest[side] / DEG); }
	CHECK(fabsf(total - 2 * PI) < 10 * DEG, "%s box %d: four L turned %.0f deg", file, box, total / DEG);
	// back and forth across the wrap: L (edge), R (front), R R (back), L (spine)
	press(f, box, -1, "L wrap", NULL); CHECK(side == 3, "L from the front: the edge");
	press(f, box, 1, "R wrap", NULL); CHECK(side == 0, "R from the edge: the front");
	// Front: the shortest turn from each side
	for (int s = 1; s < 4; s++) {
		while (side != s) press(f, box, 1, "R to", NULL);
		total = press(f, box, shelf_turn_home(side), "Front", &r);
		CHECK(side == 0 && fabsf(wrap(r - rest[0])) < 3 * DEG, "Front from %d: side %d", s, side);
		CHECK(fabsf(total) < (s == 2 ? 200 : 110) * DEG, "Front from %d turned %.0f deg, not the short way", s, total / DEG);
	}
}

int main(void)
{
	// the input alone: sides and steps for press sequences
	int sd = 0;
	static const struct { unsigned keys; int side, steps; } seq[] = {
		{ KEY_R, 1, 1 }, { KEY_R, 2, 1 }, { KEY_R, 3, 1 }, { KEY_R, 0, 1 }, { KEY_L, 3, -1 }, { KEY_L, 2, -1 },
		{ KEY_R, 3, 1 }, { KEY_L | KEY_R, 3, 0 }, { 0, 3, 0 }, { KEY_L, 2, -1 }, { KEY_L, 1, -1 }, { KEY_L, 0, -1 }, { KEY_L, 3, -1 } };
	for (unsigned i = 0; i < sizeof(seq) / sizeof(*seq); i++) {
		int n = shelf_turn_steps(seq[i].keys);
		sd = shelf_turn_side(sd, n);
		CHECK(n == seq[i].steps && sd == seq[i].side, "press %u: side %d steps %d", i, sd, n);
	}
	CHECK(shelf_turn_home(0) == 0 && shelf_turn_home(1) == -1 && shelf_turn_home(2) == -2 && shelf_turn_home(3) == 1, "Front: the shortest turns");
	for (int s = 0; s < 4; s++) CHECK(shelf_turn_side(s, shelf_turn_home(s)) == 0, "Front from %d lands on the front", s);
	CHECK(shelf_turn_side(1, 2) == 3 && shelf_turn_side(0, -2) == 2 && shelf_turn_side(3, 2) == 1, "two quarters at once wrap");

	static const char *const files[] = { "assets/romfs/room/case-motion.bbh", "assets/romfs/room/hands.bbh" };
	static const int boxes[] = { HANDS_BOX_CASE, HANDS_BOX_CASE_PAL, HANDS_BOX_CASE_MULTI, HANDS_BOX_CARTON };
	for (int i = 0; i < 2; i++) {
		size_t size;
		void *data = slurp(files[i], &size);
		hands_file f;
		char err[64];
		if (!data || !hands_parse(&f, data, size, err, sizeof(err))) { printf("FAIL: %s\n", files[i]); return 1; }
		for (unsigned b = 0; b < sizeof(boxes) / sizeof(*boxes); b++) run(&f, boxes[b], files[i]);
		free(data);
	}
	if (fails) return 1;
	printf("Turn loop PASS: R and L round and round (front, spine, back, edge), Front the shortest way; every frame the way pressed, no jump; both files, 4 box kinds\n");
	return 0;
}
