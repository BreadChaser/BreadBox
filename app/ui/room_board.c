// BreadBox room walls, portable side (room_walls.h): the corkboard's cards, their
// file, their pictures (64x64 RGB565 cells) and the walls' mesh. GPL-2.0-or-later.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "room_walls.h"

// ---------------------------------------------------------------- the cards

static bool same(const room_card *a, int kind, int slot, const char *game)
{
	return a->kind == kind && (kind != ROOM_CARD_STATE || a->slot == slot) && !strcmp(a->game, game);
}

static int find(const room_board *b, int kind, int slot, const char *game)
{
	for (int i = 0; i < b->n; i++)
		if (same(&b->card[i], kind, slot, game)) return i;
	return -1;
}

uint32_t room_board_time(const room_board *b, int kind, int slot, const char *game)
{
	int i = find(b, kind, slot, game);
	return i < 0 ? 0 : b->card[i].time;
}

// spots from the board's middle out (a 5 x 3 grid, row-major index), so a few cards sit together
static const uint8_t spot_order[ROOM_BOARD_MAX] = { 7, 6, 8, 2, 12, 1, 13, 5, 9, 3, 11, 0, 14, 4, 10 };

bool room_board_put(room_board *b, const room_card *c)
{
	int spot = -1, at = find(b, c->kind, c->slot, c->game);
	if (at >= 0) {   // the same card again (a slot saved over, the game played again): its place stays
		spot = b->card[at].spot;
		memmove(&b->card[at], &b->card[at + 1], (size_t)(b->n - at - 1) * sizeof(room_card));
		b->n--;
	} else if (b->n == ROOM_BOARD_MAX) {   // full: the oldest goes, the new card takes its place
		if (c->time < b->card[b->n - 1].time) return false;
		spot = b->card[--b->n].spot;
	} else {
		for (int k = 0; k < ROOM_BOARD_MAX && spot < 0; k++) {
			bool used = false;
			for (int i = 0; i < b->n; i++) used |= b->card[i].spot == spot_order[k];
			if (!used) spot = spot_order[k];
		}
	}
	int i = 0;
	while (i < b->n && b->card[i].time > c->time) i++;
	memmove(&b->card[i + 1], &b->card[i], (size_t)(b->n - i) * sizeof(room_card));
	b->card[i] = *c;
	b->card[i].spot = (uint8_t)spot;
	b->card[i].game[ROOM_BOARD_PATH_LEN - 1] = 0;
	b->card[i].title[ROOM_BOARD_TITLE_LEN - 1] = 0;
	b->n++;
	return true;
}

// ---------------------------------------------------------------- the file

#define FILE_VERSION 1
#define HEAD 8
#define REC (8 + ROOM_BOARD_PATH_LEN + ROOM_BOARD_TITLE_LEN)
#define CELL_BYTES (ROOM_BOARD_CELL * ROOM_BOARD_CELL * 2)

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

size_t room_board_size(const room_board *b)
{
	size_t n = HEAD;
	for (int i = 0; i < b->n; i++) n += REC + (b->card[i].has_cell ? CELL_BYTES : 0);
	return n;
}

size_t room_board_write(const room_board *b, uint8_t *out, size_t cap)
{
	size_t n = room_board_size(b);
	if (n > cap) return 0;
	memcpy(out, "BBRD", 4);
	out[4] = FILE_VERSION; out[5] = 0;
	out[6] = (uint8_t)b->n; out[7] = 0;
	uint8_t *p = out + HEAD;
	for (int i = 0; i < b->n; i++) {
		const room_card *c = &b->card[i];
		p[0] = c->kind; p[1] = c->slot; p[2] = c->has_cell; p[3] = c->spot;
		put32(p + 4, c->time);
		memcpy(p + 8, c->game, ROOM_BOARD_PATH_LEN);
		memcpy(p + 8 + ROOM_BOARD_PATH_LEN, c->title, ROOM_BOARD_TITLE_LEN);
		p += REC;
		if (c->has_cell) {
			for (int k = 0; k < ROOM_BOARD_CELL * ROOM_BOARD_CELL; k++) { p[2 * k] = (uint8_t)c->cell[k]; p[2 * k + 1] = (uint8_t)(c->cell[k] >> 8); }
			p += CELL_BYTES;
		}
	}
	return n;
}

bool room_board_read(room_board *b, const uint8_t *data, size_t size)
{
	b->n = 0;
	if (!data || size < HEAD || memcmp(data, "BBRD", 4) || data[4] != FILE_VERSION || data[6] > ROOM_BOARD_MAX) return false;
	const uint8_t *p = data + HEAD, *end = data + size;
	unsigned used = 0;
	for (int i = 0; i < data[6]; i++) {
		if (end - p < REC) { b->n = 0; return false; }
		room_card *c = &b->card[b->n];
		memset(c, 0, offsetof(room_card, cell));
		c->kind = p[0]; c->slot = p[1]; c->has_cell = p[2] != 0; c->spot = p[3];
		c->time = get32(p + 4);
		memcpy(c->game, p + 8, ROOM_BOARD_PATH_LEN);
		memcpy(c->title, p + 8 + ROOM_BOARD_PATH_LEN, ROOM_BOARD_TITLE_LEN);
		c->game[ROOM_BOARD_PATH_LEN - 1] = c->title[ROOM_BOARD_TITLE_LEN - 1] = 0;
		p += REC;
		if (c->has_cell) {
			if (end - p < CELL_BYTES) { b->n = 0; return false; }
			for (int k = 0; k < ROOM_BOARD_CELL * ROOM_BOARD_CELL; k++) c->cell[k] = (uint16_t)(p[2 * k] | p[2 * k + 1] << 8);
			p += CELL_BYTES;
		}
		// a damaged record (a spot taken twice, an unknown kind) is left out, not the whole board
		if (c->kind > ROOM_CARD_PLAYED || c->spot >= ROOM_BOARD_MAX || (used >> c->spot & 1) || !c->game[0]) continue;
		used |= 1u << c->spot;
		b->n++;
	}
	return true;
}

// ---------------------------------------------------------------- pictures

static uint16_t rgb(int r, int g, int b) { return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3); }

static void fill(uint16_t *cell, int x0, int y0, int x1, int y1, uint16_t c)
{
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			if ((unsigned)x < ROOM_BOARD_CELL && (unsigned)y < ROOM_BOARD_CELL) cell[y * ROOM_BOARD_CELL + x] = c;
}

static uint16_t texel(const room_picture *p, int x, int y)
{
	if (!p->tiled) return p->px[y * p->stride + x];
	int w = p->stride;
	uint32_t i = (uint32_t)(((y >> 3) * (w >> 3) + (x >> 3)) << 6) |
		(x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
	return p->px[i];
}

// the picture centre-cropped to dw x dh's shape, each texel the mean of the block it covers
static void blit(uint16_t *cell, int dx, int dy, int dw, int dh, const room_picture *p)
{
	if (!p || !p->px || p->w <= 0 || p->h <= 0) {   // no picture: a dark frame with a lighter middle
		fill(cell, dx, dy, dx + dw, dy + dh, rgb(52, 48, 60));
		fill(cell, dx + 3, dy + 3, dx + dw - 3, dy + dh - 3, rgb(76, 72, 86));
		return;
	}
	float sw = (float)p->w, sh = (float)p->h;
	if (sw * dh > sh * dw) sw = sh * dw / dh; else sh = sw * dh / dw;
	float ox = (p->w - sw) / 2, oy = (p->h - sh) / 2, kx = sw / dw, ky = sh / dh;
	for (int y = 0; y < dh; y++) {
		int y0 = (int)(oy + y * ky), y1 = (int)(oy + (y + 1) * ky);
		if (y1 <= y0) y1 = y0 + 1;
		if (y1 > p->h) y1 = p->h;
		for (int x = 0; x < dw; x++) {
			int x0 = (int)(ox + x * kx), x1 = (int)(ox + (x + 1) * kx);
			if (x1 <= x0) x1 = x0 + 1;
			if (x1 > p->w) x1 = p->w;
			unsigned r = 0, g = 0, b = 0, n = 0;
			for (int sy = y0; sy < y1; sy++)
				for (int sx = x0; sx < x1; sx++) {
					uint16_t c = texel(p, sx, sy);
					r += c >> 11; g += c >> 5 & 63; b += c & 31; n++;
				}
			if (n && (unsigned)(dx + x) < ROOM_BOARD_CELL && (unsigned)(dy + y) < ROOM_BOARD_CELL)
				cell[(dy + y) * ROOM_BOARD_CELL + dx + x] = (uint16_t)((r / n) << 11 | (g / n) << 5 | b / n);
		}
	}
}

// 3x5 glyphs, rows top first, 3 bits each (bit 2 = left)
static const struct { char c; uint16_t rows; } glyphs[] = {
	{ 'A', 0x2BED }, { 'B', 0x6BAE }, { 'C', 0x3923 }, { 'D', 0x6B6E }, { 'E', 0x79A7 }, { 'F', 0x79A4 },
	{ 'G', 0x396B }, { 'H', 0x5BED }, { 'I', 0x7497 }, { 'J', 0x126A }, { 'K', 0x5BAD }, { 'L', 0x4927 },
	{ 'M', 0x5FED }, { 'N', 0x6B6D }, { 'O', 0x2B6A }, { 'P', 0x6BA4 }, { 'Q', 0x2B73 }, { 'R', 0x6BAD },
	{ 'S', 0x388E }, { 'T', 0x7492 }, { 'U', 0x5B6F }, { 'V', 0x5B6A }, { 'W', 0x5BFD }, { 'X', 0x5AAD },
	{ 'Y', 0x5A92 }, { 'Z', 0x72A7 }, { '0', 0x7B6F }, { '1', 0x2C97 }, { '2', 0x62A7 }, { '3', 0x628E },
	{ '4', 0x5BC9 }, { '5', 0x798E }, { '6', 0x39EF }, { '7', 0x7292 }, { '8', 0x7BEF }, { '9', 0x7BCE },
	{ '/', 0x12A4 }, { ':', 0x0410 }, { '-', 0x01C0 }, { '.', 0x0002 }, { ',', 0x0014 }, { '\'', 0x2400 },
	{ '!', 0x2482 }, { '?', 0x6282 }, { '&', 0x2AAB }, { '+', 0x05D0 }, { '#', 0x5F7D }, { '(', 0x1491 },
	{ ')', 0x4494 },
};

static uint16_t glyph(char c)
{
	if (c >= 'a' && c <= 'z') c = (char)(c - 32);
	for (size_t i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++)
		if (glyphs[i].c == c) return glyphs[i].rows;
	return 0;
}

int room_board_text(uint16_t *cell, int stride, int x, int y, int maxx, const char *s, uint16_t ink)
{
	for (; s && *s && x + 3 <= maxx; s++, x += 4) {
		uint16_t g = glyph(*s);
		for (int r = 0; r < 5; r++)
			for (int k = 0; k < 3; k++)
				if (g >> (14 - r * 3 - k) & 1)
					cell[(y + r) * stride + x + k] = ink;
	}
	return x;
}

static int text_w(const char *s) { int n = (int)strlen(s); return n ? n * 4 - 1 : 0; }

// the words of s that fit in w pixels (at least the first word's start); returns where the rest starts
static const char *fit_words(const char *s, int w, char *out, size_t cap)
{
	int max = (w + 1) / 4, n = (int)strlen(s), cut = n;
	if (n > max) {
		cut = max;
		for (int i = max; i > 0; i--)
			if (s[i] == ' ') { cut = i; break; }
	}
	if (cut >= (int)cap) cut = (int)cap - 1;
	memcpy(out, s, (size_t)cut);
	out[cut] = 0;
	s += cut;
	while (*s == ' ') s++;
	return s;
}

static void date_text(char *out, size_t cap, uint32_t t)
{
	time_t tt = (time_t)t;
	struct tm *tm = t ? gmtime(&tt) : NULL;   // the 3DS clock is local time already
	if (tm) snprintf(out, cap, "%d/%d", tm->tm_mon + 1, tm->tm_mday);
	else snprintf(out, cap, "-");
}

#define PAPER rgb(242, 233, 214)
#define INK rgb(40, 44, 70)
#define POSTMARK rgb(168, 52, 40)

void room_board_postcard(uint16_t *cell, const room_picture *pic, const char *title, int kind, int slot, uint32_t time)
{
	enum { W = ROOM_POSTCARD_W, H = ROOM_POSTCARD_H };
	fill(cell, 0, 0, ROOM_BOARD_CELL, ROOM_BOARD_CELL, PAPER);
	fill(cell, 2, 2, 62, 39, rgb(200, 188, 166));     // a hairline round the picture
	blit(cell, 3, 3, 58, 35, pic);
	char t[ROOM_BOARD_TITLE_LEN];
	fit_words(title ? title : "", 61, t, sizeof(t));
	room_board_text(cell, ROOM_BOARD_CELL, 2, 40, 63, t, INK);
	char what[16], when[16];
	if (kind == ROOM_CARD_CONTINUE) snprintf(what, sizeof(what), "CONTINUE");
	else snprintf(what, sizeof(what), "SLOT %d", (unsigned)(slot + 1) % 100);   // the player counts 1..12; slot is the card index (bb_slots.h)
	date_text(when, sizeof(when), time);
	room_board_text(cell, ROOM_BOARD_CELL, 2, 46, 62 - text_w(when) - 2, what, rgb(96, 92, 104));
	room_board_text(cell, ROOM_BOARD_CELL, 62 - text_w(when), 46, 63, when, POSTMARK);
	// the airmail edge along the top: red and blue dashes
	for (int x = 0; x < W; x++) cell[x] = (x / 4) % 2 ? rgb(196, 54, 46) : rgb(46, 74, 160);
}

void room_board_ticket(uint16_t *cell, const room_picture *cover, const char *title, uint32_t time)
{
	enum { W = ROOM_TICKET_W, H = ROOM_TICKET_H };
	uint16_t paper = rgb(236, 190, 112), dark = rgb(176, 128, 64);
	fill(cell, 0, 0, ROOM_BOARD_CELL, ROOM_BOARD_CELL, paper);
	for (int y = 2; y < H; y += 4) {   // perforated sides
		cell[y * ROOM_BOARD_CELL] = dark;
		cell[y * ROOM_BOARD_CELL + W - 1] = dark;
	}
	room_board_text(cell, ROOM_BOARD_CELL, (W - text_w("ADMIT ONE")) / 2, 2, W, "ADMIT ONE", rgb(120, 40, 30));
	for (int x = 2; x < W - 2; x += 2) cell[8 * ROOM_BOARD_CELL + x] = dark;   // the tear line
	// the name on one line, or two (the cover a little shorter)
	char l1[ROOM_BOARD_TITLE_LEN], l2[ROOM_BOARD_TITLE_LEN];
	const char *rest = fit_words(title ? title : "", W - 3, l1, sizeof(l1));
	fit_words(rest, W - 3, l2, sizeof(l2));
	int lines = l2[0] ? 2 : 1, cb = lines == 2 ? 46 : 52;
	fill(cell, 3, 10, W - 3, cb, rgb(120, 84, 44));
	blit(cell, 4, 11, W - 8, cb - 12, cover);
	uint16_t ink = rgb(60, 36, 20);
	room_board_text(cell, ROOM_BOARD_CELL, (W - text_w(l1)) / 2, cb + 1, W, l1, ink);
	if (lines == 2) room_board_text(cell, ROOM_BOARD_CELL, (W - text_w(l2)) / 2, cb + 7, W, l2, ink);
	char when[16];
	date_text(when, sizeof(when), time);
	room_board_text(cell, ROOM_BOARD_CELL, (W - text_w(when)) / 2, 59, W, when, rgb(120, 40, 30));
}

// pins: red, blue, yellow, green (8x8 each in the top row from x 32), paper 0..15, shadow 16..31
static const uint8_t pin_rgb[4][3] = { { 214, 48, 40 }, { 52, 92, 206 }, { 232, 196, 48 }, { 62, 160, 76 } };

void room_board_utility(uint16_t *cell)
{
	fill(cell, 0, 0, ROOM_BOARD_CELL, ROOM_BOARD_CELL, rgb(255, 255, 255));
	fill(cell, 16, 0, 32, 16, rgb(46, 30, 18));
	for (int k = 0; k < 4; k++) {
		const uint8_t *c = pin_rgb[k];
		int x0 = 32 + 8 * k;
		fill(cell, x0, 0, x0 + 8, 8, rgb(c[0] * 3 / 4, c[1] * 3 / 4, c[2] * 3 / 4));
		fill(cell, x0 + 1, 1, x0 + 7, 7, rgb(c[0], c[1], c[2]));
		fill(cell, x0 + 2, 2, x0 + 4, 4, rgb((c[0] + 255) / 2, (c[1] + 255) / 2, (c[2] + 255) / 2));
	}
}

// ---------------------------------------------------------------- the mesh

bool room_wall_of(const room_file *f, const char *name, room_wall *w)
{
	const float *p = room_anchor(f, name);
	memset(w, 0, sizeof(*w));
	if (!p || p[3] <= 0 || p[4] <= 0 || (p[5] != 1 && p[5] != -1)) return false;
	memcpy(w->plane, p, sizeof(w->plane));
	const float at[3] = { p[0] + p[5] * 150, p[1], p[2] }, toward[3] = { p[5], 0, 0 };
	room_probe_light(f, at, toward, w->light);
	w->unit = room_light_unit(f);
	return w->ok = true;
}

static int16_t i16(float x) { return (int16_t)floorf(x + .5f); }
static uint8_t lit(float x) { return (uint8_t)(x > 255 ? 255 : x < 0 ? 0 : x + .5f); }

void room_wall_point(const room_wall *w, float s, float t, float off, room_vertex *v)
{
	const float *p = w->plane;
	float nx = p[5];
	v->x = i16(p[0] + nx * off);
	v->y = i16(p[1] + (.5f - t) * p[4]);
	v->z = i16(p[2] + nx * (s - .5f) * p[3]);
	v->w = 0;
	v->r = lit(128 * w->light[0]); v->g = lit(128 * w->light[1]); v->b = lit(128 * w->light[2]);
	v->a = 255;
}

void room_walls_relight(const room_vertex *unlit, room_vertex *out, int n, int nposter, const room_wall *poster, const room_wall *board)
{
	for (int i = 0; i < n; i++) {
		const room_wall *w = i < nposter ? poster : board;
		const float *l = w->light, k = w->unit / 128;
		out[i] = unlit[i];
		out[i].r = lit(unlit[i].r * l[0] * k); out[i].g = lit(unlit[i].g * l[1] * k); out[i].b = lit(unlit[i].b * l[2] * k);
	}
}

// a quad as two triangles: corners (s, t) TL TR BR BL, texels (u0, v0)-(u1, v1)
static int quad(const room_wall *w, const float st[4][2], float off, float u0, float v0, float u1, float v1, uint8_t shade, room_vertex *o)
{
	static const int order[6] = { 0, 1, 2, 0, 2, 3 };
	const float uv[4][2] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
	for (int k = 0; k < 6; k++) {
		int c = order[k];
		room_wall_point(w, st[c][0], st[c][1], off, &o[k]);
		o[k].u = i16(uv[c][0] * 16);
		o[k].v = i16(uv[c][1] * 16);
		o[k].r = lit(shade * w->light[0]); o[k].g = lit(shade * w->light[1]); o[k].b = lit(shade * w->light[2]);
	}
	return 6;
}

int room_poster_mesh(const room_wall *w, float u1, float v1, room_vertex *out)
{
	const float st[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	return quad(w, st, 2, 0, 0, u1, v1, 128, out);
}

static uint32_t hash(const room_card *c)
{
	uint32_t h = 2166136261u;
	for (const char *s = c->game; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
	h = (h ^ c->kind) * 16777619u;
	h = (h ^ c->slot) * 16777619u;
	h = (h ^ c->time) * 16777619u;
	return h ^ h >> 15;
}

void room_board_place(const room_wall *w, const room_card *c, room_card_place *p)
{
	uint32_t h = hash(c);
	int col = c->spot % 5, row = c->spot / 5;
	float jx = ((h & 255) / 255.0f - .5f) * 64, jy = ((h >> 8 & 255) / 255.0f - .5f) * 50;   // mm: a loose grid, cards overlap a little
	bool ticket = c->kind == ROOM_CARD_PLAYED;
	p->w = ticket ? 106 : 160;
	p->h = ticket ? 147 : 130;
	p->s = (col + .5f) / 5 + jx / w->plane[3];
	p->t = (row + .5f) / 3 + jy / w->plane[4];
	p->angle = ((h >> 16 & 255) / 255.0f - .5f) * .34f;   // about +-10 degrees
	p->pin = (int)(h >> 24 & 3);
	// keep the card (its rotated corners) inside the cork
	float ex = (fabsf(cosf(p->angle)) * p->w + fabsf(sinf(p->angle)) * p->h) / 2 + 6;
	float ey = (fabsf(sinf(p->angle)) * p->w + fabsf(cosf(p->angle)) * p->h) / 2 + 6;
	float ms = ex / w->plane[3], mt = ey / w->plane[4];
	if (p->s < ms) p->s = ms;
	if (p->s > 1 - ms) p->s = 1 - ms;
	if (p->t < mt) p->t = mt;
	if (p->t > 1 - mt) p->t = 1 - mt;
}

// corners of a w x h mm rectangle centred at (s, t) turned by a (counter-clockwise as seen), offset (mm)
static void corners(const room_wall *w, float s, float t, float cw, float ch, float a, float dx, float dy, float st[4][2])
{
	static const float k[4][2] = { { -.5f, .5f }, { .5f, .5f }, { .5f, -.5f }, { -.5f, -.5f } };   // TL TR BR BL (y up)
	float c = cosf(a), sn = sinf(a);
	for (int i = 0; i < 4; i++) {
		float x = k[i][0] * cw, y = k[i][1] * ch;
		float rx = x * c - y * sn + dx, ry = x * sn + y * c + dy;
		st[i][0] = s + rx / w->plane[3];
		st[i][1] = t - ry / w->plane[4];
	}
}

// paper catches more light than the cork round it: the cards a quarter brighter than the wall's light
#define CARD_SHADE 160

int room_board_mesh(const room_wall *w, const room_board *b, room_vertex *out, int cap)
{
	int n = 0;
	const float U = ROOM_BOARD_CELL * 3, V = ROOM_BOARD_CELL * 3;   // the 16th cell: utility
	for (int i = b->n - 1; i >= 0 && n + 18 <= cap; i--) {
		const room_card *c = &b->card[i];
		room_card_place p;
		room_board_place(w, c, &p);
		float st[4][2];
		bool ticket = c->kind == ROOM_CARD_PLAYED;
		float cu = (float)(c->spot % 4 * ROOM_BOARD_CELL), cv = (float)(c->spot / 4 * ROOM_BOARD_CELL);
		corners(w, p.s, p.t, p.w, p.h, p.angle, 7, -9, st);   // its shadow, down and to the right
		n += quad(w, st, 2, U + 18, V + 2, U + 30, V + 14, 128, out + n);
		corners(w, p.s, p.t, p.w, p.h, p.angle, 0, 0, st);
		// half a texel in from the cell's edges: the bilinear filter never reaches the next cell
		n += quad(w, st, 3, cu + .5f, cv + .5f, cu + (ticket ? ROOM_TICKET_W : ROOM_POSTCARD_W) - .5f, cv + (ticket ? ROOM_TICKET_H : ROOM_POSTCARD_H) - .5f, CARD_SHADE, out + n);
		// the pin near the top edge's middle
		float c0 = cosf(p.angle), s0 = sinf(p.angle), py = p.h / 2 - 5;
		corners(w, p.s, p.t, 13, 13, p.angle, -py * s0, py * c0, st);
		n += quad(w, st, 5, U + 32 + 8 * p.pin + 1, V + 1, U + 32 + 8 * p.pin + 7, V + 7, 128, out + n);
	}
	return n;
}
