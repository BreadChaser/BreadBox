/* Text stands on the ruled lines with any font (owner 2026-11-12: "the lines under text for the post-it notes cards:
 * the text still doesn't line up"). On a real 3DS the card text sat ~3 px under every rule while Azahar showed it on
 * them: the placement was a fixed 20 px baseline in citro2d's 30 px cell, which is where Azahar's replacement font
 * draws its letters. Nintendo's system font stands lower in its cell (hardware stills: 3 px low at the 0.7 floor,
 * i.e. 24-25 of 30).
 *
 * This runs the shipped measuring code (app/ui/ui_font.c) on glyph sheets whose letters stand at a known row, then
 * puts every ruled surface's text where the shipped tokens put it (UI_PAPER_TOP / UI_PAPER_TITLE_TOP, used by
 * ui_card_draw, ui_card_header, the room cards, the postcards) and checks that the letters' bottom edge lands on the
 * rule (+-0 px) at the real draw sizes. The fonts: two Nintendo-like ones (letters at 24 and 25 of 30, header 25),
 * Azahar's (header 25, letters at 20), another region's cell size (34 px), an A8 sheet, a sheet format that cannot be
 * read (the header's baselinePos is the fallback), and the app's real title font file (assets/romfs/font). */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../app/ui/ui_font.c"
#include "ui_style.h"

/* ---- the fonts ---- */
static TGLP_s tglp;
static FINF_s finf = { &tglp, 0, 0 };
static u8 *sheet;
static char charset[128];
static charWidthInfo_s widths[128];
static bool real_font;               /* the title font file: its own glyph index and widths */
static u16 real_index[128];

static unsigned texel(int x, int y, int width) { return font_texel(x, y, width); }
static void put(int index, int x, int y, int alpha)   /* alpha 0..255 */
{
	int per = tglp.nRows * tglp.nLines, local = index % per;
	int ox = (local % tglp.nRows) * (tglp.cellWidth + 1) + 1, oy = (local / tglp.nRows) * (tglp.cellHeight + 1) + 1;
	unsigned pos = texel(ox + x, oy + y, tglp.sheetWidth);
	u8 *s = sheet + (index / per) * tglp.sheetSize;
	if (tglp.sheetFmt == GPU_A8) s[pos] = alpha;
	else s[pos / 2] = (s[pos / 2] & ~(15 << (4 * (pos & 1)))) | (alpha / 17) << (4 * (pos & 1));
}
/* A synthetic font: letters' ink from cap_top to the bottom edge `edge` (rows of a cell `cell` tall), an anti-aliased
   fringe row under it (a third covered: not ink), descenders 5 rows lower, the header's own baselinePos `declared`. */
static void make_font(int cell, int declared, int edge, int fmt)
{
	free(sheet);
	real_font = false;
	strcpy(charset, "?HEILTxzgpoaFS");
	memset(&tglp, 0, sizeof(tglp));
	tglp.cellHeight = cell; tglp.cellWidth = 24; tglp.baselinePos = declared;
	tglp.nRows = 8; tglp.nLines = 4; tglp.sheetWidth = 256;
	tglp.sheetFmt = fmt;
	int height = 4 * (cell + 1) + 8;
	height = (height + 7) / 8 * 8;
	tglp.sheetSize = tglp.sheetWidth * height / (fmt == GPU_A8 ? 1 : 2);
	sheet = calloc(1, tglp.sheetSize);
	tglp.sheetData = sheet;
	finf.ascent = declared; finf.lineFeed = cell;
	int cap_top = edge - cell * 14 / 30, x_top = edge - cell * 10 / 30;
	for (int i = 0; charset[i]; i++) {
		char c = charset[i];
		bool low = strchr("xzgpoa", c) != NULL, desc = strchr("gp", c) != NULL;
		int top = low ? x_top : cap_top, bottom = desc ? edge + 5 : edge;
		widths[i] = (charWidthInfo_s){ 1, 14, 16 };
		if (fmt != GPU_A4 && fmt != GPU_A8) continue;   /* a sheet the reader cannot read */
		for (int y = top; y < bottom; y++)
			for (int x = 0; x < 14; x++) put(i, x, y, 255);
		for (int x = 0; x < 14; x++) put(i, x, bottom, 85);
	}
	finf.tglp = &tglp;
}

/* The app's title font (assets/romfs/font/title.bcfnt, CFNT): its header, sheet, widths and code map. */
static u8 *cfnt;
static void load_title_font(const char *path)
{
	FILE *f = fopen(path, "rb");
	assert(f);
	fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
	cfnt = malloc(n); assert(fread(cfnt, 1, n, f) == (size_t)n); fclose(f);
	assert(!memcmp(cfnt, "CFNT", 4));
	unsigned finf_at = cfnt[6] | cfnt[7] << 8;
	#define U16(o) (cfnt[o] | cfnt[(o) + 1] << 8)
	#define U32(o) (U16(o) | (unsigned)U16((o) + 2) << 16)
	assert(!memcmp(cfnt + finf_at, "FINF", 4));
	unsigned t = U32(finf_at + 16) - 8, cw = U32(finf_at + 20) - 8, cm = U32(finf_at + 24) - 8;   /* data, past the magic */
	assert(!memcmp(cfnt + t, "TGLP", 4));
	memset(&tglp, 0, sizeof(tglp));
	tglp.cellWidth = cfnt[t + 8]; tglp.cellHeight = cfnt[t + 9]; tglp.baselinePos = cfnt[t + 10];
	tglp.sheetSize = U32(t + 12); tglp.sheetFmt = U16(t + 18) == 11 ? GPU_A4 : U16(t + 18) == 8 ? GPU_A8 : 99;
	tglp.nRows = U16(t + 20); tglp.nLines = U16(t + 22); tglp.sheetWidth = U16(t + 24);
	tglp.sheetData = cfnt + U32(t + 28);
	finf.ascent = cfnt[finf_at + 30]; finf.lineFeed = cfnt[finf_at + 9];
	strcpy(charset, "HEILTxz");
	memset(real_index, 0xff, sizeof(real_index));
	for (unsigned o = cm; o; ) {   /* CMAP: direct, table or scan */
		unsigned lo = U16(o + 8), hi = U16(o + 10), method = U16(o + 12), next = U32(o + 16);
		for (unsigned c = lo; c <= hi && c < 128; c++) {
			if (method == 0) real_index[c] = U16(o + 20) + c - lo;
			else if (method == 1) real_index[c] = U16(o + 20 + 2 * (c - lo));
		}
		if (method == 2) for (unsigned i = 0, k = U16(o + 20); i < k; i++) {
			unsigned c = U16(o + 22 + 4 * i);
			if (c < 128) real_index[c] = U16(o + 24 + 4 * i);
		}
		next = next ? next - 8 : 0;
		o = next;
	}
	for (int c = 0; c < 128; c++) if (real_index[c] != 0xffff)
		for (unsigned o = cw; o; ) {   /* CWDH: widths by glyph index */
			unsigned lo = U16(o + 8), hi = U16(o + 10), next = U32(o + 12);
			if (real_index[c] >= lo && real_index[c] <= hi) {
				const u8 *w = cfnt + o + 16 + 3 * (real_index[c] - lo);
				widths[c] = (charWidthInfo_s){ (signed char)w[0], w[1], w[2] };
				break;
			}
			o = next ? next - 8 : 0;
		}
	real_font = true;
	finf.tglp = &tglp;
}

/* the fake citro2d font calls ui_font.c makes (both faces read the font under test) */
FINF_s *C2D_FontGetInfo(C2D_Font f) { (void)f; return &finf; }
int C2D_FontGlyphIndexFromCodePoint(C2D_Font f, u32 ch)
{
	(void)f;
	if (real_font) return real_index[ch & 127];
	const char *p = strchr(charset, (int)ch);
	return p ? (int)(p - charset) : 0;   /* a missing letter draws the font's replacement glyph ('?') */
}
charWidthInfo_s *C2D_FontGetCharWidthInfo(C2D_Font f, int index)
{
	(void)f;
	if (real_font) { for (int c = 0; c < 128; c++) if (real_index[c] == index) return &widths[c]; assert(0); }
	return &widths[index];
}
C2D_Font libui_font(void) { return (C2D_Font)1; }
static int logged;
void log_printf(const char *fmt, ...) { (void)fmt; logged++; }
static bool dev;
bool dev_enabled(void) { return dev; }

static void reset(void) { ready[0] = ready[1] = false; }

/* ---- the ruled surfaces ---- */
static int checks;
static const char *font_name;
static float edge_rows;   /* the font's letters' true bottom edge, in cell rows */
/* text drawn at `top` (a cell top, as the tokens give it) at `size`: its letters' bottom edge must be `line` */
static void on_line(const char *surface, float top, float size, float line)
{
	float edge = top + size * 30.0f * edge_rows / tglp.cellHeight;
	if (fabsf(edge - line) > 1e-3f) {
		fprintf(stderr, "FAIL %s, %s at size %.2f: the letters stand at %.2f, the rule is at %.0f (%+.2f px)\n",
			font_name, surface, size, edge, line, edge - line);
		exit(1);
	}
	checks++;
}
/* sizes a fitted line may take between its size and the floor (ui_fit_ex shrinks in between) */
static void sizes(const char *surface, float size, float floor_size, float line, bool title)
{
	for (float s = size; s >= floor_size - 1e-4f; s -= .01f)
		on_line(surface, title ? UI_PAPER_TITLE_TOP(line, s) : UI_PAPER_TOP(line, s), s, line);
}
static void body_surfaces(void)
{
	/* the Settings / pause card (ui_card_draw): rows, values, the notice, the eyebrow on the red rule */
	for (int k = 0; k < UI_CARD_ROWS; k++) {
		float band = UI_CARD_ROWS_Y + k * UI_CARD_ROW, line = band + UI_CARD_ROW - 1;
		sizes("settings/pause card row label", UI_CARD_ROW_SIZE, UI_TEXT_FLOOR, line, false);
		sizes("settings/pause card row value", UI_CARD_VALUE_SIZE, UI_TEXT_FLOOR, line, false);
	}
	float notice = UI_CARD_NOTICE_LINE;
	sizes("settings/pause card notice", UI_CARD_NOTICE_SIZE, UI_TEXT_FLOOR, notice, false);
	sizes("card eyebrow on the red rule", UI_TEXT_FLOOR, UI_TEXT_FLOOR, UI_CARD_RULE_Y, false);
	sizes("room card eyebrow on the red rule", UI_TEXT_FLOOR, UI_TEXT_FLOOR, UI_ROOM_RULE_Y, false);
	/* the room's place cards, the Collection list, the game card and the welcome tour's cards (shelf_scene_draw.c shelf_scene_text_at
	   on paper: UI_PAPER_TOP at px / 30) */
	const float px[] = { UI_ROOM_ROW_PX, UI_ROOM_VALUE_PX, UI_ROOM_LINE_PX, UI_ROOM_HINT_PX, UI_ROOM_MIN_PX, UI_ROOM_TINY_PX };
	for (float y = UI_ROOM_ROWS_Y; y <= UI_ROOM_HINT_Y; y += UI_ROOM_LIST_STEP)
		for (unsigned i = 0; i < sizeof(px) / sizeof(px[0]); i++)
			on_line("room card line", UI_PAPER_TOP(y, px[i] / 30.0f), px[i] / 30.0f, y);
	on_line("game card foot line", UI_PAPER_TOP(UI_ROOM_GAME_HINT_Y, UI_ROOM_HINT_PX / 30.0f), UI_ROOM_HINT_PX / 30.0f, UI_ROOM_GAME_HINT_Y);
	/* the postcards: the handwriting on the back's first address line (pc_hand: UI_PAPER_TOP(0) about the line), the
	   Continue postcard's name lines, its foot and the blank card's line */
	on_line("postcard handwriting", UI_PC_TOP_RULE_Y + UI_PAPER_TOP(0, UI_PC_HAND_BIG), UI_PC_HAND_BIG, UI_PC_TOP_RULE_Y);
	for (int i = 0; i < UI_PC_CC_NAME_LINES + 1; i++)
		sizes("Continue postcard name / foot line", UI_TEXT_FLOOR, UI_TEXT_FLOOR, UI_PC_CC_FOOT - i * UI_PAPER_LINE, false);
	sizes("blank Continue postcard", UI_TEXT_FLOOR, UI_TEXT_FLOOR, UI_PC_CC_EMPTY_LINE, false);
}
static void title_surfaces(void)
{
	/* card titles on the red rule (ui_card_header): the settings / pause card and the room's cards */
	sizes("card title on the red rule", UI_CARD_TITLE_SIZE, UI_CARD_TITLE_MIN, UI_CARD_RULE_Y, true);
	sizes("room card title on the red rule", UI_CARD_TITLE_SIZE, UI_CARD_TITLE_MIN, UI_ROOM_RULE_Y, true);
	/* the title stays inside its card */
	assert(UI_PAPER_TITLE_TOP(UI_CARD_RULE_Y, UI_CARD_TITLE_SIZE) >= UI_CARD_Y);
	assert(UI_PAPER_TITLE_TOP(UI_ROOM_RULE_Y, UI_CARD_TITLE_SIZE) >= 0);
}
static void body_font(const char *name, int cell, int declared, int edge, int fmt, float expect)
{
	font_name = name;
	make_font(cell, declared, edge, fmt);
	reset();
	edge_rows = edge;
	float got = ui_face_baseline(UI_FACE_SYSTEM);
	if (fabsf(got - expect) > 1e-4f) { fprintf(stderr, "FAIL %s: baseline %.3f, expected %.3f\n", name, got, expect); exit(1); }
	if (fmt == GPU_A4 || fmt == GPU_A8) body_surfaces();
	else {   /* unreadable sheet: the header's baselinePos (a font whose header is right still lands on the lines) */
		edge_rows = declared;
		body_surfaces();
	}
	printf("  %-46s letters at %5.2f/30: every ruled line, every size\n", name, got);
}
int main(int argc, char **argv)
{
	assert(argc == 2);
	printf("text_baseline: the letters' bottom edge on each ruled line (+-0 px)\n");
	body_font("Nintendo-like system font (letters at 25)", 30, 25, 25, GPU_A4, 25);
	body_font("Nintendo-like system font (letters at 24)", 30, 25, 24, GPU_A4, 24);
	body_font("Azahar's replacement (header 25, letters 20)", 30, 25, 20, GPU_A4, 20);
	body_font("another region's cell (34 px, letters at 28)", 34, 28, 28, GPU_A4, 28 * 30.0f / 34);
	body_font("an A8 sheet (letters at 23)", 30, 23, 23, GPU_A8, 23);
	body_font("an unreadable sheet (header 24)", 30, 24, 24, 99, 24);
	/* the shoulder labels' ink rows read the same sheet (ui_button_text): any ink counts there */
	make_font(30, 25, 25, GPU_A4); reset();
	int top, bottom;
	assert(ui_font_ink_rows(NULL, "L", 1, &top, &bottom) && top == 25 - 14 && bottom == 25);   /* the fringe row is ink at 1 */
	assert(ui_font_ink_rows(NULL, "L", UI_FONT_INK_HALF, &top, &bottom) && bottom == 24);
	/* the title face: the app's own font file */
	font_name = "title font (assets/romfs/font/title.bcfnt)";
	load_title_font(argv[1]);
	reset();
	float title = ui_face_baseline(UI_FACE_TITLE);
	assert(measured[1]);
	assert(fabsf(title - 34 * 30.0f / 42) < 1e-4f);   /* Michroma's letters stand on its header's baselinePos 34 of 42 */
	edge_rows = 34;
	title_surfaces();
	printf("  %-46s letters at %5.2f/30: titles on the red rule\n", font_name, title);
	/* the dev log line: only with the dev switch */
	logged = 0; ui_font_log(); assert(!logged);
	dev = true; ui_font_log(); assert(logged == 2);
	printf("text_baseline PASS: %d placements on ruled lines across 7 fonts\n", checks);
	return 0;
}
