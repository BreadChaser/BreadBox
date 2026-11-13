// Host checks for the room main menu's portable half (app/ui/room_bbr.c)
// against the shipped room file (BBR v3): tables, room styles, the loader's refusals,
// draw ranges, navigation, the camera's projection and stereo, the glides, and the
// bookcase's far view.
#include "room.h"
#include "ui_motion.h"
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

// screen position (400x240, y down) of a world point; returns the NDC depth
static float project(const float m[16], const float q[3], float *sx, float *sy)
{
	float c[4];
	for (int r = 0; r < 4; r++) c[r] = m[r * 4] * q[0] + m[r * 4 + 1] * q[1] + m[r * 4 + 2] * q[2] + m[r * 4 + 3];
	// untilt: clip x = screen up, clip y = -screen right
	*sx = 200 + 200 * (-c[1] / c[3]);
	*sy = 120 - 120 * (c[0] / c[3]);
	return c[2] / c[3];
}

// light round 3: the `post` anchor's y is the share of empty cubbies with decor (V3: every other one)
static void check_decor_share(const room_file *f, const room_case_plan *plan)
{
	const float *post = room_anchor(f, "post");
	CHECK(post && fabsf(post[0] - .5f) < 1e-6f && fabsf(post[1] - .5f) < 1e-6f, "post anchor (dither .5, decor .5)");
	int with = 0;
	for (int i = 3; i < 24; i++) {
		with += plan->decor[i] >= 0;
		CHECK((plan->decor[i] >= 0) == ((i - 3) % 2 == 1), "cubby %d: decor %d (every other empty cubby, from the second)", i, plan->decor[i]);
	}
	CHECK(with == 10, "%d of 21 empty cubbies have decor", with);
}

// light round 3's 1 px light line: the wall view looks down a little, so its lower rows see wider; the
// end uprights' outer faces (c[0] - c[7] / 2 and the right one) must stay outside the screen on every row
// the case's front covers, at every height the view takes and in both eyes (else the room beside it shows)
static void check_wall_view_edges(const room_file *f, const float *c)
{
	for (int owned = 0; owned <= 24; owned += 3)
		for (float hy = c[9]; hy <= c[9] + c[4] * c[3]; hy += c[3] / 2) {
			room_pose wp = room_wall_pose(f, owned, hy);
			float xo[2] = { c[0] - c[7] / 2, c[0] + c[1] * c[2] + c[7] / 2 }, worst[2] = { -1e9f, 1e9f };
			for (int e = -1; e <= 1; e++) {
				float m[16];
				room_camera(&wp, e * wp.eye / 2, m, NULL, NULL, NULL);
				for (float y = c[9] - c[8]; y <= c[9] + c[4] * c[3]; y += 2)
					for (int side = 0; side < 2; side++) {
						float q[3] = { xo[side], y, 0 }, sx, sy;
						project(m, q, &sx, &sy);
						if (sy < 0 || sy > 240) continue;
						if (side == 0) worst[0] = fmaxf(worst[0], sx); else worst[1] = fminf(worst[1], sx);
					}
			}
			CHECK(worst[0] <= 0 && worst[1] >= 400, "wall view (%d owned, hy %.0f): the case's frame spans %.2f..%.2f px, not the whole screen",
				owned, hy, worst[0], worst[1]);
		}
}

static void check_half_dither(const room_file *f)
{
	// light round 3: the room file's `post` x sets the strength; V3 halves it (+-2 around +4, not +-4)
	const float *post = room_anchor(f, "post");
	room_dither_set(post ? post[0] : 1);
	uint32_t pat[64];
	room_dither_texture(pat);
	int lo = 255, hi = 0;
	for (int i = 0; i < 64; i++) { int r = (int)(pat[i] >> 24); lo = r < lo ? r : lo; hi = r > hi ? r : hi; }
	CHECK(lo == 2 && hi == 6, "half-strength dither spans %d..%d (2..6)", lo, hi);
	CHECK(room_dither_channel(0, 0, 0, 100) == ((100 + 2) & 0xF8) && room_dither_channel(3, 0, 0, 100) == ((100 + 5) & 0xF8),
		"the pass and the pattern agree at half strength");
	room_dither_set(1);
}

// the loader refuses what it must not read: cut files (header, tables, mesh, the styles'
// colours and probes), a wrong magic or version, a v3 file without styles, a style whose
// colours, probes or atlas are outside the file, the old lightmap formats
// V3 fix round: the light unit belongs to its room file (another file's style does not change it), and the real
// boxes on a shelf get the cubby's light in that unit (they were lit at 128 = 1.0 twice: half the light in Day)
static void check_unit_and_box_light(room_file *pf, const void *data, size_t size)
{
	char err[64];
	room_file other;
	unsigned cur = pf->cur;
	CHECK(room_parse(&other, data, size, err, sizeof(err)) && room_style_use(&other, 1), "a second room file: %s", err);
	CHECK(fabsf(room_light_unit(pf) - (pf->lrange > 2 ? 63.75f : 128)) < .01f && room_light_unit(&other) == 128,
		"units: this file %.2f (style %u), the other's night %.2f", room_light_unit(pf), cur, room_light_unit(&other));
	for (int st = 0; st < 2; st++) {
		room_style_use(pf, st);
		for (int i = 0; i < 24; i += 5) {
			room_case_geo g;
			uint8_t want[3], got[3];
			float l[3];
			room_case_section(pf, i, &g);
			room_cubby_light(pf, &g, 1, want);
			room_cubby_light_f(pf, &g, l);
			room_lit_rgb(pf, 0xffffffffu, 1, l, got);
			for (int c = 0; c < 3; c++)
				if (want[c] < 255) CHECK(abs(got[c] - want[c]) <= 1, "style %d cubby %d channel %d: a white box %u, the cubby %u", st, i, c, got[c], want[c]);
		}
	}
	room_style_use(pf, (int)cur);
}

static void check_refusals(room_file *pf, const void *data, size_t size)
{
	char err[64];
	CHECK(pf->head->version == 3 && pf->stride == 16 && pf->nstyle >= 2, "v3 file: version %u stride %u styles %u", pf->head->version, pf->stride, pf->nstyle);
	CHECK(!room_parse(&(room_file){0}, data, 16, err, sizeof(err)), "a cut header parsed");
	CHECK(!room_parse(&(room_file){0}, data, 200, err, sizeof(err)), "cut tables parsed");
	CHECK(!room_parse(&(room_file){0}, data, size / 2, err, sizeof(err)), "a cut file parsed");
	CHECK(!room_parse(&(room_file){0}, data, pf->style[pf->nstyle - 1].colours + 8, err, sizeof(err)), "cut colour streams parsed");
	CHECK(!room_parse(&(room_file){0}, NULL, size, err, sizeof(err)), "NULL parsed");
	char *bad = malloc(size + 16);
	char *bad4 = (char *)(((uintptr_t)bad + 3) & ~(uintptr_t)3);
	room_bbr_header *bh = (room_bbr_header *)bad4;
	size_t style_at = (const uint8_t *)pf->style - (const uint8_t *)data, tex_at = (const uint8_t *)pf->tex - (const uint8_t *)data;
	room_bbr_style *bs = (room_bbr_style *)(bad4 + style_at);
	room_bbr_tex *bt = (room_bbr_tex *)(bad4 + tex_at);
	struct { const char *what; int k; } cases[] = { { "a wrong magic", 0 }, { "version 4", 1 }, { "no styles", 2 }, { "colours outside", 3 },
		{ "probes outside", 4 }, { "an atlas index outside", 5 }, { "a runtime atlas", 6 }, { "an ETC1 texture (the old lightmap)", 7 }, { "stride 20 in v3", 8 },
		{ "two styles with one id", 9 }, { "an atlas not a power of two", 10 }, { "light range 3", 11 } };
	for (unsigned c = 0; c < sizeof(cases) / sizeof(*cases); c++) {
		memcpy(bad4, data, size);
		switch (cases[c].k) {
		case 0: bad4[3] = 'X'; break;
		case 1: bh->version = 4; break;
		case 2: bh->flags = 0; break;
		case 3: bs[1].colours = (uint32_t)size - 4; break;
		case 4: bs[0].probes = (uint32_t)size + 64; break;
		case 5: bs[0].atlas = bh->ntex; break;
		case 6: bs[0].atlas = (uint16_t)(room_tex(pf, "tv") - pf->tex); break;
		case 7: bt[0].fmt = 4; break;   // the old ETC1 lightmap's format: unknown now
		case 8: bh->stride = 20; break;
		case 9: memcpy(bs[1].id, bs[0].id, sizeof(bs[0].id)); break;
		case 10: bt[bs[0].atlas].w = 192; bt[bs[0].atlas].size = 192u * 128 * 2; break;
		case 11: bs[0].range = 3; break;
		}
		CHECK(!room_parse(&(room_file){0}, bad4, size, err, sizeof(err)), "%s parsed", cases[c].what);
	}
	free(bad);
}

// room styles: Day first (the default), Night second; each its own colours, probes and atlas
static void check_styles(room_file *pf, const void *data, size_t size)
{
	CHECK(room_style_find(pf, "day") == 0 && room_style_find(pf, "night") == 1 && room_style_find(pf, "rustic") < 0 && room_style_find(pf, NULL) < 0,
		"style ids");
	CHECK(pf->cur == 0 && pf->nprobe >= 24 && pf->probe, "style 0 in use after parse: %u probes", pf->nprobe);
	const uint8_t *day = room_style_colours(pf, 0), *night = room_style_colours(pf, 1);
	CHECK(day && night && day != night && !room_style_colours(pf, 2), "colour streams");
	{   // review N4: once the app drops the vertices (room_scene.c keep_tables) there are no colour streams to give
		room_file kept = *pf;
		kept.vdata = NULL;
		CHECK(!room_style_colours(&kept, 0) && !room_style_colours(&kept, 1), "colour streams after the vertices were dropped");
	}
	{
		room_vertex v0 = room_vertex_at(pf, 0);
		CHECK(day && !memcmp(&v0.r, day, 4), "the vertices carry the first style's colours");
		unsigned differ = 0, dsum = 0, nsum = 0;
		for (unsigned i = 0; day && night && i < pf->head->nvert; i++) {
			differ += memcmp(day + i * 4, night + i * 4, 3) != 0;
			dsum += day[i * 4] + day[i * 4 + 1] + day[i * 4 + 2];
			nsum += night[i * 4] + night[i * 4 + 1] + night[i * 4 + 2];
		}
		CHECK(differ > pf->head->nvert / 2 && nsum < dsum, "night is not its own (darker) light: %u differ", differ);
		room_case_geo g;
		uint8_t ld[3], ln[3];
		room_case_section(pf, 10, &g);
		room_cubby_light(pf, &g, 1, ld);
		CHECK(room_style_use(pf, 1) && pf->cur == 1, "switching to night");
		room_cubby_light(pf, &g, 1, ln);
		CHECK(ld[0] + ld[1] + ld[2] > 60 && memcmp(ld, ln, 3), "cubby light day %u,%u,%u night %u,%u,%u", ld[0], ld[1], ld[2], ln[0], ln[1], ln[2]);
		CHECK(pf->style[1].atlas != pf->style[0].atlas, "night has no atlas of its own");
		// light round 3: Day's light reaches 4.0 (255 / 4 = 1.0), Night keeps the PS1's 2.0 (128 = 1.0); probes,
		// the case's boxes and the wall pictures take the same unit
		CHECK(pf->style[0].range == 4 && (pf->style[1].range == 0 || pf->style[1].range == 2) && !pf->style[0].pad && !pf->style[1].pad,
			"style light ranges %u %u (day 4, night 2)", pf->style[0].range, pf->style[1].range);
		CHECK(pf->lrange == 2 && fabsf(room_light_unit(pf) - 128) < .01f, "night: range %.1f unit %.2f", pf->lrange, room_light_unit(pf));
		room_style_use(pf, 0);
		CHECK(pf->lrange == 4 && fabsf(room_light_unit(pf) - 63.75f) < .01f, "day: range %.1f unit %.2f", pf->lrange, room_light_unit(pf));
		check_unit_and_box_light(pf, data, size);
		room_style_use(pf, 1);
		CHECK(!room_style_use(pf, 7) && pf->cur == 1, "a style outside the table was taken");
		room_style_use(pf, 0);
	}
}

// the draw plan: room parts are one contiguous range (draw 1), then the TV
// picture, shadows and the sprites; no hand parts in the app's file
static void check_draw_plan(room_file *pf)
{
	const char *order[] = { "static", "p_tv", "p_lib", "p_set", "p_tog", "p_share", "a_card" };
	unsigned end = 0;
	for (unsigned i = 0; i < sizeof(order) / sizeof(*order); i++) {
		const room_bbr_part *p = room_part(pf, order[i]);
		CHECK(p, "part %s missing", order[i]);
		if (!p) continue;
		CHECK(p->first == end, "part %s not contiguous (%u, expected %u)", order[i], p->first, end);
		CHECK(p->mode == ROOM_MODE_OPAQUE && p->tex == 0, "part %s not an opaque atlas part", order[i]);
		end = p->first + p->count;
	}
	const room_bbr_part *tv = room_part(pf, "tv_screen"), *glow = room_part(pf, "glow"), *steam = room_part(pf, "steam");
	CHECK(tv && glow && steam, "tv_screen/glow/steam missing");
	CHECK(!room_part(pf, "hand_palm") && !room_part(pf, "shadow") && !room_part(pf, "rays"), "hand, shadow or ray parts in the room file");
	CHECK(!room_tex(pf, "light"), "a lightmap in the room file");
	if (tv) CHECK(pf->tex[tv->tex].fmt == ROOM_TEX_RUNTIME, "tv picture is not a runtime texture");
	if (glow && steam) CHECK(glow->mode == ROOM_MODE_ADD && steam->mode == ROOM_MODE_ADD, "sprite modes");
	for (unsigned i = 0; i < pf->nstyle; i++) {
		const room_bbr_tex *atlas = &pf->tex[pf->style[i].atlas];
		CHECK(atlas->w == 256 && atlas->h == 128 && atlas->fmt == ROOM_TEX_RGBA5551, "style %u atlas %ux%u (256x128 RGBA5551)", i, atlas->w, atlas->h);
	}
	// palette slots and sprite corners stay in range
	for (unsigned i = 0; i < end; i++) {
		room_vertex vv = room_vertex_at(pf, pf->idx[i]), *v = &vv;
		if (v->w < 0 || v->w > 11) { CHECK(0, "room vertex slot %d", v->w); break; }
	}
	for (unsigned i = glow ? glow->first : 0; glow && i < glow->first + glow->count; i++) {
		room_vertex vv = room_vertex_at(pf, pf->idx[i]), *v = &vv;
		if (v->w <= 0 || (v->a & 3) > 3) { CHECK(0, "glow sprite size %d", v->w); break; }
	}
}

// navigation: every place reachable from every other place
static void check_navigation(void)
{
	for (int a = 0; a < ROOM_PLACES; a++) {
		int seen = 1 << a, grew = 1;
		while (grew) {
			grew = 0;
			for (int p = 0; p < ROOM_PLACES; p++)
				if (seen & (1 << p))
					for (int d = 0; d < 4; d++) {
						int q = room_nav(p, d);
						if (q >= 0 && !(seen & (1 << q))) { seen |= 1 << q; grew = 1; }
					}
		}
		CHECK(seen == (1 << ROOM_PLACES) - 1, "not every place reachable from %s", room_place_id[a]);
		CHECK(room_cycle(room_cycle(a, 1), -1) == a, "L/R do not undo each other");
	}
	CHECK(room_nav(ROOM_CONTINUE, ROOM_RIGHT) == ROOM_LIBRARY && room_nav(ROOM_LIBRARY, ROOM_DOWN) == ROOM_SHARE, "mock map");
}

// camera: every pose looks at its point (screen centre), inside the depth range,
// and the convergence plane has no parallax between the eyes
static void check_cameras(room_file *pf)
{
	for (int p = -1; p < ROOM_PLACES; p++) {
		room_pose pose;
		const char *id = p < 0 ? "home" : room_place_id[p];
		CHECK(room_pose_of(pf, id, &pose), "pose %s missing", id);
		float m[16], sx, sy;
		room_camera(&pose, 0, m, NULL, NULL, NULL);
		float z = project(m, pose.look, &sx, &sy);
		CHECK(fabsf(sx - 200) < .5f && fabsf(sy - 120) < .5f, "%s: look-at at %.1f,%.1f", id, sx, sy);
		CHECK(z > -1 && z < 0, "%s: depth %.3f outside -1..0", id, z);
		// from home, every place's tag point is in the picture
		for (int k = 0; p < 0 && k < ROOM_PLACES; k++) {
			char fname[24];
			snprintf(fname, sizeof(fname), "focus.%s", room_place_id[k]);
			const float *fo = room_anchor(pf, fname);
			CHECK(fo, "%s missing", fname);
			if (!fo) continue;
			project(m, fo, &sx, &sy);
			CHECK(sx > 0 && sx < 400 && sy > 0 && sy < 240, "home: %s off screen (%.0f,%.0f)", fname, sx, sy);
		}
		// a point on the view axis at the convergence distance
		float d[3] = { pose.look[0] - pose.pos[0], pose.look[1] - pose.pos[1], pose.look[2] - pose.pos[2] };
		float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), q[3];
		for (int i = 0; i < 3; i++) q[i] = pose.pos[i] + d[i] / l * pose.conv;
		float lx, rx, ly, ry, near[3];
		room_camera(&pose, -pose.eye / 2, m, NULL, NULL, NULL);
		project(m, q, &lx, &ly);
		room_camera(&pose, pose.eye / 2, m, NULL, NULL, NULL);
		project(m, q, &rx, &ry);
		CHECK(fabsf(lx - rx) < .05f, "%s: parallax %.3f px at the convergence distance", id, rx - lx);
		// nearer than that pops out: the right eye sees it further left
		for (int i = 0; i < 3; i++) near[i] = pose.pos[i] + d[i] / l * pose.conv * .5f;
		room_camera(&pose, -pose.eye / 2, m, NULL, NULL, NULL);
		project(m, near, &lx, &ly);
		room_camera(&pose, pose.eye / 2, m, NULL, NULL, NULL);
		project(m, near, &rx, &ry);
		CHECK(rx < lx, "%s: near point has no crossed parallax", id);
	}
	// the bookshelf's camera: the look-at point on a chosen screen row
	{
		room_pose pose = { { 300, 100, -400 }, { 300, 100, 0 }, 600, 0, 404 };
		float m[16], sx, sy;
		room_camera_ex(&pose, 0, 107, m, NULL, NULL, NULL);
		project(m, pose.look, &sx, &sy);
		CHECK(fabsf(sx - 200) < .5f && fabsf(sy - 107) < .5f, "row 107: look-at at %.1f,%.1f", sx, sy);
	}
	// nearer is a larger depth value (citro2d's GEQUAL test keeps the nearest)
	{
		room_pose pose;
		room_pose_of(pf, "home", &pose);
		float m[16], sx, sy, a[3], b[3];
		room_camera(&pose, 0, m, NULL, NULL, NULL);
		for (int i = 0; i < 3; i++) { a[i] = pose.pos[i] + (pose.look[i] - pose.pos[i]) * .3f; b[i] = pose.pos[i] + (pose.look[i] - pose.pos[i]) * 1.5f; }
		CHECK(project(m, a, &sx, &sy) < project(m, b, &sx, &sy), "depth order");
	}
}

// Motion is independent of display cadence, including a delayed frame.
static void check_motion(room_file *pf)
{
	for (int hz = 30; hz <= 60; hz += 30) {
		float x = 0, vel = 0;
		for (int i = 0; i < hz; i++) {
			float old = x;
			ui_motion_settle(&x, &vel, 1, UI_MOTION_SETTLE_RATE, 1.0f / hz);
			CHECK(x >= old && x <= 1.00001f, "motion overshot at %d Hz", hz);
		}
		CHECK(fabsf(x - 1) < .001f, "motion did not settle at %d Hz", hz);
		ui_motion_settle(&x, &vel, 0, UI_MOTION_SETTLE_RATE, .5f);
		CHECK(isfinite(x) && x >= 0 && x < .01f, "delayed frame made motion unstable");
	}
	// The room's camera stays inside its endpoint bounds: no vertical arc.
	// glides: A glides to the place in at most 700 ms, B glides back home; the home view settles
	room_view v;
	room_view_init(&v, pf, ROOM_CONTINUE, -1);
	for (int i = 0; i < 120; i++) room_view_step(&v, 1 / 60.0f);
	room_pose home = room_view_pose(&v);
	room_view_go(&v, ROOM_SETTINGS);
	room_pose start = room_view_pose(&v);
	CHECK(fabsf(start.pos[0] - home.pos[0]) < 1 && fabsf(start.pos[2] - home.pos[2]) < 1, "glide does not start where the camera is");
	room_pose destination;
	room_pose_of(pf, "settings", &destination);
	for (int i = 0; i <= 20; i++) {
		room_view sample = v;
		sample.t = sample.t0 + ROOM_GLIDE * i / 20.0f;
		room_pose q = room_view_pose(&sample);
		for (int axis = 0; axis < 3; axis++)
			CHECK(q.pos[axis] >= fminf(home.pos[axis], destination.pos[axis]) - .01f &&
			      q.pos[axis] <= fmaxf(home.pos[axis], destination.pos[axis]) + .01f, "camera left its travel bounds");
	}
	int frames = 0;
	while (!room_view_arrived(&v) && frames < 200) { room_view_step(&v, 1 / 60.0f); frames++; }
	CHECK(frames >= 39 && frames <= 40, "glide took %d frames", frames);
	room_pose at = room_view_pose(&v), want;
	room_pose_of(pf, "settings", &want);
	CHECK(fabsf(at.pos[0] - want.pos[0]) < .01f && fabsf(at.look[2] - want.look[2]) < .01f, "arrived off the pose");
	CHECK(room_view_tint(&v, ROOM_SETTINGS) == 1 && room_view_tint(&v, ROOM_LIBRARY) == 1, "tint at a place");
	room_view_back(&v);
	for (int i = 0; i < 88; i++) room_view_step(&v, 1 / 60.0f);
	CHECK(v.mode == ROOM_HOME && v.focus == ROOM_SETTINGS, "back: mode %d focus %d", v.mode, v.focus);
	CHECK(room_view_tint(&v, ROOM_SETTINGS) > 1.1f, "the focused place does not glow");
	// back from the library's own screen: starts at the library pose, glides home
	room_view_init(&v, pf, ROOM_LIBRARY, ROOM_LIBRARY);
	room_pose lib = room_view_pose(&v);
	room_pose_of(pf, "library", &want);
	CHECK(fabsf(lib.pos[2] - want.pos[2]) < .01f, "return does not start at the library");
	for (int i = 0; i < 88; i++) room_view_step(&v, 1 / 60.0f);
	CHECK(v.mode == ROOM_HOME, "return glide did not end");
}

// the bookcase (owner: "one shelf per system" = one cubby per system): a cubby each, never
// shared, from the top left; boxes from the left, a bookend after them when the cubby is not
// full; the other cubbies decor (for systems to come)
static void check_case(room_file *pf)
{
	room_section s[3] = { { ROOM_SPINE_MIXED, 0, 20 }, { ROOM_SPINE_CARTON, 16, 27 }, { ROOM_SPINE_JEWEL, 12, 14 } };
	room_case_plan plan;
	room_case_plan_make(pf, s, 3, &plan);
	CHECK(plan.cubbies == 24 && plan.nsec == 3 && plan.count[0] == 1 && plan.count[1] == 1 && plan.count[2] == 1 && plan.used == 3,
		"plan %d %d %d used %d", plan.count[0], plan.count[1], plan.count[2], plan.used);
	CHECK(plan.first[0] == 0 && plan.first[1] == 1 && plan.first[2] == 2 && plan.owner[0] == 0 && plan.owner[1] == 1 && plan.owner[2] == 2 &&
		plan.owner[3] == -1 && plan.owner[23] == -1 && plan.decor[2] < 0, "plan cubbies");
	check_decor_share(pf, &plan);
	CHECK(plan.fill[0] == 0 && plan.fill[1] == 1 && plan.fill[2] > .3f && plan.fill[2] < 1 && plan.fill[3] == 0,
		"cubby fill %.2f %.2f %.2f", plan.fill[0], plan.fill[1], plan.fill[2]);
	for (int i = 6; i < 24; i++) if (plan.decor[i] >= 0) CHECK(plan.decor[i] != plan.decor[i - 2], "decor %d repeats its neighbour", i);
	int bc = -1;
	float bx = 0;
	room_case_geo gb;
	CHECK(!room_case_bookend(pf, &plan, 0, &bc, &bx) && !room_case_bookend(pf, &plan, 1, &bc, &bx) && room_case_bookend(pf, &plan, 2, &bc, &bx) &&
		bc == 2 && room_case_section(pf, 2, &gb) && fabsf(bx - fminf(gb.xin0 + gb.inner * plan.fill[2] + 4, gb.xin0 + gb.inner - 5)) < .01f,
		"bookends: right of the boxes of a cubby not full, none on a full one or an empty Favorites");
	room_section big[3] = { { ROOM_SPINE_MIXED, 4, 20 }, { ROOM_SPINE_CARTON, 315, 27 }, { ROOM_SPINE_JEWEL, 191, 14 } };
	room_case_plan full;
	room_case_plan_make(pf, big, 3, &full);
	CHECK(full.used == 3 && full.count[1] == 1 && full.count[2] == 1 && full.fill[1] == 1 && full.fill[2] == 1 && full.fill[0] < 1 &&
		full.owner[3] == -1 && !room_case_bookend(pf, &full, 1, &bc, &bx) && room_case_bookend(pf, &full, 0, &bc, &bx) && bc == 0,
		"a big library: still one cubby per system (its boxes slide), no bookend; %d used", full.used);
	room_section many[30];
	for (int i = 0; i < 30; i++) many[i] = (room_section){ ROOM_SPINE_JEWEL, 5, 14 };
	room_case_plan all;
	room_case_plan_make(pf, many, 30, &all);
	CHECK(all.nsec == 24 && all.used == 24 && all.count[23] == 1, "more systems than cubbies: %d", all.nsec);
	static room_vertex mesh[24 * 36];
	int first[ROOM_CUBBIES + 1], backs;
	int nv = room_case_mesh(pf, s, &plan, 2, mesh, 24 * 36, first, &backs);
	CHECK(backs == 24 * 6 && first[0] == backs && nv - first[24] == 3 * 12, "case mesh: %d backs, %d vertices, %d for the strips", backs, nv, nv - first[24]);
	CHECK(first[1] - first[0] == 12 && first[2] - first[1] == 12 && first[3] - first[2] == 18 && first[4] - first[3] == 0 && first[5] - first[4] == 6 &&
		first[24] - first[23] == 0, "cubby ranges: card + plate, boxes + plate, boxes + bookend + plate, then empty, decor, ... empty");
	// the sections and the library's cameras
	room_case_geo g0, g2;
	CHECK(room_case_sections(pf) == 24 && room_case_section(pf, 0, &g0) && room_case_section(pf, 2, &g2) && !room_case_section(pf, 24, &g0),
		"case sections");
	CHECK(g0.row == 7 && g0.bay == 0 && g2.bay == 2 && g2.xl > g0.xr - 1 && g0.top > g0.base + 100, "section geometry");
	{
		const float *cs = room_anchor(pf, "case");
		room_case_geo gb0;
		CHECK(cs && room_case_section(pf, 21, &gb0) && gb0.row == 0 && fabsf(gb0.base - cs[9]) < .01f && cs[9] > 0,
			"the bottom row stands on the plinth (base %.0f)", cs ? cs[9] : -1);
	}
	room_pose wall = room_wall_pose(pf, 3, g0.base), close = room_section_pose(pf, g2.cx, g2.base, 125, 0, 0);
	CHECK(wall.pos[2] < close.pos[2] && close.F > wall.F && fabsf(close.look[0] - g2.cx) < 1 && close.look[1] > g2.base, "library cameras");
	room_view lv;
	room_view_init(&lv, pf, ROOM_LIBRARY, ROOM_LIBRARY);
	room_view_set_library(&lv, &wall);
	room_pose l0 = room_view_pose(&lv);
	CHECK(fabsf(l0.pos[2] - wall.pos[2]) < .01f && fabsf(l0.pos[1] - wall.pos[1]) < .01f, "the way back starts at the wall view");
	const float *c = room_anchor(pf, "case");
	CHECK(c && (int)c[2] == 3 && (int)c[4] == 8, "case is not 8 rows x 3 bays");
	if (c) check_wall_view_edges(pf, c);
	if (c) {
		for (int i = 0; i < nv; i++) CHECK(mesh[i].w == 2 && mesh[i].y >= c[9] - c[8] && mesh[i].y <= c[9] + c[4] * c[3], "vertex %d off the case", i);
		// unlit = under 30 in the PS1's 128 = 1.0 (Day's range 4 stores light at 255 / 4 = 1.0)
		// (the cubbies; after them each section's switched-off strip, whose underside is dark on purpose)
		for (int i = 0; i < first[24]; i++) if ((mesh[i].r + mesh[i].g + mesh[i].b) * 128 < 30 * room_light_unit(pf)) { CHECK(0, "case vertex %d unlit", i); break; }
		// a full cubby's boxes span its inner width; a system that does not fill its cubby is cropped
		int full_w = mesh[first[1] + 1].x - mesh[first[1]].x, part_w = mesh[first[2] + 1].x - mesh[first[2]].x;
		CHECK(abs(full_w - (int)(c[1] - c[7])) <= 1, "full cubby %d mm wide", full_w);
		CHECK(abs(part_w - (12 * 16 + 2)) <= 1, "12 jewel cases %d mm", part_w);
	}
	CHECK(room_case_mesh(pf, s, &plan, 2, mesh, 10, NULL, NULL) <= 10, "case mesh overran its buffer");
	uint16_t *px = malloc(ROOM_SHELF_W * ROOM_SHELF_H * 2);
	room_shelf_texture(px);
	int clear = 0, solid = 0;
	for (int i = 0; i < 128 * 64; i++) if (px[(i / 128) * ROOM_SHELF_W + i % 128] & 1) solid++; else clear++;
	CHECK(solid > clear && clear > 1000, "spine tile coverage: %d solid, %d clear", solid, clear);
	CHECK((px[200 * ROOM_SHELF_W + 64] & 1), "plate is not opaque");
	int back = 0, decor = 0;
	for (int i = 0; i < 128 * 64; i++) back += px[(64 + i / 128) * ROOM_SHELF_W + 128 + i % 128] & 1;
	for (int i = 0; i < 128 * 64; i++) decor += px[(128 + i / 128) * ROOM_SHELF_W + 128 + i % 128] & 1;
	CHECK(back == 128 * 64 && decor > 400 && decor < 128 * 64 / 2, "back %d, decor %d texels", back, decor);
	if (getenv("ROOM_SHELF_PPM")) {   // look at it: ROOM_SHELF_PPM=out.ppm
		FILE *o = fopen(getenv("ROOM_SHELF_PPM"), "wb");
		fprintf(o, "P6 %d %d 255\n", ROOM_SHELF_W, ROOM_SHELF_H);
		for (int i = 0; i < ROOM_SHELF_W * ROOM_SHELF_H; i++) {
			uint16_t q = px[i];
			unsigned char rgb[3] = { (q & 1) ? (q >> 11) << 3 : 40, (q & 1) ? ((q >> 6) & 31) << 3 : 40, (q & 1) ? ((q >> 1) & 31) << 3 : 60 };
			fwrite(rgb, 1, 3, o);
		}
		fclose(o);
	}
	free(px);
}

// the real boxes' mesh cache (review M1): a room style change or a new load makes it again

// the bookcase lamp (owner 2026-11-12: "add a light once you click on a system"; the strip light): off it
// changes nothing; on it lights the open cubby's boxes and back clearly, more at the boxes than the back, with
// a few vertices only, and every section gets its (switched-off) strip
static void check_lamp(room_file *pf)
{
	room_section s[3] = { { ROOM_SPINE_MIXED, 0, 20 }, { ROOM_SPINE_CARTON, 2, 27 }, { ROOM_SPINE_JEWEL, 44, 14 } };
	room_case_plan plan;
	room_case_plan_make(pf, s, 3, &plan);
	static room_vertex lit[24 * 36], grid[256];
	int first[ROOM_CUBBIES + 1], backs;
	int n = room_case_mesh(pf, s, &plan, 2, lit, 24 * 36, first, &backs);
	CHECK(n - first[24] == 3 * 12, "%d strip vertices after the cubbies for 3 sections", n - first[24]);
	for (int c = 1; c <= 2; c++) {
		room_case_geo g;
		room_case_section(pf, c, &g);
		// lamp 0: the grid is the plain back (same light, the same colour on every corner)
		int m = room_lamp_mesh(pf, c, 0, 2, grid, 256);
		CHECK(m > 0 && m <= 256, "cubby %d: %d lamp vertices", c, m);
		for (int i = 0; i < m - 6; i++)
			CHECK(abs(grid[i].r - lit[c * 6].r) <= 1 && abs(grid[i].g - lit[c * 6].g) <= 1 && abs(grid[i].b - lit[c * 6].b) <= 1,
				"cubby %d: off, vertex %d differs from the plain back", c, i);
		float add[3], back[3], side[3];
		room_lamp_at(&g, 0, g.cx, g.base + 60, 5, add);
		CHECK(add[0] == 0 && add[1] == 0 && add[2] == 0, "off adds light");
		room_lamp_at(&g, 1, g.cx, g.base + 60, 5, add);           // the middle of a spine
		room_lamp_at(&g, 1, g.cx, g.base + 60, 352, back);        // the back behind it
		room_lamp_at(&g, 1, g.xin0 + 20, g.base + 60, 5, side);   // a spine at the cubby's end (a big library)
		float l = (add[0] + add[1] + add[2]) / 3, lb = (back[0] + back[1] + back[2]) / 3, ls = (side[0] + side[1] + side[2]) / 3;
		CHECK(l > .7f && l < 1.5f && lb < l && ls > .8f * l, "cubby %d: spines %.2f, back %.2f, end %.2f (even end to end)", c, l, lb, ls);
		CHECK(add[0] - add[2] < .15f * l, "cubby %d: the strip is neutral", c);
		m = room_lamp_mesh(pf, c, 1, 2, grid, 256);
		int brighter = 0;
		for (int i = 0; i < m - 6; i++) brighter += grid[i].r + grid[i].g + grid[i].b > lit[c * 6].r + lit[c * 6].g + lit[c * 6].b + 6;
		CHECK(brighter > (m - 6) / 2, "cubby %d: lit back %d of %d vertices brighter", c, brighter, m - 6);
		const float *cs = room_anchor(pf, "case");
		for (int i = 0; i < m; i++)
			CHECK(grid[i].x >= g.xl - 1 && grid[i].x <= g.xr + 1 && grid[i].y >= g.base - 30 && grid[i].y <= g.top + 30 && grid[i].z <= cs[6],
				"lamp vertex %d off its cubby", i);
	}
	float t[3];
	room_lamp_case_tint(1, t);
	CHECK(t[0] > 1 && t[0] <= 1.12f && t[0] == t[2], "case tint %.2f %.2f %.2f: a little, neutral (the wood must not turn hot)", t[0], t[1], t[2]);
	room_lamp_case_tint(0, t);
	CHECK(t[0] == 1 && t[1] == 1 && t[2] == 1, "tint off");
	CHECK(room_lamp_mesh(pf, 1, 1, 2, grid, 12) <= 12, "lamp mesh overran its buffer");
}

// "why are the PlayStation discs green?" (owner 2026-11-13): a black case's plastic must survive the
// RGB565 spine atlas as a neutral grey, and the boxes' light (room, lamp, Day and Night) must keep it
// neutral: max - min <= 3 at 8 bits on the lit spine; a printed carton keeps its hue
static void check_box_colour(room_file *pf)
{
	static const unsigned char mid[4] = ROOM_JEWEL_MID, side[4] = ROOM_JEWEL_SIDE;
	unsigned char greys[10];
	for (int i = 0; i < 4; i++) { greys[i] = mid[i]; greys[4 + i] = side[i]; }
	greys[8] = ROOM_JEWEL_RAIL; greys[9] = ROOM_JEWEL_EDGE;
	for (int i = 0; i < 10; i++) {
		int t = greys[i], r5 = t >> 3, g6 = t >> 2;
		int r = r5 << 3 | r5 >> 2, g = g6 << 2 | g6 >> 4;   // as the GPU reads RGB565 back
		CHECK(t % 8 == 0 && g == t && r - g >= 0 && r - g <= 1, "jewel grey %d reads %d %d %d from RGB565", t, r, g, r);
	}
	{   // the cause: the old case's 0x0c0c0c (and its blended shading) came back from RGB565 as 8, 12, 8
		int t = 12, r5 = t >> 3, g6 = t >> 2;
		CHECK((r5 << 3 | r5 >> 2) == 8 && (g6 << 2 | g6 >> 4) == 12, "the green cast's arithmetic");
	}
	int worst = 0;
	float hue_err = 0;
	for (unsigned st = 0; st < pf->nstyle; st++) {
		room_style_use(pf, st);
		float unit = room_light_unit(pf);
		{
			for (int c = 0; c < 6; c++) {
				room_case_geo g;
				room_case_section(pf, c, &g);
				float base[3];
				room_cubby_light_f(pf, &g, base);
				for (int lamp = 0; lamp <= 1; lamp++)
					for (int h = 0; h <= 125; h += 25) {
						float add[3], l[3];
						room_lamp_at(&g, lamp, g.cx, g.base + h, 5, add);
						for (int a = 0; a < 3; a++) add[a] += base[a];
						room_box_light(add, l);
						uint8_t v[3], vc[3];
						room_lit_rgb(pf, 0xffffffff, 1, l, v);
						for (int i = 0; i < 10; i++) {
							int o[3];
							for (int a = 0; a < 3; a++) { float x = greys[i] * v[a] / unit; o[a] = x > 255 ? 255 : (int)(x + .5f); }
							int mx = o[0] > o[1] ? (o[0] > o[2] ? o[0] : o[2]) : (o[1] > o[2] ? o[1] : o[2]);
							int mn = o[0] < o[1] ? (o[0] < o[2] ? o[0] : o[2]) : (o[1] < o[2] ? o[1] : o[2]);
							if (mx - mn > worst) worst = mx - mn;
							CHECK(mx - mn <= 3, "style %u cubby %d lamp %d h %d: grey %d lit %d %d %d", st, c, lamp, h, greys[i], o[0], o[1], o[2]);
						}
						CHECK(210 * l[0] < 250, "style %u cubby %d lamp %d: box light %.2f washes a carton's 210 out", st, c, lamp, l[0]);
						room_lit_rgb(pf, 0xff3c46d2, 1, l, vc);   // a red carton (210, 70, 60)
						if (vc[0] > 60 && vc[0] < 255) {   // above the bytes' rounding, under the clamp
							float rg = (float)vc[1] / vc[0], e = fabsf(rg - 70.0f / 210) / (70.0f / 210);
							hue_err = e > hue_err ? e : hue_err;
						}
					}
			}
		}
	}
	CHECK(hue_err < .05f, "a red carton's green:red drifts %.0f %%", hue_err * 100);
	room_style_use(pf, 0);
	printf("box colour: black plastic within %d of neutral, carton hue within %.0f %%\n", worst, hue_err * 100);
}

static void check_mesh_cache(room_file *pf, const void *data, size_t size)
{
	char err[64];
	{
		room_mesh_key k = { 0 };
		CHECK(room_mesh_key_stale(&k, 1, 4, 10, true), "an empty cache is not stale");
		room_mesh_key_set(&k, 1, 4, 10);
		CHECK(!room_mesh_key_stale(&k, 1, 4, 10, true), "a cache with nothing changed is stale");
		CHECK(room_mesh_key_stale(&k, 2, 4, 10, true), "a style change (new generation) kept the old light");
		CHECK(room_mesh_key_stale(&k, 1, 5, 10, true) && room_mesh_key_stale(&k, 1, 4, 9, true) && room_mesh_key_stale(&k, 1, 4, 10, false),
			"another section, count or a moved box kept the old mesh");
	}
	// ... and its wiring (review N2): the generation is the room file's, so the loader's own calls make the boxes again
	{
		unsigned cur0 = pf->cur;
		room_mesh_key k = { 0 };
		room_mesh_key_set(&k, pf->gen, 4, 10);
		CHECK(!room_mesh_key_stale(&k, pf->gen, 4, 10, true), "nothing changed, yet the boxes are made again");
		for (unsigned i = 0; i < pf->nstyle; i++) {
			CHECK(room_style_use(pf, (int)(pf->cur + 1) % (int)pf->nstyle), "style %u", i);
			CHECK(room_mesh_key_stale(&k, pf->gen, 4, 10, true), "a room style change kept the boxes' old light (style %u)", pf->cur);
			room_mesh_key_set(&k, pf->gen, 4, 10);
		}
		room_file again;
		CHECK(room_parse(&again, data, size, err, sizeof(err)), "parse again: %s", err);
		CHECK(again.gen && again.gen != pf->gen && room_mesh_key_stale(&k, again.gen, 4, 10, true), "a new load kept the old boxes (generation %u)", again.gen);
		CHECK(room_style_use(pf, (int)cur0), "back to style %u", cur0);
	}
}

// the final pass: the PS1 dither matrix (+4) in an 8x8 RGBA8 tile, then RGB565 precision
static void check_dither(room_file *pf)
{
	{
		uint32_t pat[64];
		room_dither_texture(pat);
		int hist[8] = { 0 };
		for (int i = 0; i < 64; i++) {
			unsigned r = pat[i] >> 24, g = (pat[i] >> 16) & 255, b = (pat[i] >> 8) & 255, a = pat[i] & 255;
			CHECK(r == b && r <= 7 && g == r / 2 && a == 255, "dither texel %d: %u %u %u %u", i, r, g, b, a);
			hist[r & 7]++;
			CHECK(pat[i] == pat[(i / 8 % 4) * 8 + i % 4], "the 8x8 tile is the 4x4 pattern repeated (%d)", i);
		}
		for (int k = 0; k < 8; k++) CHECK(hist[k] == 8, "dither value %d appears %d times (8: every value twice a 4x4)", k, hist[k]);
		CHECK(room_dither_channel(0, 0, 0, 255) == 248 && room_dither_channel(1, 2, 1, 255) == 252 && room_dither_channel(3, 3, 2, 0) == 0,
			"the pass keeps white at RGB565's top and black black");
		for (int v = 0; v < 256; v += 3)
			for (int c = 0; c < 3; c++) {
				int sum = 0;
				for (int y = 0; y < 4; y++)
					for (int x = 0; x < 4; x++) {
						int o = room_dither_channel(x, y, c, (uint8_t)v);
						CHECK(o % (c == 1 ? 4 : 8) == 0 && abs(o - v) < (c == 1 ? 4 : 8), "channel %d value %d -> %d at %d,%d", c, v, o, x, y);
						sum += o;
					}
				if (v <= 240) CHECK(abs(sum - 16 * v) <= 16 * 4, "channel %d value %d: a 4x4 averages %.1f", c, v, sum / 16.0);
			}
		check_half_dither(pf);
	}
}
int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "assets/romfs/room/room.bbr";
	size_t size;
	void *data = slurp(path, &size);
	if (!data) { printf("FAIL: cannot read %s\n", path); return 1; }
	room_file f;
	char err[64];
	CHECK(room_parse(&f, data, size, err, sizeof(err)), "parse: %s", err);
	if (fails) return 1;
	check_refusals(&f, data, size);
	check_styles(&f, data, size);
	check_draw_plan(&f);
	check_navigation();
	check_cameras(&f);
	check_motion(&f);
	check_case(&f);
	check_lamp(&f);
	check_box_colour(&f);
	check_mesh_cache(&f, data, size);
	check_dither(&f);
	if (!fails)
		printf("room PASS: %u vertices, %u triangles, %d parts; navigation, poses, stereo, glides, far view\n",
			(unsigned)f.head->nvert, (unsigned)f.head->nidx / 3, f.head->npart);
	free(data);
	return fails ? 1 : 0;
}
