#include "shelf_scene_internal.h"

static int room_lookahead(int r, const int *visible, int n);
static int room_target_spines(int r, int *indices, int cap);
// ---------------------------------------------------------------- close-up spine cells
// The shelf whose real spines are built: the one the camera is close to.
// In the room the highlighted section's spines are baked already on the wall view.
static int close_row(void)
{
	if (!shelf_scene_view->nrows) return -1;
	return shelf_scene_view->level == SHELF_ALL && shelf_scene_view->zoom < .01f ? (shelf_scene_view->hsel < shelf_scene_view->nrows ? shelf_scene_view->hsel : -1) : shelf_scene_view->row < shelf_scene_view->nrows ? shelf_scene_view->row : -1;
}
static int px_of(float mm, float s) { return (int)lroundf(mm * s); }
// Keep enough cells for the draw bound, a held case, and lookahead.
// Oversized rows use compact title strips rather than starving visible spines.
static void cells_for(int r)
{
	unsigned key = shelf_scene_view->layout;
	if (shelf_scene_state.cell_row == r && shelf_scene_state.cell_key == key) return;
	const shelf_row *row = &shelf_scene_view->rows[r];
	float tmax = 1;
	for (int i = 0; i < row->n; i++) tmax = fmaxf(tmax, row->box[i].t);
	shelf_scene_state.cw = K * px_of(tmax, row->s);
	shelf_scene_state.ch = K * px_of(row->hmax, row->s);
	if (shelf_scene_state.cw < K) shelf_scene_state.cw = K;
	if (shelf_scene_state.cw > ATLAS) shelf_scene_state.cw = ATLAS;
	// Favorites can be empty, including when walking back to the wall.
	// Keep the atlas divisor valid even when that row has no box height.
	if (shelf_scene_state.ch < K) shelf_scene_state.ch = K;
	if (shelf_scene_state.ch > ATLAS) shelf_scene_state.ch = ATLAS;
	int rows = ATLAS / shelf_scene_state.ch;
	int need = row->n < ROOM_BOXES + 2 ? row->n : ROOM_BOXES + 2;
	if (need < 1) need = 1;
	int need_cols = (need + rows * shelf_scene_state.atlases - 1) / (rows * shelf_scene_state.atlases);
	int limit = ATLAS / need_cols;
	shelf_scene_state.compact = shelf_scene_state.cw > limit;
	if (shelf_scene_state.compact) shelf_scene_state.cw = limit;
	shelf_scene_state.cols = ATLAS / shelf_scene_state.cw;
	shelf_scene_state.per_atlas = shelf_scene_state.cols * (ATLAS / shelf_scene_state.ch);
	shelf_scene_state.cells = shelf_scene_state.per_atlas * shelf_scene_state.atlases;
	if (shelf_scene_state.cells > MAX_CELLS) shelf_scene_state.cells = MAX_CELLS;
	assert(shelf_scene_state.cells >= need);
	memset(shelf_scene_state.cell, 0, sizeof(shelf_scene_state.cell));
	shelf_scene_state.cell_row = r;
	shelf_scene_state.cell_key = key;
}
static int spine_width(const shelf_box *b) { int w = K * px_of(b->t, shelf_scene_view->rows[shelf_scene_state.cell_row].s); return w < shelf_scene_state.cw ? w : shelf_scene_state.cw; }
int shelf_scene_cell_of(const shelf_box *b)
{
	if (shelf_scene_state.cell_row < 0 || shelf_scene_state.cell_row != close_row()) return -1;   // cells belong to one shelf
	for (int i = 0; i < shelf_scene_state.cells; i++)
		if (shelf_scene_state.cell[i].system == b->system && shelf_scene_state.cell[i].row == b->row && shelf_scene_state.cell[i].used) return i;
	return -1;
}
static void cell_rect(int c, int *x, int *y)
{
	int k = c % shelf_scene_state.per_atlas;
	*x = k % shelf_scene_state.cols * shelf_scene_state.cw;
	*y = k / shelf_scene_state.cols * shelf_scene_state.ch;
}
// a box's spine in its cell: whole close-up pixels, K texels each
void shelf_scene_spine_uv(int cell, const shelf_box *b, float *u0, float *v0, float *u1, float *v1)
{
	int x, y;
	float s = shelf_scene_view->rows[shelf_scene_state.cell_row].s;
	cell_rect(cell, &x, &y);
	*u0 = (float)x / ATLAS;
	*u1 = (float)(x + spine_width(b)) / ATLAS;
	// render targets keep the first framebuffer row at the texture's top
	*v0 = 1 - (float)y / ATLAS;
	*v1 = 1 - (float)(y + K * px_of(b->h, s)) / ATLAS;
}


// ---------------------------------------------------------------- spine bake
static int cell_for(const shelf_box *b)
{
	(void)b;
	int lru = 0;
	for (int i = 0; i < shelf_scene_state.cells; i++) {
		if (!shelf_scene_state.cell[i].used) return i;
		if (shelf_scene_state.cell[i].used < shelf_scene_state.cell[lru].used) lru = i;
	}
	return shelf_scene_state.cells && shelf_scene_state.cell[lru].used != shelf_scene_state.frame ? lru : -1;
}
// A colour with its HSL lightness kept in [lo, hi] (hue and saturation kept).
static u32 clamp_lightness(u32 c, float lo, float hi)
{
	float r = ch(c, 0) / 255.0f, g = ch(c, 1) / 255.0f, b = ch(c, 2) / 255.0f;
	float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), l = (mx + mn) / 2, d = mx - mn;
	if (l >= lo && l <= hi) return c;
	float sat = d > 0 ? fminf(1, d / (1 - fabsf(2 * l - 1))) : 0, h = 0;   // h in sixths of the circle
	if (d > 0) h = mx == r ? fmodf((g - b) / d + 6, 6) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
	l = clampf(l, lo, hi);
	float C = (1 - fabsf(2 * l - 1)) * sat, X = C * (1 - fabsf(fmodf(h, 2) - 1)), m = l - C / 2, o[3] = { 0, 0, 0 };
	switch ((int)h % 6) {
	case 0: o[0] = C; o[1] = X; break;
	case 1: o[0] = X; o[1] = C; break;
	case 2: o[1] = C; o[2] = X; break;
	case 3: o[1] = X; o[2] = C; break;
	case 4: o[0] = X; o[2] = C; break;
	default: o[0] = C; o[2] = X; break;
	}
	return shelf_scene_rgba((o[0] + m) * 255 + .5f, (o[1] + m) * 255 + .5f, (o[2] + m) * 255 + .5f, ch(c, 3));
}
// The cover's colours in one pass over every other texel of the picture (the top-left
// part of the texture): b->color its average (box sides, plain spines), b->accent its
// dominant saturated colour (carton spines): twelve hue bins weighted by saturation x
// value, the top bin's weighted mean, lightness kept in 18-82 %; a grey cover: its average.
#define HUE_BINS 12
static void average_color(shelf_box *b, C3D_Tex *t, const Tex3DS_SubTexture *s)
{
	const u16 *px = t->data;
	u32 w = (u32)(s->right * t->width + .5f), h = (u32)((1 - s->bottom) * t->height + .5f), r = 0, g = 0, bl = 0, k = 0;
	float bin[HUE_BINS][4] = { { 0 } }, total = 0;
	for (u32 y = 0; y < h && y < t->height; y += 2)
		for (u32 x = 0; x < w && x < t->width; x += 2, k++) {
			u16 c = px[texel(x, y, t->width)];
			int cr = (c >> 11) & 31, cg = (c >> 5) & 63, cb = c & 31;
			r += cr; g += cg; bl += cb;
			float fr = cr / 31.0f, fg = cg / 63.0f, fb = cb / 31.0f;
			float mx = fmaxf(fr, fmaxf(fg, fb)), mn = fminf(fr, fminf(fg, fb)), d = mx - mn;
			if (d <= 0) continue;
			float wt = d;   // saturation (d / mx) x value (mx)
			float hh = mx == fr ? fmodf((fg - fb) / d + 6, 6) : mx == fg ? (fb - fr) / d + 2 : (fr - fg) / d + 4;
			int i = (int)(hh * HUE_BINS / 6) % HUE_BINS;
			bin[i][0] += wt; bin[i][1] += wt * fr; bin[i][2] += wt * fg; bin[i][3] += wt * fb;
			total += wt;
		}
	if (k) b->color = shelf_scene_rgba(r * 255 / 31 / k, g * 255 / 63 / k, bl * 255 / 31 / k, 255);
	if (!b->color) b->color = 0xff010101;
	int top = 0;
	for (int i = 1; i < HUE_BINS; i++) if (bin[i][0] > bin[top][0]) top = i;
	u32 a = k && total > .06f * k && bin[top][0] > 0
		? shelf_scene_rgba(bin[top][1] / bin[top][0] * 255, bin[top][2] / bin[top][0] * 255, bin[top][3] / bin[top][0] * 255, 255) : b->color;
	b->accent = clamp_lightness(a, .18f, .82f) | 0xff000000u;
}
static float even(float v) { return K * roundf(v / K); }   // whole close-up pixels
#define INK_CENTER .48f    // where the middle of the letters sits in the line box, from its top
// The maker's logo of a system, as a small texture, once it is on the SD card
// (covers_logo: fetched through the cover downloader, never part of the app).
// NULL until then and offline: spines then carry the maker's name as plain text.
static const logo_t *logo_of(const bb_system_t *sys)
{
	if (!sys || !sys->logo_url) return NULL;
	logo_t *free_slot = NULL;
	for (int i = 0; i < LOGOS; i++) {
		if (shelf_scene_state.logos[i].system == sys) return shelf_scene_state.logos[i].ok ? &shelf_scene_state.logos[i] : NULL;
		if (!shelf_scene_state.logos[i].system && !free_slot) free_slot = &shelf_scene_state.logos[i];
	}
	int w, h;
	const u32 *px = free_slot ? covers_logo(sys, &w, &h) : NULL;   // the first call asks the downloader
	if (!px) return NULL;
	int tw = 8, th = 8;
	while (tw < w) tw <<= 1;
	while (th < h) th <<= 1;
	if (!C3D_TexInit(&free_slot->tex, tw, th, GPU_RGBA8)) return NULL;
	free_slot->system = sys;
	u32 *d = free_slot->tex.data;
	memset(d, 0, (size_t)tw * th * 4);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) d[texel(x, y, tw)] = px[y * w + x];
	C3D_TexSetFilter(&free_slot->tex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&free_slot->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	svcStoreProcessDataCache(CUR_PROCESS_HANDLE, (u32)free_slot->tex.data, free_slot->tex.size);
	free_slot->u = (float)w / tw;
	free_slot->v = 1 - (float)h / th;   // the picture sits in the texture's top left, as the covers do
	free_slot->aspect = (float)w / h;
	free_slot->ok = true;
	return free_slot;
}
// the system's downloaded logo centred at cx, cy within w x h; false: none (yet)
static bool spine_logo(const bb_system_t *sys, float cx, float cy, float w, float h)
{
	const logo_t *l = logo_of(sys);
	if (!l) return false;
	float lw = w, lh = w / l->aspect;
	if (lh > h) { lh = h; lw = h * l->aspect; }
	const float p[4][2] = { { cx - lw / 2, cy - lh / 2 }, { cx + lw / 2, cy - lh / 2 }, { cx + lw / 2, cy + lh / 2 }, { cx - lw / 2, cy + lh / 2 } };
	const float uv[4][2] = { { 0, 1 }, { l->u, 1 }, { l->u, l->v }, { 0, l->v } };
	const u32 white[4] = { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff };
	shelf_scene_tquad((C3D_Tex *)&l->tex, p, uv, white);
	return true;
}
// Rotated text down the spine (like the title) centred in [y0, y0 + len], at most px
// close-up pixels high; squeezed along the spine (to half) and then made smaller to fit.
// outline: a 1 px ring of that colour around the letters (0 none).
static void spine_text(const char *s, float cx, float y0, float len, float px, u32 ink, u32 outline)
{
	C2D_Text t;
	C2D_TextBufClear(shelf_scene_state.text);
	if (!s || !*s || !C2D_TextFontParse(&t, NULL, shelf_scene_state.text, s)) return;
	C2D_TextOptimize(&t);
	float sc = text_px(px * K), sx = 1, tw = 0;
	C2D_TextGetDimensions(&t, sc, sc, &tw, NULL);
	if (tw <= 0) return;
	if (tw > len) {
		float k = len / tw;
		if (k >= .5f) sx = k;
		else { sx = .5f; sc *= k / .5f; }
		tw = len;
	}
	float lh = 30 * sc, off = -even(lh * INK_CENTER);
	C2D_ViewSave(&shelf_scene_view_saved);
	C2D_ViewTranslate(cx, even(y0 + (len - tw) / 2));
	C2D_ViewRotate(PI / 2);
	if (outline)
		for (int d = 0; d < 4; d++)
			C2D_DrawText(&t, C2D_WithColor, (d == 0 ? -K : d == 1 ? K : 0), off + (d == 2 ? -K : d == 3 ? K : 0), Z, sc * sx, sc, outline);
	C2D_DrawText(&t, C2D_WithColor, 0, off, Z, sc * sx, sc, ink);
	C2D_ViewRestore(&shelf_scene_view_saved);
}
// Short text across the spine (upright), centred at cx, cy within w x h.
static void spine_across(const char *s, float cx, float cy, float w, float h, u32 ink)
{
	C2D_Text t;
	C2D_TextBufClear(shelf_scene_state.text);
	if (!C2D_TextFontParse(&t, NULL, shelf_scene_state.text, s)) return;
	C2D_TextOptimize(&t);
	float px = fminf(h / K * .9f, 9), sc = text_px(px * K), tw;
	C2D_TextGetDimensions(&t, sc, sc, &tw, NULL);
	if (tw > w * .86f) { sc *= w * .86f / tw; tw = w * .86f; }
	C2D_DrawText(&t, C2D_WithColor, even(cx - tw / 2), even(cy - 15 * sc), Z, sc, sc, ink);
}
// The title down the spine (letter tops toward the front cover), centred on
// the spine's middle `mid` (kept inside [y0, y0 + len]), as large as the spine allows.
// A long title is made smaller (to 10 px), then squeezed along the spine (to 60 %),
// and only then cut short. outline: a 1 px ring around the letters (0 none).
static void spine_title(const char *title, float cx, float mid, float y0, float len, float w, u32 ink, u32 outline)
{
	float px = clampf(w / K - 8, 8, 15), sc = text_px(px * K), sx = 1, tw = 0;
	char s[SHELF_TITLE + 4];
	snprintf(s, sizeof(s), "%s", title);
	C2D_Text t;
	C2D_TextBufClear(shelf_scene_state.text);
	if (!C2D_TextFontParse(&t, NULL, shelf_scene_state.text, s)) return;
	C2D_TextGetDimensions(&t, sc, sc, &tw, NULL);
	if (tw > len) {
		float least = fminf(px, 10);
		if (px * len / tw >= least) px *= len / tw;
		else if (least / px * .6f * tw <= len) { sx = len / (tw * least / px); px = least; }
		else {
			int n = (int)strlen(title), cut = (int)(n * len / (tw * .6f * least / px)) - 2;
			px = least;
			sx = .6f;
			for (cut = cut < n ? cut : n; cut > 1; cut--) {
				while (cut > 1 && title[cut - 1] == ' ') cut--;
				snprintf(s, sizeof(s), "%.*s...", cut, title);
				C2D_TextBufClear(shelf_scene_state.text);
				if (!C2D_TextFontParse(&t, NULL, shelf_scene_state.text, s)) return;
				C2D_TextGetDimensions(&t, text_px(px * K), text_px(px * K), &tw, NULL);
				if (tw * sx <= len) break;
			}
		}
		sc = text_px(px * K);
		C2D_TextGetDimensions(&t, sc, sc, &tw, NULL);
	}
	tw *= sx;
	C2D_TextOptimize(&t);
	float lh = 30 * sc, start = even(clampf(mid - tw / 2, y0, y0 + len - tw));
	C2D_ViewSave(&shelf_scene_view_saved);
	C2D_ViewTranslate(cx, start);
	C2D_ViewRotate(PI / 2);
	// across the spine: the letters' ink (caps and x-height, not the empty line box) is centred
	float off = -even(lh * INK_CENTER);
	if (outline)
		for (int d = 0; d < 4; d++)
			C2D_DrawText(&t, C2D_WithColor, (d == 0 ? -K : d == 1 ? K : 0), off + (d == 2 ? -K : d == 3 ? K : 0), Z, sc * sx, sc, outline);
	C2D_DrawText(&t, C2D_WithColor, 0, off, Z, sc * sx, sc, ink);
	C2D_ViewRestore(&shelf_scene_view_saved);
}
// A real spine scan of the game, fetched at runtime like the covers (gba-spine-sources.md).
// Hook for a later round: none yet, every spine is drawn.
static const C3D_Tex *spine_scan(const shelf_box *b)
{
	(void)b;
	return NULL;
}
// The maker's logo block at the head of a PlayStation spine (0-11 %): the downloaded
// logo, else the name as plain text down the spine like the title (nothing Sony shipped).
static void spine_head_block(const shelf_box *b, float x, float y, float w, float h, float scale, u32 ink)
{
	float cx = x + w / 2, top = h * .11f;
	if (!spine_logo(b->system, cx, y + top / 2, w * .8f * scale, top * .8f * scale))
		spine_text("PlayStation", cx, y + top * (.5f - .4f * scale), top * .8f * scale, fminf(w / K * .45f, 8) * scale, ink, 0);
}
// an opaque grey the RGB565 atlas holds exactly (v a multiple of 8)
static u32 grey565(unsigned v) { return 0xff000000u | v << 16 | v << 8 | v; }
// The black case's plastic in exact greys (room.h ROOM_JEWEL_*): four bands down, darker rounded sides
static void jewel_black(float x, float y, float w, float h)
{
	static const unsigned char mid[4] = ROOM_JEWEL_MID, side[4] = ROOM_JEWEL_SIDE;
	float l = even(w * .25f), r = even(w * .2f);
	for (int i = 0; i < 4; i++) {
		float y0 = y + even(h * i / 4), y1 = i == 3 ? y + h : y + even(h * (i + 1) / 4);
		rect(x, y0, l, y1 - y0, grey565(side[i]));
		rect(x + l, y0, w - l - r, y1 - y0, grey565(mid[i]));
		rect(x + w - r, y0, r, y1 - y0, grey565(side[i]));
	}
}
// One spine into its cell, K texels per close-up pixel. Jewel
// cases by the game's region: North America black, PAL black or dark blue with a PAL tag
// and a light foot band, Japan a white spine card with the product code at the foot.
// Cartons: a real spine scan when there is one, else one solid colour from the cover's
// dominant saturated colour, the title, and GAME BOY ADVANCE as plain text at the foot.
// No slot numbers on any box. Called with the atlas bound.
static void bake(shelf_box *b, int cell, C3D_Tex *cover, const Tex3DS_SubTexture *cs)
{
	(void)cover; (void)cs;
	const bb_package *pk = b->system->package;
	bool cased = pk && pk->style == BB_PACKAGE_CASE;
	float s = shelf_scene_view->rows[shelf_scene_state.cell_row].s;
	int cx0, cy0;
	cell_rect(cell, &cx0, &cy0);
	float x = cx0, y = cy0, w = spine_width(b), h = K * px_of(b->h, s), cx = x + w / 2;
	const float p[4][2] = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h } };
	const C3D_Tex *scan = spine_scan(b);
	if (scan) {
		const float uv[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
		const u32 white[4] = { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff };
		shelf_scene_tquad((C3D_Tex *)scan, p, uv, white);
		return;
	}
	if (!cased) {
		u32 c = b->accent ? b->accent : clamp_lightness(placeholder(b, 1), .18f, .82f);
		u32 ink = shelf_carton_ink(c);
		rect(x, y, w, h, c);   // printed ink: no white wash or clipped highlight
		u32 edge = shelf_scene_shade(c, .82f);   // the cardboard's folded long edges
		rect(x, y, K, h, edge);
		rect(x + w - K, y, K, h, edge);
		spine_title(b->title, cx, y + h * .44f, y + h * .08f, h * .72f, w, ink, 0);
		spine_text("GAME BOY ADVANCE", cx, y + h * .84f, h * .13f, fminf(w / K * .3f, 8), ink, 0);
		return;
	}
	u32 ink = 0xfff6f9fb, dark = 0xff1a1a1a;
	if (b->region == SHELF_REGION_JP) vgrad(x, y, w, h, 0xffeef2f2, 0xffdce2e2);   // the white spine card
	else if (b->region == SHELF_REGION_PAL && hashc(b->title) % 3 == 0) vgrad(x, y, w, h, 0xff7a3a22, 0xff52220f);   // #1b2f6a
	bool black = b->region != SHELF_REGION_JP && !(b->region == SHELF_REGION_PAL && hashc(b->title) % 3 == 0);
	if (black) jewel_black(x, y, w, h);   // exact greys: no green in the RGB565 atlas (room.h)
	// the case's plastic: rounded shading across, lighter rails along both long edges
	else {
		const float q0[4][2] = { { x, y }, { x + w / 2, y }, { x + w / 2, y + h }, { x, y + h } };
		const u32 c0[4] = { 0x40000000, 0x06000000, 0x06000000, 0x40000000 };
		shelf_scene_solid4(q0, c0);
		const float q1[4][2] = { { x + w / 2, y }, { x + w, y }, { x + w, y + h }, { x + w / 2, y + h } };
		const u32 c1[4] = { 0x06000000, 0x38000000, 0x38000000, 0x06000000 };
		shelf_scene_solid4(q1, c1);
	}
	if (b->region == SHELF_REGION_JP) {
		spine_head_block(b, x, y, w, h, .75f, dark);
		spine_title(b->title, cx, y + h * .515f, y + h * .13f, h * .77f, w, 0xff181818, 0);
		if (b->serial[0]) spine_text(b->serial, cx, y + h * .92f, h * .06f, fminf(w / K * .4f, 7), 0xff303030, 0);
		else rect(cx - even(w * .25f), y + even(h * .945f), 2 * even(w * .25f), 2 * K, 0xff303030);
		rect(x, y, K, h, 0x26000000);
		rect(x + w - K, y, K, h, 0x26000000);
	} else {
		spine_head_block(b, x, y, w, h, 1, ink);
		float t0 = .13f;
		if (b->region == SHELF_REGION_PAL) {
			spine_across("PAL", cx, y + h * .115f, w, h * .03f, ink);
			rect(x, y + even(h * .94f), w, even(h * .025f), 0xffd6dada);   // the publisher band, plain
			t0 = .15f;
		}
		spine_title(b->title, cx, y + h * (t0 + .90f) / 2, y + h * t0, h * (.90f - t0), w, ink, 0);
		u32 rail = black ? grey565(ROOM_JEWEL_RAIL) : 0x30ffffff;
		rect(x, y, K, h, rail);
		rect(x + w - K, y, K, h, rail);
	}
	u32 edge = black ? grey565(ROOM_JEWEL_EDGE) : 0x1affffff;
	rect(x, y, w, K, edge);
	rect(x, y + h - K, w, K, edge);
}

// At the start of the next frame, before any screen target is bound: bake
// missing spines of the shelf in close-up and learn their cover colours.
void shelf_scene_bake_pending(void)
{
	if (!shelf_scene_view->nrows) return;
	u64 start = svcGetSystemTick();
	int r = close_row();
	if (r < 0 || !shelf_scene_view->rows[r].n || !in_room()) return;
	cells_for(r);
	int visible[2 * ROOM_BOXES + 2], nvisible = shelf_scene_visible_spines(r, visible, ROOM_BOXES);
	shelf_box *todo[BAKES_PER_FRAME];
	int ntodo = 0, colors = 0;
	shelf_row *row = &shelf_scene_view->rows[r];
	// The room list is shared with drawing: actual clamped camera, both eyes,
	// nearest screen center first. Never protect or bake hidden cubby boxes.
	if (shelf_scene_view->zoom >= .3f) for (int k = 0; k < nvisible; k++) {
		int c = shelf_scene_cell_of(&row->box[visible[k]]);
		if (c >= 0) shelf_scene_state.cell[c].used = shelf_scene_state.frame;
	}
	int current[ROOM_BOXES];memcpy(current,visible,nvisible*sizeof(*current));
	int candidates = nvisible;
	{
        int target[ROOM_BOXES], ntarget=room_target_spines(r,target,ROOM_BOXES);
        // Before the near boxes fade in, spend the same quota on the destination.
        if (shelf_scene_view->zoom < .3f) candidates=0;
        for(int j=0;j<ntarget && candidates<shelf_scene_state.cells-2;j++) {
            bool found=false;for(int k=0;k<candidates;k++)if(visible[k]==target[j])found=true;
            if(!found)visible[candidates++]=target[j];
        }
        for(int j=0;j<candidates;j++){int c=shelf_scene_cell_of(&row->box[visible[j]]);if(c>=0)shelf_scene_state.cell[c].used=shelf_scene_state.frame;}

		const shelf_box *held = shelf_scene_pulled(shelf_scene_view);
		if (held && (uintptr_t)held >= (uintptr_t)row->box &&
            (uintptr_t)held < (uintptr_t)(row->box + row->n) &&
            ((uintptr_t)held - (uintptr_t)row->box) % sizeof(*held) == 0) {
			int c=shelf_scene_cell_of(held);if(c>=0)shelf_scene_state.cell[c].used=shelf_scene_state.frame;
			int i=(int)(held-row->box);bool found=false;
			for(int j=0;j<candidates;j++)if(visible[j]==i)found=true;
			if(!found && i>=0 && i<row->n)visible[candidates++]=i;
		}
		int ahead = room_lookahead(r, visible, candidates);
		if (ahead >= 0) visible[candidates++] = ahead;
	}
	for (int k = 0; k < candidates && svcGetSystemTick() - start < SYSCLOCK_ARM11 / 300; ) {
		shelf_box *b = &row->box[visible[k++]];
		if (!b->color && colors < 1) {
			Tex3DS_SubTexture s;
			C3D_Tex *t = covers_small(b->cover, &s);
			if (t) { average_color(b, t, &s); colors++; }
		}
		int c = shelf_scene_cell_of(b);
		if (c >= 0) {
			shelf_scene_state.cell[c].used = shelf_scene_state.frame;
			// Repaint only on a readiness transition. A painted cover and its tint
			// arrive separately; neither warrants a periodic rebake while waiting.
			Tex3DS_SubTexture sub;
			bool want_cover = !shelf_scene_state.cell[c].cover && covers_small(b->cover, &sub);
			bool want_tint = !shelf_scene_state.cell[c].tinted && b->color;
			bool want_logo = !shelf_scene_state.cell[c].logo && logo_of(b->system);
			if (!want_cover && !want_tint && !want_logo) continue;
		}
		if (ntodo < BAKES_PER_FRAME) todo[ntodo++] = b;
		if (ntodo == BAKES_PER_FRAME) break;
	}
	{
        // Below the near-case fade threshold the wall uses generic bookcase art.
        // Only the destination is warmed there; the wall projection is not drawn.
        bool active=shelf_scene_view->zoom>.3f;
        int drawn=active?nvisible:0,baked=0;
        for(int j=0;j<drawn;j++)baked+=shelf_scene_cell_of(&row->box[current[j]])>=0;
        static int old_row=-1,old_cells=-1;static unsigned old_layout;
        static bool old_active,reported;static u32 missing_since;
        bool changed=old_row!=r || old_cells!=shelf_scene_state.cells || old_layout!=shelf_scene_view->layout || old_active!=active;
        if(changed || baked==drawn){missing_since=shelf_scene_state.frame;reported=false;}
        bool deficit=baked<drawn && shelf_scene_state.frame-missing_since>=60 && !reported;
        if(changed || deficit) {
            log_printf("shelf: cells=%d visible=%d baked=%d compact=%d row=%d cw=%d\n",shelf_scene_state.cells,drawn,baked,shelf_scene_state.compact,r,shelf_scene_state.cw);
            if(deficit) {
                reported=true;
                for(int j=0;j<drawn;j++)if(shelf_scene_cell_of(&row->box[current[j]])<0)
                    log_printf("shelf: missing box=%d title=%s reason=no-painted-cell zoom=%.3f\n",current[j],row->box[current[j]].title,shelf_scene_view->zoom);
            }
            int marked=0;
            if(changed && shelf_scene_state.compact)for(int j=0;j<row->n && marked<8;j++)if(K*px_of(row->box[j].t,row->s)>shelf_scene_state.cw && ++marked)
                log_printf("shelf: compact row=%d box=%d title=%s natural=%d cell=%d\n",r,j,row->box[j].title,K*px_of(row->box[j].t,row->s),shelf_scene_state.cw);
        }
        old_row=r;old_cells=shelf_scene_state.cells;old_layout=shelf_scene_view->layout;old_active=active;
    }
	if (!ntodo) return;
	int bound = -1;
	for (int i = 0; i < ntodo; i++) {
		// a few ms of spines a frame, then the rest next frame: a frame of an animation is 16.7 ms
		if (svcGetSystemTick() - start > SYSCLOCK_ARM11 / 300) break;
		shelf_box *b = todo[i];
		int c = shelf_scene_cell_of(b);
		Tex3DS_SubTexture s;
		C3D_Tex *cover = covers_small(b->cover, &s);
		if (c < 0 && (c = cell_for(b)) < 0) { log_printf("shelf: no cell cells=%d visible=%d row=%d\n",shelf_scene_state.cells,nvisible,r); break; }
		if (cover && !b->color && colors < 1) { average_color(b, cover, &s); colors++; }
		if (c / shelf_scene_state.per_atlas != bound) {
			bound = c / shelf_scene_state.per_atlas;
			C2D_SceneBegin(shelf_scene_state.atlas_rt[bound]);
			C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
		}
		bake(b, c, cover, &s);
		shelf_scene_state.cell[c].system = b->system;
		shelf_scene_state.cell[c].row = b->row;
		shelf_scene_state.cell[c].used = shelf_scene_state.frame;
		// Track the actual painted states, including the one eventual tint repaint.
		shelf_scene_state.cell[c].cover = cover != NULL;
		shelf_scene_state.cell[c].tinted = b->color != 0;
		shelf_scene_state.cell[c].logo = logo_of(b->system) != NULL;
	}
	if (bound < 0) return;
	C2D_Flush();
	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
}
static int room_target_spines(int r,int *indices,int cap)
{
 int discard[ROOM_BOXES];shelf_scene_visible_spines(r,discard,ROOM_BOXES);
 int n=shelf_scene_state.visible.target_n<cap?shelf_scene_state.visible.target_n:cap;memcpy(indices,shelf_scene_state.visible.target,n*sizeof(*indices));return n;
}
// One immediate neighbor outside the visible interval, in the scroll direction.
// It is considered only after every visible candidate needs no bake.
static int room_lookahead(int r, const int *visible, int n)
{
 if (!n) return -1;
 int lo=visible[0],hi=lo;
 for(int j=1;j<n;j++){if(visible[j]<lo)lo=visible[j];if(visible[j]>hi)hi=visible[j];}
 float delta=shelf_scene_view->sel[r]-row_cur(r);
 static float previous;static int direction=1,old_row=-1;static unsigned layout;
 if(old_row!=r || layout!=shelf_scene_view->layout){previous=row_cur(r);direction=1;old_row=r;layout=shelf_scene_view->layout;}
 if(delta>.001f || row_cur(r)>previous+.001f)direction=1;
 else if(delta<-.001f || row_cur(r)<previous-.001f)direction=-1;
 previous=row_cur(r);
 int i=direction>0?hi+1:lo-1;
 return i>=0 && i<shelf_scene_view->rows[r].n && shelf_scene_flowed.cub[i]==shelf_scene_flowed.cub[visible[0]]?i:-1;
}
