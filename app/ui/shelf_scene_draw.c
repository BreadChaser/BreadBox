#include "shelf_scene_internal.h"

// a render target must be in VRAM; the second atlas only if it fits
static void atlases_make(void)
{
	for (shelf_scene_state.atlases = 0; shelf_scene_state.atlases < ATLASES; shelf_scene_state.atlases++) {
		C3D_Tex *a = &shelf_scene_state.atlas[shelf_scene_state.atlases];
		if (!C3D_TexInitVRAM(a, ATLAS, ATLAS, GPU_RGB565)) break;
		C3D_TexSetFilter(a, GPU_LINEAR, GPU_LINEAR);
		C3D_TexSetWrap(a, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		if (!(shelf_scene_state.atlas_rt[shelf_scene_state.atlases] = C3D_RenderTargetCreateFromTex(a, GPU_TEXFACE_2D, 0, -1))) { C3D_TexDelete(a); break; }
	}
}
// the case's back (not needed to run: without it the back is the cover, darker)
static void back_make(void)
{
	for (int size = 256; size >= 128 && !shelf_scene_case_back.rt; size /= 2) {
		if (!C3D_TexInitVRAM(&shelf_scene_case_back.tex, size, size, GPU_RGB565)) continue;
		C3D_TexSetFilter(&shelf_scene_case_back.tex, GPU_LINEAR, GPU_LINEAR);
		C3D_TexSetWrap(&shelf_scene_case_back.tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		if ((shelf_scene_case_back.rt = C3D_RenderTargetCreateFromTex(&shelf_scene_case_back.tex, GPU_TEXFACE_2D, 0, -1))) shelf_scene_case_back.size = size;
		else C3D_TexDelete(&shelf_scene_case_back.tex);
	}
}
static void back_release(void);
bool shelf_scene_prepare_gpu_now(void)
{
	shelf_scene_state.tried = true;
	u64 t0 = svcGetSystemTick();
	const char *step = "grain";
	if (!C3D_TexInit(&shelf_scene_state.grain, GRAIN_W, GRAIN_H, GPU_L8)) goto fail;
	shelf_scene_grain_start(&shelf_scene_state.grain);
	C3D_TexSetFilter(&shelf_scene_state.grain, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&shelf_scene_state.grain, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	svcStoreProcessDataCache(CUR_PROCESS_HANDLE, (u32)shelf_scene_state.grain.data, shelf_scene_state.grain.size);
	step = "lights";
	if (!shelf_scene_radial(&shelf_scene_state.glow, false) || !shelf_scene_radial(&shelf_scene_state.dark, true)) goto fail;
	step = "spines";
	atlases_make();
	// the case's back in what is left (128 px: 256 costs the second atlas; made first and the atlases
	// deleted and made again when they did not fit, the first frame hung in Azahar)
	back_make();
	if (!shelf_scene_state.atlases) goto fail;
	step = "far shelves";
	step = "text";
	shelf_scene_state.text = C2D_TextBufNew(1024);
	shelf_scene_state.cache = C2D_TextBufNew(2048);
	if (!shelf_scene_state.text || !shelf_scene_state.cache) goto fail;
	shelf_scene_state.cell_row = -1;
	shelf_scene_state.ready = true;
	log_printf("shelf: ready in %u ms (linear free %u KB, %d spine atlases, back %d px, VRAM free %u KB)\n",
		(unsigned)((svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000)), (unsigned)(linearSpaceFree() / 1024), shelf_scene_state.atlases, shelf_scene_case_back.size,
		(unsigned)(vramSpaceFree() / 1024));
	return true;
fail:
	log_printf("shelf: %s failed (linear free %u KB)\n", step, (unsigned)(linearSpaceFree() / 1024));
	shelf_scene_release();
	shelf_scene_state.tried = true;
	return false;
}

bool shelf_scene_prepare(void) { return theme_room(theme()) && shelf_scene_prepare_gpu(); }

// The held case's back (bb-room3d round 10, authentic backs): no real back scans, so one is
// made in the style of the system's real ones for the chosen box: GBA the cover's colour with
// the system band, PS1 a dark insert with the spine strips; the title, two screens cut from the
// cover, blurb lines, a blank rating box. No codes, no numbers. One 256 x 256 render target
// (128 x 128 if VRAM is short), made at the start of a frame when the chosen box changes or its
// full-size cover arrives (~1-2 ms, once).
static void back_release(void)
{
	if (shelf_scene_case_back.rt) C3D_RenderTargetDelete(shelf_scene_case_back.rt);
	if (shelf_scene_case_back.tex.data) C3D_TexDelete(&shelf_scene_case_back.tex);
	memset(&shelf_scene_case_back, 0, sizeof(shelf_scene_case_back));
}
void shelf_scene_release(void)
{
	back_release();
	for (int i = 0; i < ATLASES; i++) {
		if (shelf_scene_state.atlas_rt[i]) C3D_RenderTargetDelete(shelf_scene_state.atlas_rt[i]);
		if (shelf_scene_state.atlas[i].data) C3D_TexDelete(&shelf_scene_state.atlas[i]);
	}
	if (shelf_scene_state.cache) C2D_TextBufDelete(shelf_scene_state.cache);
	// grain_job owns CPU bytes: no wait for a cancelled/background read here.
	if (shelf_scene_state.grain.data) C3D_TexDelete(&shelf_scene_state.grain);
	if (shelf_scene_state.glow.data) C3D_TexDelete(&shelf_scene_state.glow);
	if (shelf_scene_state.dark.data) C3D_TexDelete(&shelf_scene_state.dark);
	for (int i = 0; i < LOGOS; i++) if (shelf_scene_state.logos[i].ok) C3D_TexDelete(&shelf_scene_state.logos[i].tex);
	if (shelf_scene_state.text) C2D_TextBufDelete(shelf_scene_state.text);
	memset(&shelf_scene_state, 0, sizeof(shelf_scene_state));
}

// ---------------------------------------------------------------- drawing primitives
static C2Di_Context *tex_mode(C3D_Tex *t, unsigned vertices)
{
	return scene_reserve(vertices, vertices / 4 * 6, C2DiF_Mode_ImageMult, t);
}
// 2D textured quad (TL, TR, BR, BL), texture rect in texture coords.
void shelf_scene_tquad(C3D_Tex *t, const float p[4][2], const float uv[4][2], const u32 c[4])
{
	C2Di_Context *ctx = tex_mode(t, 4);
	if (!ctx) return;
	C2Di_Vertex *v = scene_quad(ctx);   // order: 0 TL, 1 TR, 2 BL, 3 BR
	scene_vertex(&v[0], p[0][0], p[0][1], Z, uv[0][0], uv[0][1], 0, 1, c[0]);
	scene_vertex(&v[1], p[1][0], p[1][1], Z, uv[1][0], uv[1][1], 0, 1, c[1]);
	scene_vertex(&v[2], p[3][0], p[3][1], Z, uv[3][0], uv[3][1], 0, 1, c[3]);
	scene_vertex(&v[3], p[2][0], p[2][1], Z, uv[2][0], uv[2][1], 0, 1, c[2]);
}
void shelf_scene_solid4(const float p[4][2], const u32 c[4])
{
	C2Di_Context *ctx = scene_reserve(4, 6, C2DiF_Mode_Solid, NULL);
	if (!ctx) return;
	C2Di_Vertex *v = scene_quad(ctx);
	scene_vertex(&v[0], p[0][0], p[0][1], Z, 0, 0, 0, 0, c[0]);
	scene_vertex(&v[1], p[1][0], p[1][1], Z, 0, 0, 0, 0, c[1]);
	scene_vertex(&v[2], p[3][0], p[3][1], Z, 0, 0, 0, 0, c[3]);
	scene_vertex(&v[3], p[2][0], p[2][1], Z, 0, 0, 0, 0, c[2]);
}
static void tri(float ax, float ay, u32 a, float bx, float by, u32 b, float cx, float cy, u32 c)
{
	scene_solid_triangle(NULL, ax, ay, a, bx, by, b, cx, cy, c, Z);
}
static void line(float x0, float y0, float x1, float y1, float w, u32 c)
{
	float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
	if (l < 1e-4f) return;
	float nx = -dy / l * w / 2, ny = dx / l * w / 2;
	const float p[4][2] = { { x0 + nx, y0 + ny }, { x1 + nx, y1 + ny }, { x1 - nx, y1 - ny }, { x0 - nx, y0 - ny } };
	solid(p, c);
}
// Filled outline (a star-shaped path round its centroid) with a linear gradient.
static void poly(float (*p)[2], int n, u32 c0, u32 c1, float gx0, float gy0, float gx1, float gy1)
{
	float cx = 0, cy = 0;
	for (int i = 0; i < n; i++) { cx += p[i][0]; cy += p[i][1]; }
	cx /= n; cy /= n;
	float gx = gx1 - gx0, gy = gy1 - gy0, gl = gx * gx + gy * gy;
#define AT(x, y) shelf_scene_mix(c0, c1, gl > 0 ? clampf(((x) - gx0) * gx / gl + ((y) - gy0) * gy / gl, 0, 1) : 0)
	u32 cc = AT(cx, cy);
	for (int i = 0; i < n; i++) {
		int j = (i + 1) % n;
		tri(cx, cy, cc, p[i][0], p[i][1], AT(p[i][0], p[i][1]), p[j][0], p[j][1], AT(p[j][0], p[j][1]));
	}
#undef AT
}
static void sprite(C3D_Tex *t, float cx, float cy, float rx, float ry, u32 c)
{
	const float p[4][2] = { { cx - rx, cy - ry }, { cx + rx, cy - ry }, { cx + rx, cy + ry }, { cx - rx, cy + ry } };
	const float uv[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
	const u32 cc[4] = { c, c, c, c };
	shelf_scene_tquad(t, p, uv, cc);
}

// Parsed text kept from frame to frame: the card and the plates change rarely,
// so most frames only draw their glyphs. Long one-off strings use R.text.
const C2D_Text *shelf_scene_text_get(const char *s, C2D_Text *tmp)
{
	if (!shelf_scene_state.cache || !s || !*s) return NULL;
	size_t n = strlen(s);
	if (n >= sizeof(shelf_scene_state.tc[0].s)) {
		if (!C2D_TextFontParse(tmp, NULL, shelf_scene_state.text, s)) return NULL;
		C2D_TextOptimize(tmp);
		return tmp;
	}
	u32 h = hashc(s);
	for (int i = 0; i < shelf_scene_state.tcn; i++)
		if (shelf_scene_state.tc[i].hash == h && !strcmp(shelf_scene_state.tc[i].s, s)) return &shelf_scene_state.tc[i].t;
	if (shelf_scene_state.tcn == TEXT_CACHE || !C2D_TextFontParse(&shelf_scene_state.tc[shelf_scene_state.tcn].t, NULL, shelf_scene_state.cache, s)) {
		C2D_TextBufClear(shelf_scene_state.cache);
		shelf_scene_state.tcn = 0;
		if (!C2D_TextFontParse(&shelf_scene_state.tc[0].t, NULL, shelf_scene_state.cache, s)) return NULL;
	}
	C2D_TextOptimize(&shelf_scene_state.tc[shelf_scene_state.tcn].t);
	shelf_scene_state.tc[shelf_scene_state.tcn].hash = h;
	memcpy(shelf_scene_state.tc[shelf_scene_state.tcn].s, s, n + 1);
	return &shelf_scene_state.tc[shelf_scene_state.tcn++].t;
}
// y is the text's baseline: the room's 3D labels were placed with .82 of their size; on ruled paper (between
// card_begin and card_end) the loaded font's own baseline (UI_PAPER_TOP), so the letters stand on the ruled lines
static bool text_on_paper;
void shelf_scene_text_at(float x, float y, float px, u32 c, u32 flags, const char *s)
{
	C2D_Text tmp;
	const C2D_Text *t = shelf_scene_text_get(s, &tmp);
	float top = text_on_paper ? UI_PAPER_TOP(y, text_px(px)) : y - px * .82f;
	if (t) C2D_DrawText(t, C2D_WithColor | flags, x, top, Z, text_px(px), text_px(px), c);
}

// One line on the shared fit policy (ui_fit.h): px, or smaller down to min_px, then cut between whole characters
// with "..." (a long game name or path never turns into micro text). flags: 0 or C2D_AlignRight (x is the right edge).
static void text_fit_at(float x, float y, float width, float px, float min_px, u32 c, u32 flags, const char *s)
{
	float scale;
	const char *t = ui_fit_ex(NULL, s, text_px(px), text_px(min_px), width, &scale);
	if (t[0]) shelf_scene_text_at(x, y, scale / text_px(1), c, flags, t);
}
static void text_fit(float x, float y, float width, float px, float min_px, u32 c, const char *s) { text_fit_at(x, y, width, px, min_px, c, 0, s); }
// The shared card header's text (ui_card_header): one line, any font, at the scene's depth, parsed into the
// scene's own per-draw buffer (the shelf loop never clears the shared one ui_text_ex uses).
static void header_text(C2D_Font font, float x, float y, float size, u32 c, u32 flags, const char *s)
{
	C2D_Text t;
	C2D_TextBufClear(shelf_scene_state.text);
	if (!s || !*s || !C2D_TextFontParse(&t, font, shelf_scene_state.text, s)) return;
	C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_WithColor | flags, x, y, Z, size, size, c);
}
// a card's header row: its title on the red rule (the pause and settings card's own routine and metrics)
static void card_title(const char *title, float width)
{
	ui_card_header(header_text, UI_ROOM_TEXT_X, UI_ROOM_TEXT_X + width, UI_ROOM_RULE_Y, title, NULL, shelf_scene_room->ink, with_alpha(shelf_scene_room->rule, 1));
}
// The longest head of s (bytes) that fits width at px: broken after a space when one fits, else between whole
// characters (a title without spaces, Japanese). 0 when s fits whole.
static size_t text_break(const char *s, float width, float px)
{
	if (text_w(px, s) <= width) return 0;
	char tmp[SHELF_TITLE + 8];
	size_t best = 0, best_space = 0, n = strlen(s);
	if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
	for (size_t i = 1; i <= n; i++) {
		if (i < n && ((unsigned char)s[i] & 0xc0) == 0x80) continue;
		memcpy(tmp, s, i); tmp[i] = 0;
		if (text_w(px, tmp) > width) break;
		best = i;
		if (i < n && s[i] == ' ') best_space = i;
	}
	return best_space ? best_space : best;
}
// Helper text over up to `lines` lines (broken at spaces, between characters if it must), the last one cut with
// "..." when the text is longer; returns the lines used.
static int text_lines(float x, float y, float step, float width, float px, int lines, u32 c, const char *s)
{
	char line[SHELF_TITLE + 8];
	int used = 0;
	while (*s && used < lines) {
		size_t cut = used + 1 < lines ? text_break(s, width, px) : 0;
		if (!cut) { text_fit(x, y + step * used++, width, px, UI_ROOM_MIN_PX, c, s); break; }
		ui_fit_copy(line, sizeof(line), s, cut);
		shelf_scene_text_at(x, y + step * used++, px, c, 0, line);
		s += cut;
		while (*s == ' ') s++;
	}
	return used;
}


static float uv_u(int r, float f) { return (shelf_scene_grain_region[r][0] + f * shelf_scene_grain_region[r][2]) / GRAIN_W; }
static float uv_v(int r, float f) { return 1 - (shelf_scene_grain_region[r][1] + f * shelf_scene_grain_region[r][3]) / GRAIN_H; }
// The shelf paints back to front: no depth buffer reads or writes (GPU fill time).
static void depth_off(bool off)
{
	C2D_Flush();
	if (off) C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	else C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
}
// ---------------------------------------------------------------- the desk (bottom screen)
static void desk(void)
{
	// the desk grain twice, one above the other (no vertical stretch)
	for (int i = 0; i < 2; i++) {
		float y = i * 120;
		const float p[4][2] = { { 0, y }, { 320, y }, { 320, y + 120 }, { 0, y + 120 } };
		const float uv[4][2] = { { uv_u(G_DESK, 0), uv_v(G_DESK, 0) }, { uv_u(G_DESK, 1), uv_v(G_DESK, 0) },
			{ uv_u(G_DESK, 1), uv_v(G_DESK, 1) }, { uv_u(G_DESK, 0), uv_v(G_DESK, 1) } };
		const u32 c[4] = { shelf_scene_room->desk, shelf_scene_room->desk, shelf_scene_room->desk, shelf_scene_room->desk };
		shelf_scene_tquad(&shelf_scene_state.grain, p, uv, c);
	}
	sprite(&shelf_scene_state.glow, 70, 40, 260, 260, with_alpha(shelf_scene_room->light, .16f));
	sprite(&shelf_scene_state.dark, 120, 90, 330, 330, with_alpha(0xff000000, .5f));
}
// a paper card, turned by ang about its centre; the caller draws on it in card coordinates
static C3D_Mtx card_saved;
static void card_begin(float x, float y, float w, float h, float ang)
{
	C2D_ViewSave(&card_saved);
	C2D_ViewTranslate(x + w / 2, y + h / 2);
	C2D_ViewRotate(ang);
	C2D_ViewTranslate(-w / 2, -h / 2);
	rect(4, 5, w, h, 0x73000000);
	const float p[4][2] = { { 0, 0 }, { w, 0 }, { w, h }, { 0, h } };
	u32 mid = shelf_scene_mix(shelf_scene_room->paper, shelf_scene_room->paper_edge, .45f);
	const u32 c[4] = { shelf_scene_room->paper, mid, shelf_scene_room->paper_edge, mid };
	shelf_scene_solid4(p, c);
	for (int i = 0; i < 70; i++) rect(rnd(i + 1) * w, rnd(i + 2) * h, 1 + rnd(i + 3) * 3, 1, with_alpha(0xff1e5078, .05f + rnd(i) * .06f));
	// one pitch for the ruled lines and the lines of text (ui_style.h UI_PAPER_LINE): every baseline is on a line
	rect(0, UI_ROOM_RULE_Y, w, 1.2f, shelf_scene_room->rule);
	for (float yy = UI_ROOM_RULE_Y + UI_PAPER_LINE; yy < h - UI_ROOM_RULE_FOOT; yy += UI_PAPER_LINE) rect(0, yy, w, 1, shelf_scene_room->rule_soft);
	rect(UI_ROOM_MARGIN_X, UI_ROOM_RULE_Y, 1, h - UI_ROOM_RULE_Y, with_alpha(shelf_scene_room->rule, .35f));
	text_on_paper = true;
}
static void card_end(void) { text_on_paper = false; C2D_ViewRestore(&card_saved); }
static void star(float x, float y, float r, u32 c)
{
	float p[10][2];
	for (int i = 0; i < 10; i++) {
		float a = -PI / 2 + i * PI / 5, rr = i % 2 ? r * .45f : r;
		p[i][0] = x + cosf(a) * rr; p[i][1] = y + sinf(a) * rr;
	}
	poly(p, 10, c, c, 0, 0, 0, 0);
}
static void stamp(float x, float y, float a)
{
	C2D_ViewSave(&shelf_scene_view_saved);
	C2D_ViewTranslate(x, y);
	C2D_ViewRotate(-.22f);
	u32 c = with_alpha(shelf_scene_room->stamp, a);
	float o[2][4] = { { -38, -12, 76, 24 }, { -34, -9, 68, 18 } };
	for (int k = 0; k < 2; k++) {
		float lw = k ? 1 : 2, X = o[k][0], Y = o[k][1], W = o[k][2], Hh = o[k][3];
		rect(X, Y, W, lw, c); rect(X, Y + Hh - lw, W, lw, c); rect(X, Y, lw, Hh, c); rect(X + W - lw, Y, lw, Hh, c);
	}
	shelf_scene_text_at(-6, 5, 11, c, C2D_AlignCenter, "FAVORITE");
	star(26, 1, 4, c);
	for (int i = 0; i < 40; i++) rect(-40 + rnd(i + 5) * 80, -14 + rnd(i + 9) * 28, 1.5f, 1.5f, with_alpha(shelf_scene_room->paper, .3f + rnd(i) * .5f));
	C2D_ViewRestore(&shelf_scene_view_saved);
}
static void clip(float x, float y)
{
	u32 c = 0xff5ea3c9;
	line(x + 7, y + 30, x + 7, y + 8, 2.2f, c);
	for (int i = 0; i < 6; i++) {
		float a0 = PI + i * PI / 6, a1 = PI + (i + 1) * PI / 6;
		line(x + 11 + cosf(a0) * 4, y + 8 + sinf(a0) * 4, x + 11 + cosf(a1) * 4, y + 8 + sinf(a1) * 4, 2.2f, c);
		line(x + 10 + cosf(a0 - PI) * 5, y + 34 + sinf(a0 - PI) * 5, x + 10 + cosf(a1 - PI) * 5, y + 34 + sinf(a1 - PI) * 5, 2.2f, c);
	}
	line(x + 15, y + 8, x + 15, y + 34, 2.2f, c);
	line(x + 5, y + 34, x + 5, y + 12, 2.2f, c);
	line(x + 6.4f, y + 26, x + 6.4f, y + 10, .8f, 0x99c8f0ff);
}
// the mini shelf's scale: the whole shelf, but never wider than 400 mm across 296 px
static float strip_len(const shelf_row *r) { return fmaxf(r->end - X0, 400); }
static float strip_x(const shelf_row *r, float x) { return 12 + (x - X0) / strip_len(r) * 296; }
int shelf_scene_strip_hit(const shelf_view *v, float x, float y)
{
	if (v->level != SHELF_ONE || y >= 44) return -1;
	const shelf_row *r = &v->rows[v->row];
	int best = -1;
	float bd = 1e9f;
	for (int i = 0; i < r->n; i++) {
		float d = fabsf(strip_x(r, r->box[i].x) - x);
		if (d < bd) { bd = d; best = i; }
	}
	return best;
}
static float home_row_y(const shelf_view *v, int i) { (void)v; return UI_ROOM_ROWS_Y + i * UI_ROOM_LIST_STEP; }
// rows that fit above the catalog's foot line (more rows: the foot line gives way)
static bool home_foot(const shelf_view *v) { return home_row_y(v, v->nrows) <= UI_ROOM_HINT_Y; }
int shelf_scene_card_hit(const shelf_view *v, float x, float y)
{
	(void)x;
	for (int i = 0; i < v->nrows; i++) {   // a row's band: between the ruled line above it and its own
		float ry = UI_ROOM_CARD_Y + home_row_y(v, i);
		if (y >= ry - UI_PAPER_LINE + 1 && y < ry + 1) return i;   // from the line above to its own line
	}
	return -1;
}

static void shelf_bottom(const shelf_view *v);
void shelf_scene_bottom(const shelf_view *v)
{
	shelf_scene_view = v;
	shelf_scene_room = theme_room(theme());
	if (!shelf_scene_room || !shelf_scene_prepare_gpu()) {
		C2D_DrawRectSolid(0, 0, Z, 320, 240, theme()->surface);
		return;
	}
	u64 t0 = svcGetSystemTick();
	C2D_TextBufClear(shelf_scene_state.text);
	depth_off(true);
	desk();
	shelf_bottom(v);
	depth_off(false);
	u64 dt = svcGetSystemTick() - t0;
	shelf_scene_state.ticks[1] += dt;
	room_pace_add(ROOM_PACE_BOTTOM, dt);
	if (dt > shelf_scene_state.peak[1]) shelf_scene_state.peak[1] = dt;
	if (!shelf_scene_state.since) shelf_scene_state.since = t0;
	if (++shelf_scene_state.frames >= 300) {
		double ms = 1000.0 / SYSCLOCK_ARM11;
		C2Di_Context *ctx = C2Di_GetContext();
		log_printf("shelf: top %.2f ms per frame (peak %.2f, %s), bottom %.2f ms (peak %.2f), %u vertices, %u frames in %.1f s\n",
			shelf_scene_state.ticks[0] * ms / shelf_scene_state.frames, shelf_scene_state.peak[0] * ms, shelf_scene_state.eyes ? "both eyes" : "one eye", shelf_scene_state.ticks[1] * ms / shelf_scene_state.frames, shelf_scene_state.peak[1] * ms,
			(unsigned)ctx->vtxBufPos, (unsigned)shelf_scene_state.frames, (t0 - shelf_scene_state.since) * ms / 1000);
		shelf_scene_state.ticks[0] = shelf_scene_state.ticks[1] = shelf_scene_state.peak[0] = shelf_scene_state.peak[1] = 0;
		shelf_scene_state.frames = shelf_scene_state.eyes = 0;
		shelf_scene_state.since = t0;
	}
}

// a list row's highlight and pointer (the catalog, the place card's lists): the shared highlighter stroke behind the
// row's text from x0 to x1 (drawn out when the row becomes the selection; `site` tells the two lists apart), and
// the pointer at its capitals
static void list_mark(float y, float x0, float x1, int row, int site)
{
	static int marked = -1;
	static u64 since;
	u64 now = svcGetSystemTick();
	if (marked != site * 256 + row) { marked = site * 256 + row; since = now; }
	float progress = (float)(now - since) / SYSCLOCK_ARM11 / UI_MARK_DRAW_S;
	ui_mark_row(x0, x1, y, row, progress, 1, Z);
	float m = y - UI_PAPER_CAP / 2.0f;
	tri(UI_ROOM_TEXT_X - 16, m - 5, 0xff1c5a8a, UI_ROOM_TEXT_X - 8, m, 0xff1c5a8a, UI_ROOM_TEXT_X - 16, m + 5, 0xff1c5a8a);
}
// the width text_fit draws s at
static float text_fit_w(float width, float px, float min_px, const char *s)
{
	float scale;
	const char *t = ui_fit_ex(NULL, s, text_px(px), text_px(min_px), width, &scale);
	return t[0] ? ui_text_width(NULL, scale, t) : 0;
}
// an info row: the label from the left, its value right-aligned (the label keeps at least UI_ROOM_LABEL_MAX of
// the line when both are long; both are fitted at the floor and cut with "...")
static void info_row(float y, const char *l, const char *v, u32 c)
{
	const float tw = UI_ROOM_TEXT_R - UI_ROOM_TEXT_X;
	if (!v) { if (l) text_fit(UI_ROOM_TEXT_X, y, tw, UI_ROOM_LINE_PX, UI_ROOM_MIN_PX, c, l); return; }
	float lw = l ? text_w(UI_ROOM_LINE_PX, l) : 0, vw = text_w(UI_ROOM_VALUE_PX, v), room_v = tw - UI_ROOM_GAP - fminf(lw, tw * UI_ROOM_LABEL_MAX);
	if (vw > room_v) vw = room_v;
	if (l) text_fit(UI_ROOM_TEXT_X, y, tw - UI_ROOM_GAP - vw, UI_ROOM_LINE_PX, UI_ROOM_MIN_PX, c, l);
	text_fit_at(UI_ROOM_TEXT_R, y, vw, UI_ROOM_VALUE_PX, UI_ROOM_MIN_PX, c, C2D_AlignRight, v);
}
static void shelf_bottom(const shelf_view *v)
{
	u32 ink = shelf_scene_room->ink, soft = shelf_scene_room->ink_soft, red = with_alpha(shelf_scene_room->rule, 1);
	bool notice = v->notice && v->t - v->notice_t < 2.5f;
	const float tw = UI_ROOM_TEXT_R - UI_ROOM_TEXT_X;
	if (v->level == SHELF_ALL || !v->nrows) {
		card_begin(UI_ROOM_CARD_X, UI_ROOM_CARD_Y, UI_ROOM_CARD_W, UI_ROOM_CARD_H, -.012f);
		card_title("Collection", tw);
		for (int i = 0; i < v->nrows; i++) {
			float y = home_row_y(v, i);
			bool on = i == v->hsel;
			float x = UI_ROOM_TEXT_X + (v->rows[i].system ? 0 : UI_ROOM_STAR_W);
			if (on) list_mark(y, UI_ROOM_TEXT_X, x + text_fit_w(UI_ROOM_TEXT_R - x, UI_ROOM_ROW_PX, UI_ROOM_MIN_PX, v->rows[i].name), i, 0);
			if (!v->rows[i].system) star(UI_ROOM_TEXT_X + 6, y - UI_ROOM_STAR_UP, UI_ROOM_STAR_R, on ? ink : soft);
			text_fit(x, y, UI_ROOM_TEXT_R - x, UI_ROOM_ROW_PX, UI_ROOM_MIN_PX, on ? ink : soft, v->rows[i].name);   // no count: nothing to act on
		}
		float free_y = home_row_y(v, v->nrows);   // under the last row
		if (!v->total && !notice) {   // an empty library (first run, or the folder holds no games): what to do, for every system
			char gone[PATH_LEN + 24];
			snprintf(gone, sizeof(gone), "Can't read %s", bb_unreadable_dir);
			char where[96];
			ui_empty_help(where, sizeof(where), g_cfg.games_dir, true);   // where games go, and Y (the bar's rescan) to look again
			const char *help = bb_unreadable_dir[0] ? gone : where;   // the saved folder is kept, not forgotten
			int room_lines = (int)((UI_ROOM_HINT_Y - UI_ROOM_LINE_STEP - free_y) / UI_ROOM_LINE_STEP) + 1;
			text_lines(UI_ROOM_TEXT_X, free_y, UI_ROOM_LINE_STEP, tw, UI_ROOM_LINE_PX, room_lines < 1 ? 1 : room_lines, ink, help);
		}
		// the foot line only for something to act on: a notice, or a folder that cannot be read (no counts, no
		// "Last played": owner 2026-11-05)
		char last[PATH_LEN + 32];
		last[0] = 0;
		if (notice) snprintf(last, sizeof(last), "%s", v->notice);
		else if (bb_unreadable_dir[0] && v->total) {
			// the saved folder's end (whole characters); Rescan and Settings are on the bar
			char tail[24 * 4 + 8];   // 24 whole characters of up to 4 bytes, "..." and the end
			ui_path_tail(tail, sizeof(tail), bb_unreadable_dir, 24);
			snprintf(last, sizeof(last), "Can't read: %s", tail);
		}
		if (last[0] && (home_foot(v) || notice)) text_fit(UI_ROOM_TEXT_X, UI_ROOM_HINT_Y, tw, UI_ROOM_HINT_PX, UI_ROOM_MIN_PX, red, last);
		card_end();
		return;
	}
	const shelf_row *row = &v->rows[v->row];
	// the mini shelf: every spine at scale, a brass pointer under the chosen one (no letters: too small to read)
	rect(8, 6, 304, 34, 0x66000000);
	for (int i = 0; i < row->n; i++) {
		const shelf_box *b = &row->box[i];
		float x = strip_x(row, b->x - b->t / 2), bw = fmaxf(2, b->t / strip_len(row) * 296 - .6f), hh = b->h * .19f;
		bool on = i == v->sel[v->row];
		u32 c = box_color(b);
		rect(x, 34 - hh - (on ? 3 : 0), bw, hh, shelf_scene_rgba(ch(c, 0) * .7f + 20, ch(c, 1) * .7f + 20, ch(c, 2) * .7f + 20, 255));
		if (on) tri(x + bw / 2, 35, shelf_scene_room->brass, x + bw / 2 - 3, 40, shelf_scene_room->brass, x + bw / 2 + 3, 40, shelf_scene_room->brass);
	}
	vgrad(8, 34, 304, 3, shelf_scene_shade(shelf_scene_room->wood, .85f), shelf_scene_shade(shelf_scene_room->wood, .45f));
	card_begin(UI_ROOM_GAME_X, UI_ROOM_GAME_Y, UI_ROOM_GAME_W, UI_ROOM_GAME_H, -.014f);
	if (!row->n) {
		card_title("Nothing here yet", tw);
		text_lines(UI_ROOM_TEXT_X, UI_ROOM_ROWS_Y, UI_ROOM_LINE_STEP, tw, UI_ROOM_LINE_PX, 2, soft, "Games you star wait here for you.");
		card_end();
		clip(28, 40);
		return;
	}
	const shelf_box *b = &row->box[v->sel[v->row]];
	// the title in the header row (one line: smaller down to the floor, then cut with "..."; the focused game's whole
	// name is on its box above), then the foot line: no system kicker, no "Last played" / "Play time" / "Never played"
	card_title(b->title, tw);
	bool foot = true;
	if (b->fav) stamp(UI_ROOM_STAMP_X, UI_ROOM_STAMP_Y, .82f);   // over the foot line's right end, clear of the rows
	if (notice) text_fit(UI_ROOM_TEXT_X, UI_ROOM_GAME_HINT_Y, tw, UI_ROOM_HINT_PX, UI_ROOM_MIN_PX, red, v->notice);
	else if (foot) {   // the L and R glyphs and what they do; nothing when they do nothing (no hands to turn the box)
		bool out = v->pto == 1 || v->pull > .002f;
		const char *lr = out ? (shelf_scene_hands_open() ? UI_HINT_LR_TURN : NULL) : UI_HINT_LR_JUMP;
		if (lr) text_fit(UI_ROOM_TEXT_X, UI_ROOM_GAME_HINT_Y, tw, UI_ROOM_HINT_PX, UI_ROOM_MIN_PX, soft, lr);
	}
	card_end();
	clip(28, 40);
}

// The cold start (k 0..1): the room comes up out of the dark, the lamp first. The black cover has a
// lit disc round the floor lamp that grows until it holds the whole screen (10 x 6 quads of 40 px,
// each corner's alpha from its distance to the lamp).
void shelf_scene_intro(float k)
{
	if (k >= 1) return;
	const float lx = 150, ly = 62, soft = 150;
	float r = soft + 20 + ui_motion_ease(k) * 300;   // lit round the lamp from the first frame, the farthest corner (307 px from it) at the end
	float a[7][11];
	for (int j = 0; j < 7; j++)
		for (int i = 0; i < 11; i++) {
			float x = i * 40, y = j * 40, d = sqrtf((x - lx) * (x - lx) + (y - ly) * (y - ly));
			a[j][i] = clampf((d - (r - soft)) / soft, 0, 1);
		}
	for (int j = 0; j < 6; j++)
		for (int i = 0; i < 10; i++) {
			float x = i * 40, y = j * 40;
			const float p[4][2] = { { x, y }, { x + 40, y }, { x + 40, y + 40 }, { x, y + 40 } };
			u32 c[4] = { with_alpha(0xff000000, a[j][i]), with_alpha(0xff000000, a[j][i + 1]),
				with_alpha(0xff000000, a[j + 1][i + 1]), with_alpha(0xff000000, a[j + 1][i]) };
			shelf_scene_solid4(p, c);
		}
}

// The app's first picture, drawn as soon as the screens work, long before the room can be: the dark
// with the floor lamp's glow where the lamp will be (shelf_scene_intro(0) over the lamp's warm light),
// the desk's dark wood below. The room's own lamp-first fade-up takes over from the same picture.
void shelf_scene_dawn(bool top)
{
	if (!top) {
		rect(0, 0, 320, 240, shelf_scene_rgba(26, 15, 8, 255));
		return;
	}
	rect(0, 0, 400, 240, shelf_scene_rgba(112, 68, 30, 255));
	shelf_scene_intro(0);
}

// The room's bottom screen (shelf.h shelf_card): the desk, one card, as R8's
// catalogue card above, so the room and the bookshelf share one look.
// The focused place's sign at home (UI mock R10 focusTag): a paper tag hanging over the
// place on a thread, its name and one line, swaying a little; k fades it in.
void shelf_scene_card(const shelf_card *c)
{
	shelf_scene_room = theme_room(theme());
	if (!shelf_scene_room || !shelf_scene_prepare_gpu()) {
		C2D_DrawRectSolid(0, 0, Z, 320, 240, theme()->surface);
		return;
	}
	u32 ink = shelf_scene_room->ink, soft = shelf_scene_room->ink_soft;
	const float tw = UI_ROOM_TEXT_R - UI_ROOM_TEXT_X;
	C2D_TextBufClear(shelf_scene_state.text);
	depth_off(true);
	desk();
	card_begin(UI_ROOM_CARD_X, UI_ROOM_CARD_Y, UI_ROOM_CARD_W, UI_ROOM_CARD_H, -.012f);
	card_title(c->title, c->picture ? UI_ROOM_TITLE_W_PICTURE : tw);   // clear of the picture's tape
	for (int i = 0; c->pages > 1 && i < c->pages; i++) {   // the page dots on the foot line: this page filled, the others rings
		float x = UI_ROOM_TEXT_R - (c->pages - 1 - i) * UI_ROOM_DOT_STEP - UI_ROOM_DOT_R, y = UI_ROOM_DOT_Y;
		C2D_DrawCircleSolid(x, y, Z, UI_ROOM_DOT_R, ink);
		if (i + 1 != c->page) C2D_DrawCircleSolid(x, y, Z, UI_ROOM_DOT_R - 1.2f, shelf_scene_room->paper);
	}
	if (c->picture) {   // the save picture, taped to the card
		static const Tex3DS_SubTexture sub = { 256, 192, 0, 1, 1, 1 - 192 / 256.0f };
		const float px = UI_ROOM_PICTURE_X, py = UI_ROOM_PICTURE_Y, pw = UI_ROOM_PICTURE_W, ph = pw * 3 / 4;
		rect(px - 2, py - 2, pw + 4, ph + 4, 0xff201810);
		C2D_DrawImageAt((C2D_Image){ c->picture, &sub }, px, py, Z, NULL, pw / 256.0f, ph / 192.0f);
		if (c->picture_label) {
			rect(px, py + ph / 2 - UI_PAPER_LINE / 2, pw, UI_PAPER_LINE, 0x99000000);   // on the picture, not the paper
			shelf_scene_text_at(px + pw / 2, py + ph / 2 + UI_PAPER_CAP / 2, UI_ROOM_TINY_PX, 0xffffffff, C2D_AlignCenter, c->picture_label);
		}
		rect(px + 15, py - 5, 30, 7, with_alpha(shelf_scene_room->paper_edge, .8f));   // tape
	}
	float y = UI_ROOM_ROWS_Y;
	for (int i = 0; i < c->nrows && i < SHELF_CARD_ROWS; i++) {
		const char *l = c->row[i].label, *v = c->row[i].value;
		if (c->list) {   // R8's catalogue rows
			bool on = c->row[i].on;
			float x = UI_ROOM_TEXT_X + (c->row[i].star ? UI_ROOM_STAR_W : 0);
			float vw = v ? text_w(UI_ROOM_VALUE_PX, v) + UI_ROOM_GAP : 0;
			if (on) list_mark(y, UI_ROOM_TEXT_X, v ? UI_ROOM_TEXT_R : x + (l ? text_fit_w(UI_ROOM_TEXT_R - vw - x, UI_ROOM_ROW_PX, UI_ROOM_MIN_PX, l) : 0), i, 1);
			if (c->row[i].star) star(UI_ROOM_TEXT_X + 6, y - UI_ROOM_STAR_UP, UI_ROOM_STAR_R, on ? ink : soft);
			if (l) text_fit(x, y, UI_ROOM_TEXT_R - vw - x, UI_ROOM_ROW_PX, UI_ROOM_MIN_PX, on ? ink : soft, l);
			if (v) shelf_scene_text_at(UI_ROOM_TEXT_R, y, UI_ROOM_VALUE_PX, soft, C2D_AlignRight, v);
			y += UI_ROOM_LIST_STEP;
		} else {
			if (c->row[i].icon)   // the row's icon in the margin, centred on its capitals
				ui_icon_draw(c->row[i].icon, UI_ROOM_MARGIN_X / 2.0f, y - UI_PAPER_CAP / 2.0f, UI_ROOM_ICON_PX, soft, shelf_scene_room->paper);
			info_row(y, l, v, soft);
			y += UI_ROOM_LINE_STEP;
		}
	}
	if (c->hint && y <= UI_ROOM_HINT_Y - UI_ROOM_LINE_STEP)   // up to two lines (the empty library's help), ending on the foot line
		text_lines(UI_ROOM_TEXT_X, text_break(c->hint, tw, UI_ROOM_HINT_PX) ? UI_ROOM_HINT_Y - UI_ROOM_LINE_STEP : UI_ROOM_HINT_Y, UI_ROOM_LINE_STEP, tw, UI_ROOM_HINT_PX, 2, soft, c->hint);
	card_end();
	depth_off(false);
}

PSX3D_COLD float shelf_scene_line_width(const char *s) { return text_w(UI_ROOM_LINE_PX, s); }

// The desk the room's cards lie on, without a card (the Continue postcard, postcards.inc). Nothing is written
// to the depth buffer: what is drawn next lies on it. load false: never loads the room's textures (a game's
// Continue question comes after shelf_scene_release; the game needs that memory): without them the desk is its
// colour without the grain, lit as desk() lights it (corners measured on a real room still).
void shelf_scene_desk(bool load)
{
	shelf_scene_room = theme_room(theme());
	bool grain = shelf_scene_room && (load ? shelf_scene_prepare_gpu() : shelf_scene_state.ready);
	depth_off(true);
	if (grain) desk();
	else if (shelf_scene_room) C2D_DrawRectangle(0, 0, Z, 320, 240, shelf_scene_shade(shelf_scene_room->desk, .68f), shelf_scene_shade(shelf_scene_room->desk, .42f), shelf_scene_shade(shelf_scene_room->desk, .62f), shelf_scene_shade(shelf_scene_room->desk, .55f));
	else C2D_DrawRectSolid(0, 0, Z, 320, 240, theme()->surface);
	depth_off(false);
}

// Other menus in a room theme (settings, dialogs): the room's wall above and
// the desk below, with the lamp's light.
void shelf_scene_backdrop(bool bottom)
{
	shelf_scene_room = theme_room(theme());
	if (!shelf_scene_room || !shelf_scene_prepare_gpu()) {
		C2D_DrawRectSolid(0, 0, Z, bottom ? 320 : 400, 240, theme()->surface);
		return;
	}
	if (bottom) {
		desk();
		rect(0, 0, 320, 240, 0x40000000);
		return;
	}
	const float p[4][2] = { { 0, 0 }, { 400, 0 }, { 400, 240 }, { 0, 240 } };
	const float uv[4][2] = { { uv_u(G_WALL, 0), uv_v(G_WALL, 0) }, { uv_u(G_WALL, 1), uv_v(G_WALL, 0) },
		{ uv_u(G_WALL, 1), uv_v(G_WALL, 1) }, { uv_u(G_WALL, 0), uv_v(G_WALL, 1) } };
	const u32 c[4] = { shelf_scene_shade(shelf_scene_room->wall, .8f), shelf_scene_shade(shelf_scene_room->wall, .8f), shelf_scene_shade(shelf_scene_room->wall, .7f), shelf_scene_shade(shelf_scene_room->wall, .7f) };
	shelf_scene_tquad(&shelf_scene_state.grain, p, uv, c);
	vgrad(0, 206, 400, 34, shelf_scene_shade(shelf_scene_room->floor, .6f), shelf_scene_shade(shelf_scene_room->floor, .4f));
	rect(0, 204, 400, 4, shelf_scene_room->skirting);
	sprite(&shelf_scene_state.glow, 90, 70, 300, 300, with_alpha(shelf_scene_room->light, .3f));
	sprite(&shelf_scene_state.dark, 160, 100, 340, 340, shelf_scene_room->shade);
}
