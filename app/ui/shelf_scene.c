/* BreadBox bookshelf artwork (room themes). Paints the approved mock
 * (UI mock js/breadbox-r8.js, variant A) with Citro2D vertex batches: every
 * surface is a world-space quad (mm) projected through one camera, so the top
 * screen is drawn once per eye for the 3D slider. The room's colours come
 * from theme_room_t; wood, plaster and brass share one grey grain atlas that
 * is generated once and cached on the SD card, and the shelf's spines are
 * baked into one texture from the cover thumbnails. Menus only: everything
 * here is released before a game starts (shelf_scene_release). */
#include "shelf_scene_internal.h"

shelf_scene_res shelf_scene_state;
bool shelf_scene_in_room(void) { return in_room(); }
C3D_Mtx shelf_scene_view_saved;   // around rotated text and artwork
shelf_scene_back shelf_scene_case_back;
const theme_room_t *shelf_scene_room;
const shelf_view *shelf_scene_view;
const u32 shelf_scene_placeholder_pal[8][2] = { { 0xff4a2a1d, 0xff3c6fe8 }, { 0xff35102b, 0xff7a30c2 }, { 0xff3a3a0f, 0xffa0b53f }, { 0xff0e1e3a, 0xff2c89d8 },
	{ 0xff3a2012, 0xffe07f4d }, { 0xff0f0f2c, 0xff2a3ad2 }, { 0xff122a1f, 0xff3abf8f }, { 0xff4a2630, 0xffe07a9d } };



// ---------------------------------------------------------------- the 3D hands
// The held box's clip class: cartons gba, jewel cases by thickness (ps1 / ps1p PAL / ps1d multi-disc)
static int hands_box(const shelf_box *b)
{
	const bb_package *pk = b->system ? b->system->package : NULL;
	return hands_box_class(!(pk && pk->style == BB_PACKAGE_CASE), b->t);
}
// With the room's hands (room_hands.c, hands.bbh) the chosen box is taken out,
// held, turned and opened by the UI mock's baked clips, in the box's slot.
static bool hands_now(hands_shelf_clip *hc, const shelf_box **box, float slot[3])
{
	const shelf_box *b = shelf_scene_pulled(shelf_scene_view);
	if (!b || !room_hands_open()) return false;
	int kind = hands_box(b);
	hands_shelf_state st = { shelf_scene_view->t, shelf_scene_view->pull, shelf_scene_view->pto, shelf_scene_view->pt0, shelf_scene_view->pdur, shelf_scene_view->play_t0 >= shelf_scene_view->pt0 ? shelf_scene_view->play_t0 : -1, shelf_scene_view->side, shelf_scene_view->turn_from, shelf_scene_view->turn_steps, shelf_scene_view->turn_t0,
		shelf_scene_view->rev, shelf_scene_view->rev_t };
	*hc = hands_for_shelf(room_hands_file(), kind, g_cfg.hand_glove == HAND_MITTENS, &st);
	if (!hc->clip) return false;
	*box = b;
	slot[0] = b->x; slot[1] = shelf_scene_view->rows[shelf_scene_view->prow].base; slot[2] = 4 + b->w / 2;   // the box standing in its slot
	return true;
}

bool shelf_scene_hands(void) { return shelf_scene_hands_open(); }

float shelf_scene_clip_seconds(const shelf_view *v, const char *action, float dflt)
{
	const shelf_box *b = v->pto == 1 || v->pull > .002f ? shelf_scene_pulled(v) : NULL;
	if (!b && v->row < v->nrows && v->rows[v->row].n) b = &v->rows[v->row].box[v->sel[v->row]];
	if (!b || !shelf_scene_hands_open()) return dflt;
	const hands_clip *c = hands_clip_for(room_hands_file(), action, hands_box(b), g_cfg.hand_glove == HAND_MITTENS);
	return c ? (!strncmp(action, "turn", 4) ? UI_MOTION_TURN_S : UI_MOTION_CASE_S) : dflt;
}

float shelf_scene_launch_seconds(const shelf_view *v)
{
	const shelf_box *b = shelf_scene_pulled(v);
	if (!b || !shelf_scene_hands_open()) return .9f;
	const hands_clip *c = hands_clip_for(room_hands_file(), "open", hands_box(b), g_cfg.hand_glove == HAND_MITTENS);
	return c ? UI_MOTION_LAUNCH_S : .9f;
}




// ---------------------------------------------------------------- the held case's back
static C3D_Mtx back_view, back_text_view;
static unsigned back_who(const shelf_box *b) { return hashc(b->title) * 31u + (unsigned)(uintptr_t)b->system; }
// text along a line from (x, y) at angle a (0 across, -PI/2 up), at most `len` long: smaller to
// 70 %, then squeezed
static void back_text(const char *s, float x, float y, float len, float px, u32 ink, float a)
{
	C2D_Text t;
	C2D_TextBufClear(shelf_scene_state.text);
	if (!s || !*s || !C2D_TextFontParse(&t, NULL, shelf_scene_state.text, s)) return;
	C2D_TextOptimize(&t);
	float sc = text_px(px), sx = 1, tw = 0;
	C2D_TextGetDimensions(&t, sc, sc, &tw, NULL);
	if (tw > len && tw > 0) {
		float k = len / tw;
		if (k >= .7f) sc *= k;
		else { sc *= .7f; sx = k / .7f; }
	}
	C2D_ViewSave(&back_text_view);
	C2D_ViewTranslate(x, y);
	if (a) C2D_ViewRotate(a);
	C2D_DrawText(&t, C2D_WithColor, 0, 0, Z, sc * sx, sc, ink);
	C2D_ViewRestore(&back_text_view);
}
// Made in 256 x 256 units (scaled to the target); the case layout's back half shows all of it
static bool back_bake(const shelf_box *b)
{
	Tex3DS_SubTexture cs;
	C3D_Tex *cover = covers_get(b->cover, &cs);
	unsigned who = back_who(b), tier = cover ? covers_full(b->cover) ? 3u : 2u : 1u;
	if (who == shelf_scene_case_back.who && tier == shelf_scene_case_back.tier) return true;
	const bb_package *pk = b->system->package;
	bool gba = !(pk && pk->style == BB_PACKAGE_CASE);
	u32 c0 = b->accent ? b->accent : box_color(b), white = 0xffffffff, paper = shelf_scene_rgba(244, 244, 240, 255);
	u32 bg = gba ? shelf_scene_shade(c0, .55f) : shelf_scene_rgba(22, 23, 27, 255);
	u32 lite = shelf_scene_rgba(fminf(255, ch(c0, 0) * 1.25f + 30), fminf(255, ch(c0, 1) * 1.25f + 30), fminf(255, ch(c0, 2) * 1.25f + 30), 255);
	C2D_SceneBegin(shelf_scene_case_back.rt);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C2D_ViewSave(&back_view);
	C2D_ViewScale(shelf_scene_case_back.size / 256.0f, shelf_scene_case_back.size / 256.0f);
	rect(0, 0, 256, 256, bg);
	vgrad(0, 0, 256, 256, with_alpha(white, gba ? 0 : .08f), with_alpha(0xff000000, .25f));
	float x0 = 10, x1 = 246;
	if (gba) {   // the system band down the left
		const float p[4][2] = { { 0, 0 }, { 30, 0 }, { 30, 256 }, { 0, 256 } };
		const u32 c[4] = { shelf_scene_rgba(42, 31, 92, 255), shelf_scene_rgba(74, 58, 142, 255), shelf_scene_rgba(74, 58, 142, 255), shelf_scene_rgba(42, 31, 92, 255) };
		shelf_scene_solid4(p, c);
		back_text("GAME BOY ADVANCE", 6, 246, 236, 14, shelf_scene_rgba(232, 226, 255, 255), -PI / 2);
		x0 = 40;
	} else {     // the insert's spine strips at both edges, the title down the left one
		for (int i = 0; i < 2; i++) {
			float x = i ? 240 : 0;
			rect(x, 0, 16, 256, shelf_scene_rgba(12, 12, 14, 255));
			rect(x + 5, 0, 1, 256, with_alpha(white, .14f));
		}
		back_text(b->title, 13, 10, 236, 8, shelf_scene_rgba(232, 232, 232, 255), PI / 2);
		x0 = 26; x1 = 230;
	}
	back_text(b->title, x0, 12, x1 - x0, 22, white, 0);
	rect(x0, 42, x1 - x0, 2, lite);
	// two screens cut from the cover art, white-framed
	float sw = (x1 - x0 - 10) / 2, sh = sw * .67f, sy = 54;
	static const float cut[2][2] = { { .15f, .25f }, { .55f, .55f } };
	for (int i = 0; i < 2; i++) {
		float sx = x0 + i * (sw + 10);
		rect(sx - 2, sy - 2, sw + 4, sh + 4, paper);
		if (!cover) { rect(sx, sy, sw, sh, shelf_scene_shade(bg, .7f)); continue; }
		float du = cs.right - cs.left, dv = cs.bottom - cs.top, u0 = cs.left + du * cut[i][0], v0 = cs.top + dv * cut[i][1];
		const float p[4][2] = { { sx, sy }, { sx + sw, sy }, { sx + sw, sy + sh }, { sx, sy + sh } };
		const float uv[4][2] = { { u0, v0 }, { u0 + du * .32f, v0 }, { u0 + du * .32f, v0 + dv * .24f }, { u0, v0 + dv * .24f } };
		const u32 c[4] = { white, white, white, white };
		shelf_scene_tquad(cover, p, uv, c);
	}
	// the blurb: lines, not text
	for (int i = 0; i < 7; i++) {
		float y = sy + sh + 16 + i * 9;
		if (y > 256 - 34) break;
		rect(x0, y, (x1 - x0) * (i == 6 ? .55f : .92f + .06f * sinf(i * 2.3f)), 3, with_alpha(white, .55f));
	}
	// the foot: a darker strip, a blank rating box, small marks (no codes, no numbers)
	rect(gba ? 30 : 16, 230, 256 - (gba ? 30 : 32), 26, with_alpha(0xff000000, .35f));
	rect(x0, 234, 14, 18, paper);
	for (int i = 0; i < 3; i++) rect(x0 + 24, 236 + i * 6, (x1 - x0) * .5f, 2, with_alpha(white, .5f));
	C2D_ViewRestore(&back_view);
	C2D_Flush();
	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
	shelf_scene_case_back.who = who;
	shelf_scene_case_back.tier = tier;
	return true;
}
// the chosen box's back, made ahead while it is browsed (settled) or in the hands
static void back_pending(void)
{
	if (!shelf_scene_case_back.rt || !shelf_scene_view || shelf_scene_view->level != SHELF_ONE || shelf_scene_view->row >= shelf_scene_view->nrows || !room_hands_file()) return;
	const shelf_box *b = shelf_scene_pulled(shelf_scene_view);
	if (!b) {
		const shelf_row *row = &shelf_scene_view->rows[shelf_scene_view->row];
		if (!row->n || fabsf(shelf_scene_view->cur - shelf_scene_view->sel[shelf_scene_view->row]) > .3f) return;
		b = &row->box[shelf_scene_view->sel[shelf_scene_view->row]];
	}
	back_bake(b);
}

// One-shot frame-start hook, chained after the cover uploads (covers_pump).
static void (*chained)(void);
static void pre_draw(void)
{
	void (*next)(void) = chained;
	video_pre_draw = next;
	chained = NULL;
	if (next) next();
	if (shelf_scene_state.ready && shelf_scene_view) {
		u64 t0 = svcGetSystemTick();
		shelf_scene_grain_step();
		shelf_scene_bake_pending();
		back_pending();
		shelf_scene_state.ticks[1] += svcGetSystemTick() - t0;
		room_pace_add(ROOM_PACE_SPINES, svcGetSystemTick() - t0);
	}
}
static void room_step(void);
// the hands' clip changes blend (hands.h hands_track): fed once a frame, on the shelf's clock
static hands_track hands_tr;
static void hands_track_frame(void)
{
	hands_shelf_clip hc;
	const shelf_box *hb;
	float slot[3];
	bool on = shelf_scene_pulled(shelf_scene_view) && room_hands_file() && hands_now(&hc, &hb, slot);
	hands_track_set(&hands_tr, on ? hc.clip : NULL, on ? hc.t : 0, shelf_scene_view->t);
	room_hands_use_track(&hands_tr, shelf_scene_view->t);
}

static void warm_case(void);
void shelf_scene_frame(const shelf_view *v)
{
	shelf_scene_view = v;
	shelf_scene_state.frame++;
	hands_track_frame();
	if (in_room()) room_step();
	if (video_pre_draw != pre_draw) {
		chained = video_pre_draw;
		video_pre_draw = pre_draw;
	}
}

// ---------------------------------------------------------------- the room

// A bookend closes the right end of a shelf that is not full (never the left).
// Its place is on the shelf's boxes' own depth order: scene() paints it with
// them, by distance from the camera, so it never shows through a spine.
#define BOOKEND_H 110.0f


// ---------------------------------------------------------------- the pulled box and the hand
static void case_of(const shelf_box *b, room_hands_case *box, Tex3DS_SubTexture *cs);


// ---------------------------------------------------------------- the top screen
const shelf_box *shelf_pulled_box(const shelf_view *v) { return shelf_scene_pulled(v); }



// ---------------------------------------------------------------- the Library in the room
static struct {
	bool init;
	float t, hy, hv, camx, camy, camvx, camvy;
	int row, cub;
	unsigned layout;
	float t_run;         // when the Library came on (its labels fade in)
	float cub_t;         // when the cubby in view changed (its boxes fade in)
	room_box box[ROOM_BOXES];
} L = { .cub = -1 };

static float row_total(const shelf_row *row) { return row->n ? row->end - X0 + 2 : 0; }

shelf_scene_flow shelf_scene_flowed = { .row = -1 };
// Keep layout-only projection work out of the cached per-frame flow path.
static PSX3D_COLD void row_camera_bounds(const shelf_row *row)
{
	room_case_geo g;
	// Compute the viewport limits only when the row layout changes. At an
	// end, leave 12 mm beyond the outer upright (or the last box's bookend),
	// including stereo disparity and the selected spine's forward tip.
	room_pose close = room_section_pose(room_scene_file(), 0, 0, row->hmax > 0 ? row->hmax : 130, 0, 0);
	float half = 200 * close.conv / F;
	// room_case_plan_make assigns exactly one cubby to each system row.
	room_case_section(room_scene_file(), shelf_scene_flowed.first, &g);
	float upright = g.xin0 - g.xl;
	float left = g.xl - upright, right = fminf(g.xr + upright, g.xin0 + shelf_scene_flowed.cw[0] + 10);
	float lo = left + half - 12, hi = right - half + 12;
	// A short row fits whole: one stable frame, with both ends visible.
	if (lo > hi) lo = hi = (left + right) / 2;
	shelf_scene_flowed.cam_lo = lo; shelf_scene_flowed.cam_hi = hi;
}
static bool flow(int r)
{
	const room_case_plan *p = room_scene_case_plan();
	if (!p || r < 0 || r >= p->nsec || r >= shelf_scene_view->nrows || p->count[r] <= 0) return false;
	const shelf_row *row = &shelf_scene_view->rows[r];
	if (shelf_scene_flowed.row == r && shelf_scene_flowed.layout == shelf_scene_view->layout && shelf_scene_flowed.base == row->box && shelf_scene_flowed.n == row->n && shelf_scene_flowed.k == p->count[r] && shelf_scene_flowed.first == p->first[r])
		return true;
	shelf_scene_flowed.row = r; shelf_scene_flowed.layout = shelf_scene_view->layout; shelf_scene_flowed.base = row->box; shelf_scene_flowed.n = row->n; shelf_scene_flowed.k = p->count[r]; shelf_scene_flowed.first = p->first[r];
	room_case_geo g;
	room_case_section(room_scene_file(), shelf_scene_flowed.first, &g);
	float cap = fmaxf(g.inner - 2, row_total(row) / shelf_scene_flowed.k), x = 0;
	int j = 0;
	memset(shelf_scene_flowed.cw, 0, sizeof(shelf_scene_flowed.cw));
	for (int i = 0; i < row->n && i < FLOW_MAX; i++) {
		const shelf_box *b = &row->box[i];
		float gap = i ? (b->x - b->t / 2) - (row->box[i - 1].x + row->box[i - 1].t / 2) : b->x - b->t / 2 - X0;
		gap = clampf(gap, .5f, 6);
		if (x > 0 && x + gap + b->t > cap && j < shelf_scene_flowed.k - 1) { shelf_scene_flowed.cw[j] = x + 2; j++; x = 0; }
		x += gap;
		shelf_scene_flowed.cub[i] = (unsigned char)j;
		shelf_scene_flowed.lx[i] = x + b->t / 2;
		x += b->t;
	}
	shelf_scene_flowed.cw[j] = x + 2;
	row_camera_bounds(row);
	return true;
}
// The cubby showing a shelf at camera position cur (boxes, fractional), its geometry and
// how far its boxes have slid (mm)
static bool row_place(int r, float cur, int *cubby, room_case_geo *g, float *off)
{
	const shelf_row *row = &shelf_scene_view->rows[r];
	if (!flow(r)) return false;
	*off = 0;
	*cubby = shelf_scene_flowed.first;
	if (!row->n) return room_case_section(room_scene_file(), *cubby, g);
	int n = row->n < FLOW_MAX ? row->n : FLOW_MAX;
	int i = (int)clampf(roundf(cur), 0, n - 1), i0 = (int)clampf(floorf(cur), 0, n - 1), i1 = i0 + 1 < n ? i0 + 1 : i0;
	int j = shelf_scene_flowed.cub[i];
	float lc = shelf_scene_flowed.cub[i0] == shelf_scene_flowed.cub[i1] && shelf_scene_flowed.cub[i0] == j ? lerpf(shelf_scene_flowed.lx[i0], shelf_scene_flowed.lx[i1], clampf(cur - i0, 0, 1)) : shelf_scene_flowed.lx[i];
	*cubby = shelf_scene_flowed.first + j;
	if (!room_case_section(room_scene_file(), *cubby, g)) return false;
	*off = clampf(lc - g->inner / 2, 0, fmaxf(0, shelf_scene_flowed.cw[j] - g->inner));
	return true;
}
// a section's first cubby: its label, the wall view's row
static bool first_cubby(int r, room_case_geo *g)
{
	const room_case_plan *p = room_scene_case_plan();
	return p && r >= 0 && r < p->nsec && room_case_section(room_scene_file(), p->first[r], g);
}
static float hsel_base(void)
{
	room_case_geo g;
	return first_cubby(shelf_scene_view->hsel, &g) ? g.base : 0;
}
// Aim at the chosen box until the viewport reaches this row's upright/bookend.
static bool cam_target(int r, float *x, float *base)
{
	int c;
	room_case_geo g;
	float off;
	if (!row_place(r, shelf_scene_view->cur, &c, &g, &off)) return false;
	*base = g.base;
	*x = g.cx;
	const shelf_row *row = &shelf_scene_view->rows[r];
	if (row->n) {
		int i = (int)clampf(roundf(shelf_scene_view->cur), 0, (row->n < FLOW_MAX ? row->n : FLOW_MAX) - 1);
		*x = clampf(g.xin0 + shelf_scene_flowed.lx[i] - off, shelf_scene_flowed.cam_lo, shelf_scene_flowed.cam_hi);
	}
	return true;
}
// once per frame: the wall's row spring, the camera along the shelf, scrolling speed
static void room_step(void)
{
	float dt = L.init ? clampf(shelf_scene_view->t - L.t, 0, .1f) : 0;
	L.t = shelf_scene_view->t;
	float tx, ty;
	bool aim = shelf_scene_view->nrows && cam_target(shelf_scene_view->row < shelf_scene_view->nrows ? shelf_scene_view->row : 0, &tx, &ty);
	if (!L.init) {
		L.init = true;
		L.layout = shelf_scene_view->layout;
		L.hy = hsel_base();
		L.hv = 0;
		if (aim) { L.camx = tx; L.camy = ty; }
		L.camvx = L.camvy = 0;
		return;
	}
	// Catalog metadata (Favorite/sort) may rebuild the rows while the camera moves.
	// Preserve the current pose and let the same springs reach the new target.
	L.layout = shelf_scene_view->layout;
	ui_motion_settle(&L.hy, &L.hv, hsel_base(), UI_MOTION_SETTLE_RATE, dt);
	if (aim && shelf_scene_view->zoom <= .001f) { L.camx = tx; L.camy = ty; L.camvx = L.camvy = 0; }   // walking up starts at the right cubby
	else if (aim) {
		ui_motion_settle(&L.camx, &L.camvx, tx, UI_MOTION_SETTLE_RATE, dt);
		ui_motion_settle(&L.camy, &L.camvy, ty, UI_MOTION_SETTLE_RATE, dt);
	}
}

// every field of a pose eased linearly (here F is this file's focal length macro)
_Static_assert(sizeof(room_pose) == 9 * sizeof(float), "room_pose is nine floats");
static room_pose mix_lin(const room_pose *a, const room_pose *b, float k)
{
	room_pose p;
	const float *x = (const float *)a, *y = (const float *)b;
	float *o = (float *)&p;
	for (int i = 0; i < 9; i++) o[i] = lerpf(x[i], y[i], k);
	return p;
}

static int cubbies_used(void) { const room_case_plan *p = room_scene_case_plan(); return p ? p->used : 0; }

bool shelf_scene_wall_pose(const shelf_view *v, room_pose *out)
{
	if (!in_room()) return false;
	const shelf_view *keep = shelf_scene_view;
	shelf_scene_view = v;
	float hy = L.init ? L.hy : v->nrows ? hsel_base() : 0;
	*out = room_wall_pose(room_scene_file(), cubbies_used(), hy);
	shelf_scene_view = keep;
	return true;
}

// the pulled box's slot in the room: the box standing in its place, its spine plane
static bool room_slot(const shelf_box *b, float slot[3])
{
	int c;
	room_case_geo g;
	float off;
	int i = (int)(b - shelf_scene_view->rows[shelf_scene_view->prow].box);
	if (i < 0 || i >= FLOW_MAX || !row_place(shelf_scene_view->prow, shelf_scene_view->prow == shelf_scene_view->row ? shelf_scene_view->cur : shelf_scene_view->pidx, &c, &g, &off)) return false;
	slot[0] = g.xin0 + shelf_scene_flowed.lx[i] - off; slot[1] = g.base; slot[2] = 5 + b->w / 2;
	return true;
}

// a point of the room on this eye's screen (400x240); false behind the camera
static bool room_project(const room_pose *p, float eye, float x, float y, float z, float *sx, float *sy)
{
	float m[16];
	room_camera(p, eye, m, NULL, NULL, NULL);
	float c[4];
	for (int r = 0; r < 4; r++) c[r] = m[r * 4] * x + m[r * 4 + 1] * y + m[r * 4 + 2] * z + m[r * 4 + 3];
	if (c[3] < 30) return false;
	// the top screen is turned a quarter: clip x is screen up, clip y screen left
	*sx = 200 - 200 * c[1] / c[3];
	*sy = 120 - 120 * c[0] / c[3];
	return true;
}

// The wall's section labels (UI mock wallLabels), on each section's first cubby: readable on
// the wall view, the highlighted one lit; they fade as the camera closes in. A name only, no
// second line: the bottom card gives the counts.
static void wall_labels(const room_pose *p)
{
	float a = (1 - ui_motion_ease(shelf_scene_view->zoom / .5f)) * ui_motion_ease((shelf_scene_view->t - L.t_run) / .4f);
	if (a <= .01f) return;
	float eye = ui_eye * (ui_parallax / 1.5f) * p->eye * .5f;
	for (int i = 0; i < shelf_scene_view->nrows; i++) {
		room_case_geo g;
		float x, y;
		if (!first_cubby(i, &g) || !room_project(p, eye, g.cx, g.top - 14, -8, &x, &y) || y < -10 || y > 250) continue;
		const shelf_row *row = &shelf_scene_view->rows[i];
		bool on = i == shelf_scene_view->hsel;
		// a paper label in a brass holder, as on a real cubby: the system's name, the chosen one lit
		const char *label = row->system ? row->system->name : "Favorites";
		float px = fit_px(label, 118, on ? 11.5f : 10), w = text_w(px, label) + 14, h = 14;
		u32 paper = on ? shelf_scene_room->paper : shelf_scene_shade(shelf_scene_room->paper, .68f);
		rect(x - w / 2 - 1.5f, y - 9.5f, w + 3, h + 3, with_alpha(shelf_scene_shade(shelf_scene_room->brass, on ? 1 : .72f), .96f * a));
		rect(x - w / 2, y - 8, w, h, with_alpha(paper, a));
		shelf_scene_text_at(x, y + 3.5f, px, with_alpha(shelf_scene_room->plate_ink, a), C2D_AlignCenter, label);
	}
}

void shelf_scene_begin(const shelf_view *v) { L.t_run = v->t; }

static room_pose room_cam(void)
{
	const room_file *f = room_scene_file();
	room_pose wall = room_wall_pose(f, cubbies_used(), L.hy);
	if (shelf_scene_view->zoom <= .0005f || shelf_scene_view->row >= shelf_scene_view->nrows) return wall;
	const shelf_row *row = &shelf_scene_view->rows[shelf_scene_view->row];
	float pk = ui_motion_ease((shelf_scene_view->pull - .3f) / .5f);
	room_pose close = room_section_pose(f, L.camx, L.camy, row->hmax > 0 ? row->hmax : 130, pk, 0);
	room_pose p = room_mix(&wall, &close, shelf_scene_view->zoom);
	// a box in the hands: the clip's own camera (straight on, a little from above)
	hands_shelf_clip hc;
	const shelf_box *hb;
	float slot[3], c[7];
	if (hands_now(&hc, &hb, slot) && hc.camera_k > 0 && room_slot(hb, slot) && room_hands_camera(hc.clip, hc.t, c)) {
		room_pose clip = { { c[0] + slot[0], c[1] + slot[1], c[2] + slot[2] }, { c[3] + slot[0], c[4] + slot[1], c[5] + slot[2] }, F, p.eye,
			fmaxf(c[5] - c[2], 1) };
		p = mix_lin(&p, &clip, hc.camera_k);
	}
	return p;
}

/* One visibility/order contract for the atlas scheduler and both stereo eyes.
 * Cubby coordinates alone are insufficient: an overfull cubby can contain 1024
 * boxes, while the clamped end camera sees neighbors away from the selection. */
// Reject offscreen x intervals before per-box projection. The two horizontal
// homogeneous clip planes are linear in x; maximizing over the row's y range
// makes this conservative even during the pitched glide and held-case camera.
static void room_spine_xrange(const float m[16], float base, float height, float *lo, float *hi)
{
 for(int side=-1;side<=1;side+=2) {
  float a=1.06f*m[12]+side*m[4], by=1.06f*m[13]+side*m[5];
  float c=(1.06f*m[14]+side*m[6])*5+1.06f*m[15]+side*m[7];
  c+=fmaxf(by*base,by*(base+height));
  if(a>.000001f)*lo=fmaxf(*lo,-c/a);
  else if(a<-.000001f)*hi=fminf(*hi,-c/a);
 }
}
static bool room_spine_project(const float m[16],float x,float y,float *sx,float *sy)
{
 float w=m[12]*x+m[13]*y+m[14]*5+m[15];if(w<30)return false;
 *sx=200-200*(m[4]*x+m[5]*y+m[6]*5+m[7])/w;
 *sy=120-120*(m[0]*x+m[1]*y+m[2]*5+m[3])/w;return true;
}
static int room_collect_pose(int r,const room_pose *p,int *indices,int cap,int *tested)
{
 int cubby;room_case_geo g;float off;
 if(!row_place(r,row_cur(r),&cubby,&g,&off))return 0;
 const shelf_row *row=&shelf_scene_view->rows[r];int cnt=row->n<FLOW_MAX?row->n:FLOW_MAX;
 float matrices[2][16],xmin=g.xl,xmax=g.xr,eye_lo=1e9f,eye_hi=-1e9f;
 for(int eye=0;eye<2;eye++) {
  float e=(eye?1:-1)*(ui_parallax/1.5f)*p->eye*.5f;
  room_camera(p,e,matrices[eye],NULL,NULL,NULL);
  float lo=-1e9f,hi=1e9f;room_spine_xrange(matrices[eye],g.base,row->hmax,&lo,&hi);
  eye_lo=fminf(eye_lo,lo);eye_hi=fmaxf(eye_hi,hi);
 }
 xmin=fmaxf(xmin,eye_lo);xmax=fminf(xmax,eye_hi);
 int cub=cubby-shelf_scene_flowed.first,lo=0,hi=cnt;
 while(lo<hi){int m=(lo+hi)/2;if(shelf_scene_flowed.cub[m]<cub)lo=m+1;else hi=m;}
 int first=lo;hi=cnt;
 while(lo<hi){int m=(lo+hi)/2;if(shelf_scene_flowed.cub[m]<=cub)lo=m+1;else hi=m;}
 int end=lo;lo=first;hi=end;
 while(lo<hi){int m=(lo+hi)/2;float right=g.xin0+shelf_scene_flowed.lx[m]-off+row->box[m].t/2;if(right<xmin)lo=m+1;else hi=m;}
 first=lo;lo=first;hi=end;
 while(lo<hi){int m=(lo+hi)/2;float left=g.xin0+shelf_scene_flowed.lx[m]-off-row->box[m].t/2;if(left<=xmax)lo=m+1;else hi=m;}
 end=lo;
 int n=0;float distances[64];if(cap>64)cap=64;
 for(int i=first;i<end;i++) {
  const shelf_box *b=&row->box[i];float x=g.xin0+shelf_scene_flowed.lx[i]-off;
  if(x-b->t/2<g.xl || x+b->t/2>g.xr)continue;
  (*tested)++;bool visible=false;float distance=1e9f;
  for(int eye=0;eye<2;eye++) {
   float a,y,z,y2;
   if(!room_spine_project(matrices[eye],x-b->t/2,g.base+b->h/2,&a,&y) ||
      !room_spine_project(matrices[eye],x+b->t/2,g.base+b->h/2,&z,&y2))continue;
   if(fmaxf(a,z)>=-12 && fminf(a,z)<=412 && y>=-24 && y<=264)visible=true;
   distance=fminf(distance,fabsf((a+z)/2-200));
  }
  if(!visible)continue;
  int at=n;while(at>0 && distance<distances[at-1])at--;
  if(at>=cap)continue;
  if(n<cap)n++;
  for(int j=n-1;j>at;j--){indices[j]=indices[j-1];distances[j]=distances[j-1];}
  indices[at]=i;distances[at]=distance;
 }
 return n;
}
static __attribute__((unused)) int room_collect_spines(int r, int *indices, int cap)
{
 room_pose p=room_cam();int tested=0;return room_collect_pose(r,&p,indices,cap,&tested);
}
static int room_collect_target(int r,int *indices,int cap,int *tested)
{
 int cubby;room_case_geo g;float off;
 if(!row_place(r,row_cur(r),&cubby,&g,&off))return 0;
 const shelf_row *row=&shelf_scene_view->rows[r];if(!row->n)return 0;
 int i=(int)clampf(roundf(row_cur(r)),0,(row->n<FLOW_MAX?row->n:FLOW_MAX)-1);
 float x=clampf(g.xin0+shelf_scene_flowed.lx[i]-off,shelf_scene_flowed.cam_lo,shelf_scene_flowed.cam_hi);
 room_pose p=room_section_pose(room_scene_file(),x,g.base,row->hmax>0?row->hmax:130,0,0);
 return room_collect_pose(r,&p,indices,cap,tested);
}

int shelf_scene_visible_spines(int r, int *indices, int cap)
{
 if (!shelf_scene_state.visible.valid || shelf_scene_state.visible.frame!=shelf_scene_state.frame || shelf_scene_state.visible.row!=r || shelf_scene_state.visible.layout!=shelf_scene_view->layout) {
  u64 start=svcGetSystemTick();
  shelf_scene_state.visible.tested=0;room_pose p=room_cam();
  shelf_scene_state.visible.n=room_collect_pose(r,&p,shelf_scene_state.visible.indices,ROOM_BOXES,&shelf_scene_state.visible.tested);
  shelf_scene_state.visible.target_n=shelf_scene_view->zoom<.98f && shelf_scene_view->pull<.01f?room_collect_target(r,shelf_scene_state.visible.target,ROOM_BOXES,&shelf_scene_state.visible.tested):0;
  shelf_scene_state.visible.frame=shelf_scene_state.frame;shelf_scene_state.visible.row=r;shelf_scene_state.visible.layout=shelf_scene_view->layout;shelf_scene_state.visible.valid=true;
  static u64 total,peak;static unsigned samples;
  u64 dt=svcGetSystemTick()-start;total+=dt;if(dt>peak)peak=dt;
  if (++samples==300) {
   log_printf("shelf: visible list avg=%lu us peak=%lu us samples=%u row=%d boxes=%d tested=%d\n",
    (unsigned long)(total/(SYSCLOCK_ARM11/1000000)/samples),(unsigned long)(peak/(SYSCLOCK_ARM11/1000000)),samples,r,shelf_scene_view->rows[r].n,shelf_scene_state.visible.tested);
   total=peak=0;samples=0;
  }
 }
 int n=shelf_scene_state.visible.n<cap?shelf_scene_state.visible.n:cap;
 memcpy(indices,shelf_scene_state.visible.indices,n*sizeof(*indices));return n;
}

// the hands and the case in the room's camera (room_library.after)
// the case in the hands: the box's cover, its readable spine for the turned case
static void case_of(const shelf_box *b, room_hands_case *box, Tex3DS_SubTexture *cs)
{
	u64 c0 = svcGetSystemTick();
	C3D_Tex *cover = covers_get(b->cover, cs);
	room_pace_add(ROOM_PACE_COVER, svcGetSystemTick() - c0);
	*box = (room_hands_case){ b->cover * 4 + (cover != NULL) * 2, cover, cs, box_color(b), { b->t, b->h, b->w } };
	box->side = side_color(b);
	int c = shelf_scene_cell_of(b);
	if (c >= 0) {
		float u0, v0, u1, v1;
		shelf_scene_spine_uv(c, b, &u0, &v0, &u1, &v1);
		box->spine = cell_tex(c);
		box->spine_sub = (Tex3DS_SubTexture){ 0, 0, u0, v0, u1, v1 };
		box->cover_id += 1;
		shelf_scene_state.cell[c].used = shelf_scene_state.frame;
	}
	if (shelf_scene_case_back.rt && shelf_scene_case_back.tier && shelf_scene_case_back.who == back_who(b)) {   // its back is made (maybe still with the small cover's screens)
		box->back = &shelf_scene_case_back.tex;
		box->back_sub = (Tex3DS_SubTexture){ (u16)shelf_scene_case_back.size, (u16)shelf_scene_case_back.size, 0, 1, 1, 0 };
	}
}
// The chosen box's case is made ahead, ~1.5 ms a frame, while nothing is in the hands
static void warm_case(void)
{
	if (!in_room() || !room_hands_file() || shelf_scene_view->level != SHELF_ONE || shelf_scene_view->zoom < .5f || shelf_scene_pulled(shelf_scene_view) ||
	    shelf_scene_view->row >= shelf_scene_view->nrows || !shelf_scene_view->rows[shelf_scene_view->row].n)
		return;
	const shelf_box *b = &shelf_scene_view->rows[shelf_scene_view->row].box[shelf_scene_view->sel[shelf_scene_view->row]];
	if (shelf_scene_cell_of(b) < 0) return;   // its spine first (the case shows it when turned)
	static Tex3DS_SubTexture cs;
	room_hands_case box;
	case_of(b, &box, &cs);
	u64 t0 = svcGetSystemTick();
	room_hands_case_warm(&box, SYSCLOCK_ARM11 / 1000 * 3 / 2);
	room_pace_add(ROOM_PACE_PRELOAD, svcGetSystemTick() - t0);
}
// The case flies out in 1.1 s, so its cover, back and texture must be there when A is pressed:
// a late one showed the plain body or the cover darkened for a moment. Called while A waits (shelf.c want_out), it
// also gives the case texture a larger share of each frame. True when all three are made, or cannot be made.
bool shelf_scene_case_ready(const shelf_view *v)
{
	if (!in_room() || !room_hands_file() || v->row >= v->nrows || !v->rows[v->row].n) return true;
	const shelf_box *b = &v->rows[v->row].box[v->sel[v->row]];
	if (shelf_scene_cell_of(b) < 0) return false;
	static Tex3DS_SubTexture cs;
	room_hands_case box;
	case_of(b, &box, &cs);
	unsigned tier = box.cover ? covers_full(b->cover) ? 3u : 2u : 1u;   // what back_bake makes for this cover now
	bool back = !shelf_scene_case_back.rt || (shelf_scene_case_back.who == back_who(b) && shelf_scene_case_back.tier == tier);
	return room_hands_case_warm(&box, SYSCLOCK_ARM11 / 1000 * 4) && back;
}

static void room_hands_cb(const float proj[16], void *ud)
{
	(void)ud;
	hands_shelf_clip hc;
	const shelf_box *hb;
	float slot[3];
	const shelf_box *pulled = shelf_scene_pulled(shelf_scene_view);
	if (!pulled || !hands_now(&hc, &hb, slot) || !room_slot(pulled, slot)) return;
	float place[12] = { 1, 0, 0, slot[0], 0, 1, 0, slot[1], 0, 0, 1, slot[2] };
	float at[3] = { slot[0], slot[1] + pulled->h / 2, slot[2] - 200 }, to_cam[3] = { 0, .3f, -1 };
	room_scene_hands_light(at, to_cam);
	static Tex3DS_SubTexture cs;
	room_hands_case box;
	case_of(pulled, &box, &cs);
	box.yaw_mode = hc.yaw_mode; box.yaw = hc.yaw;
	u64 h0 = svcGetSystemTick();
	room_hands_draw(hc.clip, hc.t, place, proj, &box);
	room_pace_add(ROOM_PACE_HANDS, svcGetSystemTick() - h0);
	room_hands_light(NULL);
}

static void room_top(void)
{
	room_pose p = room_cam();
	room_library lib = { .hsel = shelf_scene_view->level == SHELF_ALL || shelf_scene_view->zoom < .98f ? shelf_scene_view->hsel : -1, .wall = 1 - ui_motion_ease(shelf_scene_view->zoom), .cur = -1,
		.box = L.box, .after = room_hands_cb };
	int cr = shelf_scene_view->zoom > .001f && shelf_scene_view->row < shelf_scene_view->nrows ? shelf_scene_view->row : -1, cubby;
	room_case_geo g;
	float off;
	if (cr >= 0 && row_place(cr, shelf_scene_view->cur, &cubby, &g, &off)) {
		const shelf_row *row = &shelf_scene_view->rows[cr];
		const shelf_box *pulled = shelf_scene_pulled(shelf_scene_view);
		float zs = ui_motion_ease(shelf_scene_view->zoom);
		if (cubby != L.cub) { L.cub = cubby; L.cub_t = shelf_scene_view->t; }   // another cubby: its boxes come in over its tile
		lib.cur = cubby;
		lib.fade = ui_motion_ease((shelf_scene_view->zoom - .3f) / .4f) * ui_motion_ease((shelf_scene_view->t - L.cub_t) / .25f);
		lib.lamp = ui_motion_ease((shelf_scene_view->zoom - .6f) / .3f);   // the lamp over the cubby: on as the camera arrives (~0.2 s)
		// Draw exactly the candidates the atlas scheduler considers visible.
		int n = 0, cnt = row->n < FLOW_MAX ? row->n : FLOW_MAX, j = cubby - shelf_scene_flowed.first;
		int indices[ROOM_BOXES], count=shelf_scene_visible_spines(cr,indices,ROOM_BOXES);
		for(int k=0;k<count;k++) {
			int i=indices[k];
			const shelf_box *b = &row->box[i];
			float x = g.xin0 + shelf_scene_flowed.lx[i] - off;
			if (b == pulled) continue;
			float near = i == shelf_scene_view->sel[cr] && shelf_scene_view->level == SHELF_ONE ? ui_motion_ease(1 - fabsf(shelf_scene_view->cur - i) * 3) * zs * (1 - shelf_scene_view->pull) : 0;
			const bb_package *pk = b->system->package;
			bool cased = pk && pk->style == BB_PACKAGE_CASE;
			u32 col = box_color(b);
			room_box *o = &L.box[n++];
			*o = (room_box){ x, g.base, 5 - UI_SHELF_TIP_MM * near, b->t, b->h, b->w, NULL, 0, 0, 0, 0,
				b->color ? shelf_scene_mix(col, 0xff000000, .25f) : shelf_scene_mix(placeholder(b, 1), placeholder(b, 0), .55f),
				side_color(b), 1 + (cased ? .35f : 0) * near };
			int c = shelf_scene_cell_of(b);
			if (c >= 0) {
				shelf_scene_spine_uv(c, b, &o->u0, &o->v0, &o->u1, &o->v1);
				o->spine = cell_tex(c);
				shelf_scene_state.cell[c].used = shelf_scene_state.frame;   // shared visible set: keep its cell
			}
		}
		// a row its system does not fill: a bookend after its last box (the right end only)
		// (oiled wood as the far view's, room_shelf_texture)
		if (n < ROOM_BOXES && cnt > 0 && shelf_scene_flowed.cub[cnt - 1] == j && shelf_scene_flowed.cw[j] + 6 + 4 <= g.inner)
			L.box[n++] = (room_box){ g.xin0 + shelf_scene_flowed.cw[j] + 2 + 3, g.base, 5, 6, BOOKEND_H, 120, NULL, 0, 0, 0, 0,
				0xff386496, 0xff284a70, 1 };
		lib.nbox = n;
	} else
		L.cub = -1;
	room_scene_draw_library(&p, shelf_scene_view->t, &lib);
	room_scene_overlay_begin();
	u64 l0 = svcGetSystemTick();
	wall_labels(&p);
	room_pace_add(ROOM_PACE_LABELS, svcGetSystemTick() - l0);
	room_scene_overlay_end();
}

void shelf_scene_top(const shelf_view *v)
{
	shelf_scene_view = v;
	shelf_scene_room = theme_room(theme());
	if (shelf_scene_room && in_room() && shelf_scene_prepare_gpu() && v->nrows) {
		if (ui_eye <= 0) {
			C2D_TextBufClear(shelf_scene_state.text);
		}
		if (ui_eye <= 0) warm_case();   // after the frame's covers_focus: its cover is asked for in rank
		u64 t0 = svcGetSystemTick();
		room_top();
		u64 dt = svcGetSystemTick() - t0;
		static u64 frame_top;
		frame_top = ui_eye <= 0 ? dt : frame_top + dt;
		shelf_scene_state.ticks[0] += dt;
		room_pace_add(ROOM_PACE_TOP, dt);
		if (frame_top > shelf_scene_state.peak[0]) shelf_scene_state.peak[0] = frame_top;
		shelf_scene_state.eyes += ui_eye != 0;
		return;
	}
	// no room (it did not load) or nothing to show yet: the page colour (home shows the room's error card)
	C2D_DrawRectSolid(0, 0, Z, 400, 240, theme()->surface);
}
