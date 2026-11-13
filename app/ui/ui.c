// Minimal text and rectangle drawing using the system font.
#include "ui_fit.h"
#include "ui_mark.h"
#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "libretro.h"
#include "theme.h"
#include "icons.h"
#include "cold_code.h"
#include "app_log.h"
#include "app_config.h"
#include "settings_ui.h"
#include "platform_video.h"
#include "app_input.h"
#include "presets.h"
#include "ui_list.h"
#include "ui_pause.h"
#include "ui.h"
#include "libui.h"
#include "ui_actions.h"
#include "bb_registry.h"
#include "scene.h"
#include "ui_motion.h"
#include "ui_destination.h"
#include "devtools.h"
#include "breadbox_import.h"
#include "bb_index.h"
#include "bb_update.h"
#include "uisound.h"

static C2D_TextBuf textbuf;

void ui_init(void)
{
	textbuf = C2D_TextBufNew(UI_METRIC_8192);
}

void ui_exit(void)
{
	C2D_TextBufDelete(textbuf);
}

// call once per frame before drawing text
void ui_begin(void)
{
	C2D_TextBufClear(textbuf);
 breadbox_import_poll();
 // Settings and nested menus must also finish presentation publication.
 // Running cores own the system arena, so menus inside a game skip this.
 if (!bb_session_system() && !bb_update_installing() && !bb_update_committed()) bb_index_publish();
}

void ui_text(float x, float y, float size, u32 color, const char *fmt, ...)
{
	char buf[UI_METRIC_256];
	C2D_Text text;
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (!C2D_TextParse(&text, textbuf, buf))
		return; // text buffer full this frame
	C2D_TextOptimize(&text);
	C2D_DrawText(&text, C2D_WithColor, x, y, UI_METRIC_0_5F, size, size, color);
}

// The shared fit policy (ui_fit.h) with the system font's measure. Results are cached by text, size, floor,
// width and font: menus ask for the same rows every frame.
static float fit_measure(const char *s, float size, void *font)
{
	static C2D_TextBuf measure_buf;
	if (!measure_buf && !(measure_buf = C2D_TextBufNew(UI_METRIC_128)))
		return UI_METRIC_0;
	C2D_Text t;
	float w = UI_METRIC_0;
	C2D_TextBufClear(measure_buf);
	if (font ? !C2D_TextFontParse(&t, font, measure_buf, s) : !C2D_TextParse(&t, measure_buf, s))
		return UI_METRIC_0;
	C2D_TextGetDimensions(&t, size, size, &w, NULL);
	return w;
}
// s fitted into width: at size, or shrunk down to min, or cut with "..." at min; *out_size the size to draw.
const char *ui_fit_ex(C2D_Font font, const char *s, float size, float min, float width, float *out_size)
{
	static struct { char src[UI_METRIC_256]; float size, min, width, got; C2D_Font font; char out[UI_METRIC_256]; } cache[UI_METRIC_32];
	static unsigned next;
	if (!s) s = "";
	for (unsigned i = UI_METRIC_0; i < UI_METRIC_32; i++)
		if (cache[i].size == size && cache[i].min == min && cache[i].width == width && cache[i].font == font && !strcmp(cache[i].src, s)) {
			if (out_size) *out_size = cache[i].got;
			return cache[i].out;
		}
	unsigned slot = next++ % UI_METRIC_32;
	ui_fit_copy(cache[slot].src, sizeof(cache[slot].src), s, sizeof(cache[slot].src));
	cache[slot].size = size;
	cache[slot].min = min;
	cache[slot].width = width;
	cache[slot].font = font;
	cache[slot].got = ui_fit_bounded(cache[slot].src, size, min, width, fit_measure, font, cache[slot].out, sizeof(cache[slot].out), NULL);
	if (out_size) *out_size = cache[slot].got;
	return cache[slot].out;
}
// Several lines on the shared policy (ui_fit.h ui_fit_lines_bounded), cached like ui_fit_ex: a card asks every frame.
int ui_fit_lines(C2D_Font font, const char *s, float size, float min, float width, int max, ui_fit_line *out, float *out_size)
{
	static struct { char src[UI_METRIC_256]; float size, min, width, got; int max, n; C2D_Font font; ui_fit_line line[UI_FIT_LINES_MAX]; } cache[UI_METRIC_4];
	static unsigned next;
	if (!s) s = "";
	if (max > UI_FIT_LINES_MAX) max = UI_FIT_LINES_MAX;
	unsigned slot = UI_METRIC_4;
	for (unsigned i = UI_METRIC_0; i < UI_METRIC_4; i++)
		if (cache[i].size == size && cache[i].min == min && cache[i].width == width && cache[i].max == max && cache[i].font == font && !strcmp(cache[i].src, s))
			slot = i;
	if (slot == UI_METRIC_4) {
		slot = next++ % UI_METRIC_4;
		ui_fit_copy(cache[slot].src, sizeof(cache[slot].src), s, sizeof(cache[slot].src));
		cache[slot].size = size;
		cache[slot].min = min;
		cache[slot].width = width;
		cache[slot].max = max;
		cache[slot].font = font;
		cache[slot].n = ui_fit_lines_bounded(cache[slot].src, size, min, width, max, fit_measure, font, cache[slot].line, &cache[slot].got);
	}
	if (out_size) *out_size = cache[slot].got;
	memcpy(out, cache[slot].line, sizeof(ui_fit_line) * (size_t)cache[slot].n);
	return cache[slot].n;
}
// Shorten s with "..." so it fits width pixels at the given text size (no shrinking).
const char *ui_fit(const char *s, float size, float width) { return ui_fit_ex(NULL, s, size, size, width, NULL); }

void ui_rect(float x, float y, float w, float h, u32 color)
{
	C2D_DrawRectSolid(x, y, UI_METRIC_0_5F, w, h, color);
}

void ui_text_ex(C2D_Font font, float x, float y, float size, u32 color, u32 flags, const char *s)
{
	C2D_Text text;
	if (!C2D_TextFontParse(&text, font, textbuf, s))
		return;
	C2D_TextOptimize(&text);
	C2D_DrawText(&text, C2D_WithColor | flags, x, y, UI_METRIC_0_7F, size, size, color);
}

// Word-wrapped text in the system font, width in pixels.
void ui_text_wrap(float x, float y, float size, u32 color, float width, const char *s)
{
	C2D_Text text;
	if (!C2D_TextParse(&text, textbuf, s))
		return;
	C2D_TextOptimize(&text);
	C2D_DrawText(&text, C2D_WithColor | C2D_WordWrap, x, y, UI_METRIC_0_7F, size, size, color, width);
}

// A tap on the bottom screen's header (title and colour rule) means Back.
u32 ui_header_back(u32 keys)
{
	if (!(keys & KEY_TOUCH))
		return keys;
	touchPosition p;
	hidTouchRead(&p);
	return p.py < UI_METRIC_30 ? (keys & ~KEY_TOUCH) | KEY_B : keys;
}

// The same title font, colours and rule for Controls, Settings and the game menu.
void ui_menu_header(const char *title, const char *right)
{
	const theme_t *th = theme();
	float size;   // the title font at the header size, cut with "..." before the scope (never under the floor)
	const char *t = ui_fit_ex(libui_font(), title, UI_HEADER_TITLE_SIZE, UI_TEXT_FLOOR, (right ? UI_HEADER_RIGHT - UI_METRIC_80 : UI_HEADER_RIGHT) - UI_HEADER_LEFT, &size);
	ui_text_ex(libui_font(), UI_HEADER_LEFT, UI_HEADER_TITLE_Y, size, th->heading, UI_METRIC_0, t);
	if (right)
		ui_text_ex(NULL, UI_HEADER_RIGHT, UI_HEADER_RIGHT_Y, UI_TEXT_FLOOR, th->heading_scope, C2D_AlignRight, ui_fit(right, UI_TEXT_FLOOR, UI_METRIC_140));
	for (int i=UI_METRIC_0; i<UI_METRIC_4; i++)
		C2D_DrawRectSolid(UI_HEADER_LEFT+i*UI_METRIC_18, UI_HEADER_RULE_Y, UI_METRIC_0_2F, UI_METRIC_16, UI_METRIC_2, th->accent[i]);
}

// Pixel-sized ASCII labels with explicit baseline/tracking. A positive width
// fits the glyph ink bounds exactly (the bottom-screen brand is 62 pixels wide).
// A negative width is a maximum: shrink long labels uniformly, never enlarge.
void ui_text_pixels(C2D_Font font, float x, float baseline, float pixels, float spacing,
	float width, u32 color, u32 align, const char *s)
{
	C2D_Text glyphs[UI_METRIC_32];
	size_t n = strlen(s);
	if (!n || n > UI_METRIC_32) return;
	// Citro2D normalizes every font to a 30px cell, including the title font.
	float sy = pixels / UI_METRIC_30;
	float metrics = UI_METRIC_30_0F / C2D_FontGetInfo(font)->tglp->cellHeight;
	float advances[UI_METRIC_32], total = spacing * (n-UI_METRIC_1);
	float first_left = UI_METRIC_0, last_right = UI_METRIC_0;
	for (size_t i = UI_METRIC_0; i < n; i++) {
		char ch[] = { s[i], UI_METRIC_0 };
		if (!C2D_TextFontParse(&glyphs[i], font, textbuf, ch)) return;
		C2D_TextGetDimensions(&glyphs[i], sy, sy, &advances[i], NULL);
		charWidthInfo_s *cw = C2D_FontGetCharWidthInfo(font, C2D_FontGlyphIndexFromCodePoint(font, s[i]));
		if (i == UI_METRIC_0) first_left = cw->left*metrics*sy;
		if (i == n-UI_METRIC_1) last_right = (cw->left + cw->glyphWidth)*metrics*sy;
		total += advances[i];
	}
	if (width != UI_METRIC_0) total += last_right - advances[n-UI_METRIC_1] - first_left;
	float fit = total > UI_METRIC_0 ? (width > UI_METRIC_0 ? width/total : width < UI_METRIC_0 ? fminf(UI_METRIC_1, -width/total) : UI_METRIC_1) : UI_METRIC_1;
	total *= fit;
	if (align == C2D_AlignCenter) x -= total/UI_METRIC_2;
	else if (align == C2D_AlignRight) x -= total;
	if (width != UI_METRIC_0) x -= first_left*fit;
	for (size_t i = UI_METRIC_0; i < n; i++) {
		C2D_DrawText(&glyphs[i], C2D_WithColor | C2D_AtBaseline,
			x, baseline, UI_METRIC_0_7F, sy*fit, width < UI_METRIC_0 ? sy*fit : sy, color);
		x += (advances[i] + spacing)*fit;
	}
}

// Locate the actual ink of the six shoulder/stick labels, once per app run (ui_font.c reads the sheet).
// Font cell height includes blank padding, so centring the cell/baseline leaves
// text visibly off-centre beside vector symbols.
void ui_button_text(float x, float y, float pixels, float spacing, u32 color, int id)
{
	static bool ready[UI_METRIC_6];
	static float midline[UI_METRIC_6];
	int slot = id-RETRO_DEVICE_ID_JOYPAD_L;
	if (slot < UI_METRIC_0 || slot >= UI_METRIC_6) return;
	const char *s = input_ps1_name(id);
	if (!ready[slot]) {
		const TGLP_s *g = C2D_FontGetInfo(NULL)->tglp;
		int top, bottom;
		midline[slot] = ui_font_ink_rows(NULL, s, UI_METRIC_1, &top, &bottom) ?
			((top+bottom+UI_METRIC_1)*UI_METRIC_0_5F-g->baselinePos)*UI_METRIC_30/g->cellHeight : -UI_METRIC_11;
		ready[slot] = true;
	}
	ui_text_pixels(NULL, x, y-midline[slot]*pixels/UI_METRIC_30, pixels, spacing, -UI_METRIC_68,
		color, C2D_AlignCenter, s);
}

// Controller/action marks are geometry, independent of the installed fonts.
// Coordinates are centred; size is the full height of the square face symbols.
static void symbol_line(float x, float y, float x2, float y2, float stroke, u32 c)
{
	C2D_DrawLine(x, y, c, x2, y2, c, stroke, UI_METRIC_0_70F);
	C2D_DrawCircleSolid(x, y, UI_METRIC_0_70F, stroke/UI_METRIC_2, c);
	C2D_DrawCircleSolid(x2, y2, UI_METRIC_0_70F, stroke/UI_METRIC_2, c);
}

// Shared action palette: touch accents and symbols, Controls highlights/icons.
u32 ui_input_color(int id)
{
 if(id==INPUT_TURBO_A)id=RETRO_DEVICE_ID_JOYPAD_A;
 if(id==INPUT_TURBO_B)id=RETRO_DEVICE_ID_JOYPAD_B;
 // Bounds include Hide (-1) and the three frontend actions (16..18).
 return g_input_palette[id >= -UI_METRIC_1 && id <= UI_METRIC_18 ? id+UI_METRIC_1 : UI_METRIC_3];
}

bool ui_input_symbol(int id, float x, float y, float size, u32 color)
{
 if(id==INPUT_TURBO_A)id=RETRO_DEVICE_ID_JOYPAD_A;
 if(id==INPUT_TURBO_B)id=RETRO_DEVICE_ID_JOYPAD_B;
	float r = size/UI_METRIC_2, stroke = size/UI_METRIC_11;
	color = (ui_input_color(id) & UI_METRIC_0XFFFFFF) | (color & UI_METRIC_0XFF000000);
	if (id == RETRO_DEVICE_ID_JOYPAD_X) {
		symbol_line(x, y-r*UI_METRIC_0_9F, x-r, y+r*UI_METRIC_0_9F, stroke, color);
		symbol_line(x-r, y+r*UI_METRIC_0_9F, x+r, y+r*UI_METRIC_0_9F, stroke, color);
		symbol_line(x+r, y+r*UI_METRIC_0_9F, x, y-r*UI_METRIC_0_9F, stroke, color);
	} else if (id == RETRO_DEVICE_ID_JOYPAD_A) {
		// Outline without painting over the button's pressed gradient.
		for (int j = UI_METRIC_0; j < UI_METRIC_32; j++) {
			float a = j*(UI_METRIC_2*(float)M_PI/UI_METRIC_32), b = (j+UI_METRIC_1)*(UI_METRIC_2*(float)M_PI/UI_METRIC_32);
			C2D_DrawLine(x+r*cosf(a), y+r*sinf(a), color,
				x+r*cosf(b), y+r*sinf(b), color, stroke, UI_METRIC_0_70F);
		}
	} else if (id == RETRO_DEVICE_ID_JOYPAD_B) {
		symbol_line(x-r*UI_METRIC_0_8F, y-r*UI_METRIC_0_8F, x+r*UI_METRIC_0_8F, y+r*UI_METRIC_0_8F, stroke, color);
		symbol_line(x+r*UI_METRIC_0_8F, y-r*UI_METRIC_0_8F, x-r*UI_METRIC_0_8F, y+r*UI_METRIC_0_8F, stroke, color);
	} else if (id == RETRO_DEVICE_ID_JOYPAD_Y) {
		symbol_line(x-r*UI_METRIC_0_85F, y-r*UI_METRIC_0_85F, x+r*UI_METRIC_0_85F, y-r*UI_METRIC_0_85F, stroke, color);
		symbol_line(x+r*UI_METRIC_0_85F, y-r*UI_METRIC_0_85F, x+r*UI_METRIC_0_85F, y+r*UI_METRIC_0_85F, stroke, color);
		symbol_line(x+r*UI_METRIC_0_85F, y+r*UI_METRIC_0_85F, x-r*UI_METRIC_0_85F, y+r*UI_METRIC_0_85F, stroke, color);
		symbol_line(x-r*UI_METRIC_0_85F, y+r*UI_METRIC_0_85F, x-r*UI_METRIC_0_85F, y-r*UI_METRIC_0_85F, stroke, color);
	} else if (id >= RETRO_DEVICE_ID_JOYPAD_UP && id <= RETRO_DEVICE_ID_JOYPAD_RIGHT) {
		float dx = id == RETRO_DEVICE_ID_JOYPAD_LEFT ? -UI_METRIC_1 : id == RETRO_DEVICE_ID_JOYPAD_RIGHT ? UI_METRIC_1 : UI_METRIC_0;
		float dy = id == RETRO_DEVICE_ID_JOYPAD_UP ? -UI_METRIC_1 : id == RETRO_DEVICE_ID_JOYPAD_DOWN ? UI_METRIC_1 : UI_METRIC_0;
		C2D_DrawTriangle(x+dx*r*UI_METRIC_0_825F, y+dy*r*UI_METRIC_0_825F, color,
			x-dx*r*UI_METRIC_0_825F-dy*r*UI_METRIC_0_8F, y-dy*r*UI_METRIC_0_825F+dx*r*UI_METRIC_0_8F, color,
			x-dx*r*UI_METRIC_0_825F+dy*r*UI_METRIC_0_8F, y-dy*r*UI_METRIC_0_825F-dx*r*UI_METRIC_0_8F, color, UI_METRIC_0_70F);
	} else if (id == INPUT_FF_TOGGLE || id == INPUT_FF_HOLD || id == INPUT_REWIND_HOLD) {
		float dir = id == INPUT_REWIND_HOLD ? -UI_METRIC_1 : UI_METRIC_1;
		for (int j = UI_METRIC_0; j < UI_METRIC_2; j++) {
			float cx = x + (j ? UI_METRIC_0_5F : -UI_METRIC_0_5F)*r;
			C2D_DrawTriangle(cx+dir*r*UI_METRIC_0_45F, y, color,
				cx-dir*r*UI_METRIC_0_45F, y-r*UI_METRIC_0_65F, color,
				cx-dir*r*UI_METRIC_0_45F, y+r*UI_METRIC_0_65F, color, UI_METRIC_0_70F);
		}
	} else if (id == RETRO_DEVICE_ID_JOYPAD_START) {
		// Rounded triangular PS1 START button.
		float ax=x-r*UI_METRIC_0_725F, bx=x+r*UI_METRIC_0_725F, h=r*UI_METRIC_0_55F;
		C2D_DrawTriangle(ax, y-h, color, ax, y+h, color, bx, y, color, UI_METRIC_0_70F);
		symbol_line(ax, y-h, ax, y+h, stroke, color);
		symbol_line(ax, y+h, bx, y, stroke, color);
		symbol_line(bx, y, ax, y-h, stroke, color);
	} else if (id == RETRO_DEVICE_ID_JOYPAD_SELECT) {
		// Flat rounded SELECT pill, with quarter-circle corners.
		float w=r*UI_METRIC_1_8F, h=r*UI_METRIC_0_75F, cr=r*UI_METRIC_0_18F;
		C2D_DrawRectSolid(x-w/UI_METRIC_2+cr, y-h/UI_METRIC_2, UI_METRIC_0_70F, w-UI_METRIC_2*cr, h, color);
		C2D_DrawRectSolid(x-w/UI_METRIC_2, y-h/UI_METRIC_2+cr, UI_METRIC_0_70F, w, h-UI_METRIC_2*cr, color);
		for (int i=UI_METRIC_0; i<UI_METRIC_4; i++)
			C2D_DrawCircleSolid(x+(i&UI_METRIC_1 ? UI_METRIC_1 : -UI_METRIC_1)*(w/UI_METRIC_2-cr),
				y+(i&UI_METRIC_2 ? UI_METRIC_1 : -UI_METRIC_1)*(h/UI_METRIC_2-cr), UI_METRIC_0_70F, cr, color);
	} else return false;
	return true;
}

// ---------------------------------------------------------------- floaty list
// Shared critically damped row and scroll motion; selected rows stay still at rest.
static float flist_dt(flist_t *f)
{
 u32 now = (u32)(svcGetSystemTick() / (SYSCLOCK_ARM11 / UI_MOTION_CLOCK_HZ));
 float dt = f->init && f->motion_tick ? (float)(u32)(now - f->motion_tick) / UI_MOTION_CLOCK_HZ : UI_METRIC_0;
 f->motion_tick = now;
 return fminf(dt, UI_MOTION_UI_S);
}

void flist_update(flist_t *f, int sel, int n, int rows)
{
 rows=UI_LIST_ROWS;
	float dt = flist_dt(f);
	float lift_step = UI_METRIC_1 - expf(-UI_MOTION_SETTLE_RATE * dt);
	if (!f->init) {
		f->hl = sel;
		f->init = true;
	}
	// keep the selection a row away from the edges when it can be
	int top = (int)(f->scroll_target + UI_METRIC_0_5F);
	if (sel < top + UI_METRIC_1) top = sel - UI_METRIC_1;
	if (sel > top + rows - UI_METRIC_2) top = sel - rows + UI_METRIC_2;
	if (top > n - rows) top = n - rows;
	if (top < UI_METRIC_0) top = UI_METRIC_0;
	f->scroll_target = top;
	ui_motion_settle(&f->hl, &f->hl_v, sel, UI_MOTION_SETTLE_RATE, dt);
	ui_motion_settle(&f->scroll, &f->scroll_v, top, UI_MOTION_SETTLE_RATE, dt);
	for (int i = UI_METRIC_0; i < FLIST_MAX && i < n; i++) {
		float target = i == sel ? UI_METRIC_1_0F : UI_METRIC_0_0F;
		f->lift[i] += (target - f->lift[i]) * lift_step;
	}
}

// jump straight to a selection: no scrolling or highlight glide
void flist_snap(flist_t *f, int sel, int n, int rows)
{
 rows=UI_LIST_ROWS;
	memset(f, UI_METRIC_0, sizeof(*f));
	int top = sel - rows / UI_METRIC_2;
	if (top > n - rows) top = n - rows;
	if (top < UI_METRIC_0) top = UI_METRIC_0;
	f->scroll = f->scroll_target = top;
	f->hl = sel;
	if (sel >= UI_METRIC_0 && sel < FLIST_MAX)
		f->lift[sel] = UI_METRIC_1;
	f->init = true;
	f->motion_tick = (u32)(svcGetSystemTick() / (SYSCLOCK_ARM11 / UI_MOTION_CLOCK_HZ));
}

int flist_row_at(const flist_t *f, float y0, float row_h, float py)
{
 y0=UI_LIST_TOP;row_h=UI_LIST_ROW;
 if(py<y0 || py>=y0+UI_LIST_ROWS*row_h)return -UI_METRIC_1;
	return (int)floorf(f->scroll + (py - y0) / row_h);
}

void flist_draw_sections(const flist_t *f, int n, int sel, float x, float y0, float w, float row_h, int rows,
	const char *(*label)(int, void *), const char *(*value)(int, void *), void *ud, u32 accent, bool (*header)(int, void *))
{
	const theme_t *th = theme();
	if (value == input_control_value) accent = input_control_color(sel);
	// highlight: soft glow, gradient bar, accent edge
	float hy = y0 + (f->hl - f->scroll) * row_h;
	int highlight_row = (int)floorf(f->hl + UI_METRIC_0_5F);
	if (
 n>UI_METRIC_0 && sel>=UI_METRIC_0 &&
 hy > y0 - row_h && hy < y0 + rows * row_h &&
	    !(header && highlight_row >= UI_METRIC_0 && highlight_row < n && header(highlight_row, ud))) {

		C2D_DrawRectSolid(x - UI_METRIC_2, hy - UI_METRIC_2, UI_METRIC_0_2F, w + UI_METRIC_4, row_h + UI_METRIC_4, th->select_glow);
		C2D_DrawRectangle(x, hy, UI_METRIC_0_21F, w, row_h, th->select_top, th->select_bottom, th->select_top, th->select_bottom);
		C2D_DrawRectSolid(x, hy, UI_METRIC_0_22F, UI_METRIC_3, row_h, accent);
	}
	int first = (int)floorf(f->scroll) - UI_METRIC_1;
	for (int i = first < UI_METRIC_0 ? UI_METRIC_0 : first; i < n && i <= f->scroll + rows; i++) {
		float ry = y0 + (i - f->scroll) * row_h;
		// rows fade out as they slide past the list's edges
		float edge = UI_METRIC_1_0F;
		if (ry < y0) edge = UI_METRIC_1_0F - (y0 - ry) / row_h;
		if (ry + row_h > y0 + rows * row_h) edge = UI_METRIC_1_0F - (ry + row_h - (y0 + rows * row_h)) / row_h;
		if (edge <= UI_METRIC_0_02F)
			continue;
		if (header && header(i, ud)) {
			u8 alpha = (u8)(UI_METRIC_255 * edge);
			const char *head = ui_fit(label(i, ud), UI_LIST_TEXT, w - UI_METRIC_24);
			ui_text_ex(NULL, x + UI_METRIC_12, ry + (row_h - UI_METRIC_30 * UI_LIST_TEXT) / UI_METRIC_2, UI_LIST_TEXT, theme_alpha(th->section, alpha), UI_METRIC_0, head);
			float hx = x + UI_METRIC_12 + ui_text_width(NULL, UI_LIST_TEXT, head) + UI_METRIC_8;   // the rule after the head
			if (hx < x + w - UI_METRIC_12)
				C2D_DrawRectSolid(hx, ry + row_h / UI_METRIC_2, UI_METRIC_0_3F, x + w - UI_METRIC_12 - hx, UI_METRIC_1, theme_alpha(th->section_line, alpha));
			continue;
		}
		float lift = i < FLIST_MAX ? f->lift[i] : (i == sel);

		u8 a = (u8)(UI_METRIC_255 * edge);
		u32 col = theme_row(th, lift, a);
		float tx = x + UI_METRIC_12 + UI_METRIC_8 * lift;
		const char *s = label(i, ud);
 const char *right=value?value(i,ud):NULL;
 // the value keeps at most half the row (ui_fit.h: cut at the floor), the label the rest
 if(right && *right && strcmp(right,"*"))right=ui_fit(right,UI_LIST_TEXT,(w-UI_METRIC_16)*UI_CARD_VALUE_MAX);
 float reserve=right&&*right?ui_text_width(NULL,UI_LIST_TEXT,right)+UI_METRIC_16:UI_METRIC_0;
 s=ui_fit(s,UI_LIST_TEXT,w-(tx-x)-UI_METRIC_8-reserve);
		ui_text_ex(NULL, tx, ry + (row_h - UI_METRIC_30 * UI_LIST_TEXT) / UI_METRIC_2, UI_LIST_TEXT, col, UI_METRIC_0, s);
		if (value) {
			const char *v = value(i, ud);
			if (value == input_control_value && input_control_slot(i) >= UI_METRIC_0) {
				int id = input_map_get(input_control_slot(i));
				if (id >= INPUT_FF_TOGGLE && id <= INPUT_REWIND_HOLD && !((id == INPUT_REWIND_HOLD) ? game_capabilities().rewind : game_capabilities().fast_forward)) id = -UI_METRIC_1;
				C2D_Text measure;
				float vw=UI_METRIC_0;
				C2D_TextFontParse(&measure, NULL, textbuf, v);
				C2D_TextGetDimensions(&measure, UI_LIST_TEXT, UI_LIST_TEXT, &vw, NULL);
				ui_input_symbol(id, x+w-UI_METRIC_8-vw-UI_METRIC_14, ry+row_h/UI_METRIC_2, UI_METRIC_12,
					theme_alpha(th->value, a));
			}
            if(v && !strcmp(v,"*")) {
                ui_star(x+w-UI_METRIC_14,ry+row_h/UI_METRIC_2,UI_METRIC_5_5F,ui_bar_color(UI_COL_STAR),UI_METRIC_0,UI_METRIC__6F);
                continue;
            }
            if (v && v[UI_METRIC_0])
				ui_text_ex(NULL, x + w - UI_METRIC_8, ry + (row_h - UI_METRIC_30 * UI_LIST_TEXT) / UI_METRIC_2, UI_LIST_TEXT,
					theme_alpha(th->value, a), C2D_AlignRight, ui_fit(v, UI_LIST_TEXT, (w - UI_METRIC_16) * UI_CARD_VALUE_MAX));
		}
	}
}

void flist_draw(const flist_t *f, int n, int sel, float x, float y0, float w, float row_h, int rows,
	const char *(*label)(int, void *), const char *(*value)(int, void *), void *ud, u32 accent)
{
	flist_draw_sections(f, n, sel, x, y0, w, row_h, rows, label, value, ud, accent, NULL);
}

// Gameplay menus use the original PSX3D pause background for either core.
static void ui_menu_backdrop(void)
{
 if(bb_session_active()) {
  C2D_DrawRectSolid(UI_METRIC_0,UI_METRIC_0,UI_METRIC__1F,UI_BOTTOM_SCREEN_WIDTH,UI_SCREEN_HEIGHT,theme()->surface);
  return;
 }
 scene_draw(true);
}

// Canonical list surface: exactly the v0.5.2 library geometry and scrollbar.
void ui_list_draw(const flist_t *f, int n, int sel,
 const char *(*label)(int, void *), const char *(*value)(int, void *), void *ud,
 u32 accent, bool (*heading)(int, void *))
{
 ui_menu_backdrop();
 flist_draw_sections(f,n,sel,UI_METRIC_0,UI_LIST_TOP,UI_LIST_WIDTH,UI_LIST_ROW,UI_LIST_ROWS,
                     label,value,ud,accent,heading);
 if(n>UI_LIST_ROWS) {
  float height=UI_METRIC_200_0F*UI_LIST_ROWS/n;
  C2D_DrawRectSolid(UI_METRIC_315,UI_METRIC_4+UI_METRIC_200_0F*f->scroll/n,UI_METRIC__3F,UI_METRIC_3,height,theme()->scrollbar);
 }
}

void ui_list_context(const char *title,const char *right)
{
 // Context occupies the quiet strip below the list, never the first row.
 // Not drawn (owner 2026-11-05: no status chatter): the header, the open tab or the screen says where you are.
 (void)title;(void)right;
}
static u32 list_keys(const flist_t *f,int n,int *sel,u32 keys,bool horizontal)
{
 static u32 previous; static int ticks;
 u32 mask=KEY_UP|KEY_DOWN|KEY_CPAD_UP|KEY_CPAD_DOWN|KEY_L|KEY_R;
 if(horizontal) mask|=KEY_LEFT|KEY_RIGHT|KEY_CPAD_LEFT|KEY_CPAD_RIGHT;
 u32 held=input_keys_held()&mask;
 if(held!=previous || (keys&held)) {previous=held;ticks=UI_METRIC_0;}
 else if(held && ++ticks>UI_REPEAT_DELAY && (ticks-UI_REPEAT_DELAY)%UI_REPEAT_INTERVAL==UI_METRIC_0) keys|=held;
 if(n<UI_METRIC_0) return keys;
 if(!n) {*sel=UI_METRIC_0;return keys&~KEY_A;}
 u32 up=KEY_UP|KEY_CPAD_UP,down=KEY_DOWN|KEY_CPAD_DOWN;
 if(horizontal){up|=KEY_LEFT|KEY_CPAD_LEFT;down|=KEY_RIGHT|KEY_CPAD_RIGHT;}
 if((keys&up)&&*sel>UI_METRIC_0)--*sel;
 if((keys&down)&&*sel+UI_METRIC_1<n)++*sel;
 if(keys&KEY_TOUCH) {
  touchPosition t;hidTouchRead(&t);
  int row=flist_row_at(f,UI_LIST_TOP,UI_LIST_ROW,t.py);
  if(t.px<UI_LIST_WIDTH && t.py>=UI_LIST_TOP && t.py<UI_LIST_TOP+UI_LIST_ROW*UI_LIST_ROWS && row>=UI_METRIC_0 && row<n) {
   if(row==*sel) keys|=KEY_A;
   *sel=row;
  }
 }
 return keys;
}
u32 ui_list_input(const flist_t *f,int n,int *sel,u32 keys,bool horizontal)
{
 int before=*sel;keys=list_keys(f,n,sel,keys,horizontal);
 if(*sel!=before)sfx_now(SFX_MOVE);   // only an actual move: the end of the list is silent
 return keys;
}


// Shared companions, dialogs and game messages retain the PSX3D typography.
void ui_pause_title(const char *title)
{
 const theme_t *th=theme();float dx=ui_depth(-UI_METRIC__8F);
 C2D_DrawRectSolid(UI_METRIC_0,UI_METRIC_0,UI_METRIC__6F,UI_TOP_SCREEN_WIDTH,UI_SCREEN_HEIGHT,th->veil);
 ui_text_ex(libui_font(),UI_METRIC_201+dx,UI_METRIC_87,UI_FONT_PAUSED,theme_alpha(THEME_TITLE_SHADOW(th),UI_METRIC_150),C2D_AlignCenter,"PAUSED");
 ui_text_ex(libui_font(),UI_METRIC_200+dx,UI_METRIC_86,UI_FONT_PAUSED,th->game_title,C2D_AlignCenter,"PAUSED");
 ui_text_ex(NULL,UI_METRIC_200+dx,UI_METRIC_136,UI_FONT_SUBTITLE,th->game_sub,C2D_AlignCenter,ui_fit(title,UI_FONT_SUBTITLE,UI_METRIC_380));
 for(int i=UI_METRIC_0;i<UI_METRIC_4;i++)C2D_DrawRectSolid(UI_METRIC_140+i*UI_METRIC_30+dx,UI_METRIC_128,UI_METRIC__65F,UI_METRIC_28,UI_METRIC_2,th->accent[i]);
}
void ui_companion_title_at(const char *title,const char *subtitle,float y,u8 alpha)
{
 float size=UI_METRIC__9F,w=ui_text_width(libui_font(),size,title),x=UI_METRIC_200+ui_depth(-UI_METRIC__8F);
 if(w>UI_METRIC_380)size*=UI_METRIC_380/w;
 ui_text_ex(libui_font(),x+UI_METRIC_1,y+UI_METRIC_1,size,theme_alpha(THEME_TITLE_SHADOW(theme()),alpha*UI_METRIC_5/UI_METRIC_8),C2D_AlignCenter,title);
 ui_text_ex(libui_font(),x,y,size,theme_alpha(theme()->game_title,alpha),C2D_AlignCenter,title);
 if(subtitle && *subtitle)ui_text_ex(NULL,x,y+UI_METRIC_36,UI_FONT_VALUE,theme_alpha(theme()->game_sub,alpha),C2D_AlignCenter,subtitle);
}
void ui_companion_title(const char *title,const char *subtitle)
{
 ui_companion_title_at(title,subtitle,UI_METRIC_178,UI_METRIC_255);
}

/* Keep menu-only state out of the hot text helpers' shared BSS anchor. */
static const ui_destination *dialog_scene __attribute__((section(".bss.ui_destination")));
static bool dialog_drawing __attribute__((section(".bss.ui_destination")));
PSX3D_COLD const ui_destination *ui_dialog_scene_set(const ui_destination *scene)
{
 const ui_destination *previous = dialog_scene;
 dialog_scene = scene;
 return previous;
}
PSX3D_COLD void ui_destination_draw_top(const ui_destination *scene, void (*overlay)(void *), void *userdata)
{
 if (scene && scene->draw_top) scene->draw_top(scene->userdata);
 if (overlay) {
  C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
  overlay(userdata);
  C2D_Flush();
  C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
 }
}
void ui_menu_top(void *ud)
{
 (void)ud;
 // A dialog may receive this dispatcher as its backdrop. Cut any cycle
 // through it before calling another scene; no caller identity checks needed.
 if (dialog_drawing || !dialog_scene || !dialog_scene->draw_top) {scene_draw(false);return;}
 dialog_drawing=true;
 dialog_scene->draw_top(dialog_scene->userdata);
 dialog_drawing=false;
}
// Text wrapped by width at a size: broken after a space when one fits, else between whole characters (a path, a
// title without spaces); paragraph breaks kept. Draws lines first..first+count-1 from (x, y), step apart; returns
// the number of lines the whole text takes.
static int wrap_text(const char *s, float size, float width, float x, float y, float step, int first, int count, u32 color)
{
	char line[UI_METRIC_256];
	int lines = UI_METRIC_0;
	while (*s) {
		size_t len = strcspn(s, "\n"), used = UI_METRIC_0, fit = UI_METRIC_0, at_break = UI_METRIC_0;
		// the longest head of this paragraph that fits: before a space or after "/", "_" or "-" (a folder name)
		// when one fits, else whole characters
		while (used < len) {
			size_t next = used + UI_METRIC_1;
			while (next < len && ((unsigned char)s[next] & UI_METRIC_0XC0) == UI_METRIC_0X80) next++;
			if (next >= sizeof(line)) break;
			memcpy(line, s, next); line[next] = UI_METRIC_0;
			if (ui_text_width(NULL, size, line) > width) break;
			used = next;
			if (used < len && (s[used] == ' ' || strchr("/_-", s[used - UI_METRIC_1]))) at_break = used;
		}
		fit = used >= len ? len : at_break ? at_break : used ? used : len;
		if (lines >= first && lines < first + count) {
			ui_fit_copy(line, sizeof(line), s, fit);
			ui_text_ex(NULL, x, y + (lines - first) * step, size, color, UI_METRIC_0, line);
		}
		lines++;
		s += fit;
		while (*s == ' ') s++;
		if (*s == '\n' && fit >= len) s++;
	}
	return lines;
}
// Dialog text at the text floor, split into pages that end above the bar (UI_DIALOG_LINES lines a page).
// Returns the page count; draws page `page` when draw is true.
static int dialog_text(const char *s, int page, bool draw)
{
	int lines = wrap_text(s, UI_DIALOG_TEXT, UI_DIALOG_TEXT_W, UI_DIALOG_TEXT_X, UI_DIALOG_TEXT_Y, UI_DIALOG_LINE,
		page * UI_DIALOG_LINES, draw ? UI_DIALOG_LINES : UI_METRIC_0, theme()->help_text);
	return lines ? (lines + UI_DIALOG_LINES - UI_METRIC_1) / UI_DIALOG_LINES : UI_METRIC_1;
}
int ui_dialog_pages(const char *text) { return dialog_text(text ? text : "", UI_METRIC_0, false); }
void ui_dialog_draw_page(const char *title,const char *scope,const char *text,int page,const ui_button_t *bar,int n)
{
 char counter[UI_METRIC_24];
 int pages=ui_dialog_pages(text);
 scope=NULL;   /* no scope word ("BreadBox", the game): the page counter only, when there are pages */
 if(pages>UI_METRIC_1){snprintf(counter,sizeof(counter),"%d / %d",page+UI_METRIC_1,pages);scope=counter;}
 ui_menu_backdrop();ui_menu_header(title,scope);
 dialog_text(text?text:"",page,true);ui_bar_draw(bar,n);
}
void ui_dialog_draw(const char *title,const char *scope,const char *text,const ui_button_t *bar,int n)
{ui_dialog_draw_page(title,scope,text,UI_METRIC_0,bar,n);}
static void ui_progress_body(const char *title,const char *message,int percent)
{
 ui_menu_header(title,NULL);
 ui_text_ex(NULL,UI_PROGRESS_TEXT_CENTER_X,UI_PROGRESS_TEXT_Y,UI_DIALOG_TEXT,theme()->help_text,C2D_AlignCenter,ui_fit(message,UI_DIALOG_TEXT,UI_PROGRESS_TEXT_WIDTH));
 if(percent>=UI_METRIC_0) {
  if(percent>UI_PROGRESS_PERCENT_MAX)percent=UI_PROGRESS_PERCENT_MAX;
  ui_rect(UI_PROGRESS_BAR_X,UI_PROGRESS_BAR_Y,UI_PROGRESS_BAR_WIDTH,UI_PROGRESS_BAR_HEIGHT,theme()->section_line);
  ui_rect(UI_PROGRESS_BAR_X,UI_PROGRESS_BAR_Y,UI_PROGRESS_BAR_WIDTH_F*percent/UI_PROGRESS_PERCENT_MAX,UI_PROGRESS_BAR_HEIGHT,theme()->accent[UI_METRIC_0]);
 }
}
void ui_progress_draw(const char *title,const char *message,int percent)
{ui_menu_backdrop();ui_progress_body(title,message,percent);}
void ui_progress_card_draw(const char *title,const char *message,int percent)
{
 C2D_DrawRectSolid(UI_METRIC_0,UI_METRIC_0,UI_METRIC__1F,UI_TOP_SCREEN_WIDTH,UI_SCREEN_HEIGHT,theme()->surface);
 ui_progress_body(title,message,percent);
}
void ui_game_blank(void *unused)
{
 (void)unused;
 C2D_DrawRectSolid(UI_METRIC_0,UI_METRIC_0,UI_METRIC_0_1F,UI_BOTTOM_SCREEN_WIDTH,UI_SCREEN_HEIGHT,UI_GAME_CLEAR);
}
void ui_game_notice(const char *text,bool alert)
{
 C2D_DrawRectSolid(UI_GAME_NOTICE_LEFT,UI_METRIC_178,UI_METRIC_0_66F,UI_GAME_NOTICE_WIDTH,UI_METRIC_22,g_game_palette.osd_bg);   // centered on the top game screen, below the text depth
 ui_text_ex(NULL,UI_GAME_NOTICE_LEFT+UI_METRIC_4,UI_METRIC_178+UI_METRIC_4,UI_FONT_VALUE,alert?g_game_palette.notice:g_game_palette.text,UI_METRIC_0,ui_fit(text,UI_FONT_VALUE,UI_GAME_NOTICE_WIDTH-UI_METRIC_2*UI_METRIC_4));
}

// ---------------------------------------------------------------- help panel
// Lower part of the top screen: what the selected option does and its speed
// impact. y0 is the top of the panel.
void ui_help_panel(float y0, const char *title, const char *impact, const char *text)
{
	if ((!text || !text[UI_METRIC_0]) && !impact)
		return;
	float dx = ui_depth(-UI_METRIC_0_4F);
	const theme_t *th = theme();
	C2D_DrawRectSolid(UI_METRIC_12 + dx, y0, UI_METRIC_0_66F, UI_METRIC_376, UI_METRIC_236 - y0, th->help_bg);
	C2D_DrawRectSolid(UI_METRIC_12 + dx, y0, UI_METRIC_0_67F, UI_METRIC_3, UI_METRIC_236 - y0, th->help_bar);
	float y = y0 + UI_METRIC_6;
	if (title) {
		ui_text_ex(NULL, UI_METRIC_24 + dx, y, UI_FONT_BODY, th->help_title, UI_METRIC_0, title);
		y += UI_METRIC_18;
	}
	if (impact) {
		ui_text_ex(NULL, UI_METRIC_24 + dx, y, UI_FONT_META, th->help_impact, UI_METRIC_0, impact);
		y += UI_METRIC_15;
	}
	if (text && text[UI_METRIC_0]) {
		C2D_Text t;
		if (C2D_TextParse(&t, textbuf, text)) {
			C2D_TextOptimize(&t);
			C2D_DrawText(&t, C2D_WithColor | C2D_WordWrap, UI_METRIC_24 + dx, y, UI_METRIC_0_82F, UI_METRIC_0_4F, UI_METRIC_0_4F,
				th->help_text, UI_METRIC_352_0F);
		}
	}
}

// Destructive settings actions require a fresh A press. B always cancels.
// n: tabs shown, the last n of bar (1: a notice with only A's answer).
typedef struct { const char *title, *message, *action; ui_button_t bar[UI_METRIC_2]; char word[UI_METRIC_24], back[UI_METRIC_24]; int page, pages, n; } confirm_t;
static void confirm_draw(void *ud)
{
	confirm_t *c = ud;
 ui_button_t bar[UI_METRIC_2]={c->bar[UI_METRIC_0],c->bar[UI_METRIC_1]};
 if(c->page+UI_METRIC_1<c->pages){bar[UI_METRIC_1].label="Next";bar[UI_METRIC_1].icon=UI_ICON_RIGHT;}   /* the answer comes on the last page */
 ui_dialog_draw_page(c->title,config_in_game()?ui_fit(bb_session_title(),UI_FONT_META,UI_METRIC_150):"BreadBox",c->message,c->page,bar+UI_METRIC_2-c->n,c->n);
}

bool ui_confirm(const char *title, const char *message, bool show_game)
{
	return ui_confirm_action(title, message, "A  Reset to defaults", show_game);
}

bool ui_confirm_action(const char *title, const char *message, const char *action, bool show_game)
{
	return ui_confirm_choice(title, message, action, NULL, show_game);
}

// A tab's caption from a prompt like "A  Reset game": the words after the key.
static const char *confirm_label(const char *s, char key)
{
	if (s[UI_METRIC_0] == key && s[UI_METRIC_1] == ' ')
		s++;
	while (*s == ' ') s++;
	return s;
}

// back: what B does (NULL: Cancel). With the default B the A tab shows the
// action's first word ("Reset game" -> Reset); with a named B both tabs show
// their whole action.
// A question with two tabs (the room bar's look), the safe answer chosen first: left / right (or a tap)
// moves the choice, A answers the chosen one, B always says no.
typedef struct { const char *title, *message; ui_button_t tab[UI_METRIC_2]; int lit; } pick_t;
static void pick_draw(void *ud)
{
	pick_t *p = ud;
	ui_menu_backdrop();
	ui_menu_header(p->title, NULL);
	dialog_text(p->message, UI_METRIC_0, true);   // a two-way question is one page (its callers keep it short)
	ui_tabs_draw(p->tab, UI_METRIC_2, p->lit);
}
bool ui_confirm_pick_update(const char *title, const char *message, const char *yes, const char *no, void (*update)(void *), void *userdata)
{
	pick_t p = { title, message, { { UI_METRIC_0, yes, (u8)ui_action_icon(yes, UI_ICON_EXIT), false }, { UI_METRIC_0, no, (u8)ui_action_icon(no, UI_ICON_CROSS), false } }, UI_METRIC_1 };
	bool released = false;
 sfx_play(SFX_DIALOG);ui_swap_enter();
	while (aptMainLoop()) {
		if (update) update(userdata);
		input_poll();
		dev_tick_keys();
		u32 held = input_keys_held(), k = input_keys_down();
		int hit = ui_tabs_hit(UI_METRIC_2, k), lit = p.lit;
		if (hit >= UI_METRIC_0 && released) {
			if (hit == p.lit) k |= KEY_A;
			p.lit = hit;
		}
		if (released && (k & (KEY_LEFT | KEY_CPAD_LEFT | KEY_L))) p.lit = UI_METRIC_0;
		if (released && (k & (KEY_RIGHT | KEY_CPAD_RIGHT | KEY_R))) p.lit = UI_METRIC_1;
		if (p.lit != lit) sfx_now(SFX_MOVE);
        k=ui_swap_confirm_keys(released ? k : UI_METRIC_0,UI_SWAP_MENU);
		if (released && (k & KEY_B)) { sfx_play(SFX_DENY); return false; }
		if (released && (k & KEY_A)) { sfx_play(p.lit == UI_METRIC_0 ? SFX_CONFIRM : SFX_DENY); return p.lit == UI_METRIC_0; }
		if (!(held & (KEY_A | KEY_B | KEY_TOUCH))) released = true;
		ui_begin();
		video_present_vsync(false, ui_menu_top, pick_draw, &p);
	}
	return false;
}
bool ui_confirm_pick(const char *title, const char *message, const char *yes, const char *no)
{
 return ui_confirm_pick_update(title,message,yes,no,NULL,NULL);
}


// Each answer's own icon (ui_actions.h): "Try again", "Other choices", "Next", "Quit"... not one check and one
// cross for every dialog. bar[0] is B's answer, bar[1] A's.
static void confirm_icons(ui_button_t *bar, const char *a_answer, const char *b_answer)
{
	bar[UI_METRIC_1].icon = (u8)ui_action_icon(a_answer, UI_ICON_CHECK);
	bar[UI_METRIC_0].icon = (u8)ui_action_icon(b_answer ? b_answer : "Cancel", UI_ICON_CROSS);
}
static bool confirm_run(const char *title, const char *message, const char *action, const char *back, bool show_game, int n)
{
 u64 scene_tick = svcGetSystemTick();
	const char *label = confirm_label(action, 'A');
	confirm_t c = { title, message, action, { { KEY_B, "Cancel", UI_ICON_CROSS, false }, { KEY_A, NULL, UI_ICON_CHECK, false } }, "", "", UI_METRIC_0, ui_dialog_pages(message), n };
	snprintf(c.word, sizeof(c.word), "%.*s", back ? (int)strlen(label) : (int)strcspn(label, " "), label);
	c.bar[UI_METRIC_1].label = c.word;
	if (back) {
		snprintf(c.back, sizeof(c.back), "%s", confirm_label(back, 'B'));
		c.bar[UI_METRIC_0].label = c.back;
	}
	confirm_icons(c.bar, label, back);
	bool released = false;
 sfx_play(SFX_DIALOG);ui_swap_enter();
	while (aptMainLoop()) {
		input_poll();
		// The pause menu also keeps answering the remote loop while this is open.
		if ((settings_ui_active() || show_game) && (dev_tick(show_game) || dev_ui_pending())) return false;
		else if (!settings_ui_active() && !show_game) dev_tick_keys();
		u32 held = input_keys_held(), k = ui_bar_input(c.bar + UI_METRIC_2 - c.n, c.n, input_keys_down());
        k=ui_swap_confirm_keys(released ? k : UI_METRIC_0,UI_SWAP_MENU);
		if (released && (k & KEY_B)) { sfx_play(SFX_DENY); return false; }
		// pages: A or down goes on, up goes back; A answers only on the last page
		if (released && (k & (KEY_DOWN | KEY_CPAD_DOWN)) && c.page + UI_METRIC_1 < c.pages) { c.page++; sfx_play(SFX_MOVE); }
		else if (released && (k & (KEY_UP | KEY_CPAD_UP)) && c.page) { c.page--; sfx_play(SFX_MOVE); }
		else if (released && (k & KEY_A) && c.page + UI_METRIC_1 < c.pages) { c.page++; released = false; sfx_play(SFX_MOVE); }
		else if (released && (k & KEY_A)) { sfx_play(SFX_CONFIRM); return true; }
		else sfx_cancel();   // A before the press was let go: nothing happened
		if (!(held & (KEY_A | KEY_B | KEY_TOUCH))) released = true;
		ui_begin();
		ui_pause_frame();
        u64 now = svcGetSystemTick();
        if (!show_game && dialog_scene && dialog_scene->tick)
            dialog_scene->tick(dialog_scene->userdata, (float)(now - scene_tick) / SYSCLOCK_ARM11);
        scene_tick = now;
		video_present_vsync(show_game, show_game ? NULL : ui_menu_top, confirm_draw, &c);
	}
	return false;
}
bool ui_confirm_choice(const char *title, const char *message, const char *action, const char *back, bool show_game)
{
	return confirm_run(title, message, action, back, show_game, UI_METRIC_2);
}
// Something the player only takes note of: one answer (A); B closes it too.
void ui_confirm_ok(const char *title, const char *message, bool show_game)
{
	confirm_run(title, message, "A  OK", NULL, show_game, UI_METRIC_1);
}

// A question with up to UI_BAR_MAX tabs (ui_confirm_pick's look): left / right (or a tap) moves the choice,
// A answers the chosen one; B, HOME > Close or a power-off answer -1. `lit` is chosen first: the safe answer,
// so A pressed twice from the screen before never picks another one (and ui_swap_confirm_keys drops it).
typedef struct { const char *title, *message; ui_button_t tab[UI_BAR_MAX]; int n, lit; } pick_n_t;
static void pick_n_draw(void *ud)
{
	pick_n_t *p = ud;
	ui_menu_backdrop();
	ui_menu_header(p->title, NULL);
	dialog_text(p->message, UI_METRIC_0, true);   // one page: callers keep it short
	ui_tabs_draw(p->tab, p->n, p->lit);
}
PSX3D_COLD int ui_confirm_pick_n(const char *title, const char *message, const char *const *answers, int n, int lit)
{
	pick_n_t p = { title, message, { { UI_METRIC_0, NULL, UI_METRIC_0, false } }, n < UI_BAR_MAX ? n : UI_BAR_MAX, lit };
	if (p.n < UI_METRIC_1 || p.lit < UI_METRIC_0 || p.lit >= p.n)
		return -UI_METRIC_1;
	for (int i = UI_METRIC_0; i < p.n; i++)
		p.tab[i] = (ui_button_t){ UI_METRIC_0, answers[i], (u8)ui_action_icon(answers[i], UI_ICON_CHECK), false };
	bool released = false;
	sfx_play(SFX_DIALOG);   // the sounds and the menu-only swap of every other question (ui_confirm_pick_update)
	ui_swap_enter();
	while (aptMainLoop()) {
		input_poll();
		dev_tick_keys();
		u32 held = input_keys_held(), k = input_keys_down();
		int hit = ui_tabs_hit(p.n, k), lit = p.lit;
		if (hit >= UI_METRIC_0 && released) {
			if (hit == p.lit) k |= KEY_A;
			p.lit = hit;
		}
		if (released && (k & (KEY_LEFT | KEY_CPAD_LEFT | KEY_L)) && p.lit > UI_METRIC_0) p.lit--;
		if (released && (k & (KEY_RIGHT | KEY_CPAD_RIGHT | KEY_R)) && p.lit + UI_METRIC_1 < p.n) p.lit++;
		if (p.lit != lit) sfx_now(SFX_MOVE);
		k = ui_swap_confirm_keys(released ? k : UI_METRIC_0, UI_SWAP_MENU);
		if (released && (k & KEY_B)) { sfx_play(SFX_DENY); return -UI_METRIC_1; }
		if (released && (k & KEY_A)) { sfx_play(SFX_CONFIRM); return p.lit; }
		if (!(held & (KEY_A | KEY_B | KEY_TOUCH))) released = true;
		ui_begin();
		video_present_vsync(false, ui_menu_top, pick_n_draw, &p);
	}
	return -UI_METRIC_1;
}

// ---------------------------------------------------------------- button bar
// The bar of touch buttons that replaces every menu's prompt line, in the
// owner's chosen style (button-refs ref7, like TWiLight Menu++ and
// Universal-Updater): flat tabs filling the strip edge to edge, thin dividers,
// a small icon over a one-word caption, the main action (A) filled in the
// theme's highlight colour, no button letters. B is always leftmost, A
// rightmost; physical presses light the matching tab. Colours come from the
// theme exactly as the UI mock's barTokensFor() derives them.







static float text_width(float size, const char *s) { return ui_text_width(NULL, size, s); }

float ui_text_width(C2D_Font font, float size, const char *s)
{
	static C2D_TextBuf wbuf;
	if (!wbuf && !(wbuf = C2D_TextBufNew(UI_METRIC_256)))
#ifdef PSX3D_TEST_PRESETS
		;   // the host test's text stubs need no buffer
#else
		return UI_METRIC_0;
#endif
	C2D_Text t;
	float w = UI_METRIC_0;
	if (wbuf)
		C2D_TextBufClear(wbuf);
	if (C2D_TextFontParse(&t, font, wbuf, s))
		C2D_TextGetDimensions(&t, size, size, &w, NULL);
	return w;
}

static u32 mix(u32 a, u32 b, float t, int alpha)
{
	int r = (int)((a & UI_METRIC_255) + (((int)(b & UI_METRIC_255)) - (int)(a & UI_METRIC_255)) * t);
	int g = (int)(((a >> UI_METRIC_8) & UI_METRIC_255) + (((int)((b >> UI_METRIC_8) & UI_METRIC_255)) - (int)((a >> UI_METRIC_8) & UI_METRIC_255)) * t);
	int bl = (int)(((a >> UI_METRIC_16) & UI_METRIC_255) + (((int)((b >> UI_METRIC_16) & UI_METRIC_255)) - (int)((a >> UI_METRIC_16) & UI_METRIC_255)) * t);
	return C2D_Color32(r, g, bl, alpha);
}

u32 ui_bar_color(int which)
{
	const theme_t *th = theme();
	switch (which) {
	case UI_COL_BAR: return th->bar ? th->bar : mix(th->surface, th->shadow, UI_METRIC_0_35F, UI_METRIC_255);
	case UI_COL_RULE: return mix(th->surface, th->select_top, UI_METRIC_0_55F, UI_METRIC_255);
	case UI_COL_NOTICE: return theme_alpha(th->surface, UI_METRIC_225);
	case UI_COL_STAR: return th->help_impact;
	}
	return UI_METRIC_0;
}

static u32 flash_key;
static int flash_t;

// A five-point star from triangles; hollow draws a smaller one in its colour inside.
void ui_star(float cx, float cy, float r, u32 c, u32 hollow, float z)
{
	float px[UI_METRIC_10], py[UI_METRIC_10];
	for (int i = UI_METRIC_0; i < UI_METRIC_10; i++) {
		float a = -(float)M_PI / UI_METRIC_2 + i * (float)M_PI / UI_METRIC_5, rr = i & UI_METRIC_1 ? r * UI_METRIC_0_45F : r;
		px[i] = cx + cosf(a) * rr;
		py[i] = cy + sinf(a) * rr;
	}
	for (int i = UI_METRIC_0; i < UI_METRIC_10; i++) {
		int j = (i + UI_METRIC_1) % UI_METRIC_10;
		C2D_DrawTriangle(cx, cy, c, px[i], py[i], c, px[j], py[j], c, z);
	}
	if (hollow)
		ui_star(cx, cy + r * UI_METRIC_0_06F, r * UI_METRIC_0_55F, hollow, UI_METRIC_0, z + UI_METRIC_0_001F);
}

// an open circular arrow; dir 1 clockwise (rescan), -1 anticlockwise (reset)
static void bar_arc(float cx, float cy, float s, u32 ink, int dir, float z)
{
	float r = s * UI_METRIC_0_4F, w = fmaxf(UI_METRIC_1_4F, s * UI_METRIC_0_14F), a0 = -(float)M_PI * UI_METRIC_0_35F, a1 = (float)M_PI * UI_METRIC_1_25F;
	for (int i = UI_METRIC_0; i < UI_METRIC_10; i++) {
		float p = a0 + (a1 - a0) * i / UI_METRIC_10, q = a0 + (a1 - a0) * (i + UI_METRIC_1) / UI_METRIC_10;
		C2D_DrawLine(cx + dir * cosf(p) * r, cy + sinf(p) * r, ink, cx + dir * cosf(q) * r, cy + sinf(q) * r, ink, w, z);
	}
	float hx = cx + dir * cosf(a0) * r, hy = cy + sinf(a0) * r;
	C2D_DrawTriangle(hx - dir * s * UI_METRIC_0_3F, hy - s * UI_METRIC_0_12F, ink, hx + dir * s * UI_METRIC_0_12F, hy - s * UI_METRIC_0_3F, ink,
		hx + dir * s * UI_METRIC_0_08F, hy + s * UI_METRIC_0_2F, ink, z);
}

// an arrow along y: dir -1 up, 1 down
static void bar_vertical_arrow(float cx, float cy, float s, u32 ink, int dir, float z)
{
	float tip = cy + dir * s * UI_METRIC_0_45F, base = cy + dir * s * UI_METRIC_0_02F;
	C2D_DrawTriangle(cx, tip, ink, cx - s * UI_METRIC_0_36F, base, ink, cx + s * UI_METRIC_0_36F, base, ink, z);
	float y0 = fminf(base, cy - dir * s * UI_METRIC_0_4F), y1 = fmaxf(base, cy - dir * s * UI_METRIC_0_4F);
	C2D_DrawRectSolid(cx - s * UI_METRIC_0_11F, y0, z, s * UI_METRIC_0_22F, y1 - y0, ink);
}

// Icons are simple shapes drawn in code, about s pixels across. All of them sit
// under the caption text, which ui_text_ex draws at 0.7.
static void bar_icon(int icon, float cx, float cy, float s, u32 ink, u32 bg, bool on)
{
	const float z = UI_METRIC_0_68F, w = fmaxf(UI_METRIC_1_5F, s * UI_METRIC_0_15F);
	// the Dreamstale glyph (icons.c); a favourite is a gold star on a soft fill
	u32 gold = ui_bar_color(UI_COL_STAR);
	if (icon == UI_ICON_STAR && on)
		ui_star(cx, cy + UI_METRIC_0_5F, s * UI_METRIC_0_42F, theme_alpha(gold, UI_METRIC_150), UI_METRIC_0, z - UI_METRIC_0_001F);
	if (icons_glyph(icon, cx, cy, icon == UI_ICON_STAR && on ? gold : ink, z))
		return;
	switch (icon) {
	case UI_ICON_STAR:
		ui_star(cx, cy, s * UI_METRIC_0_62F, on ? ui_bar_color(UI_COL_STAR) : ink, on ? UI_METRIC_0 : bg, z);
		break;
	case UI_ICON_SORT:
		for (int i = UI_METRIC_0; i < UI_METRIC_3; i++)
			C2D_DrawRectSolid(cx - s * UI_METRIC_0_5F, cy - s * UI_METRIC_0_42F + i * s * UI_METRIC_0_36F, z, s * (UI_METRIC_1 - i * UI_METRIC_0_3F), fmaxf(UI_METRIC_1_5F, s * UI_METRIC_0_16F), ink);
		break;
	case UI_ICON_RESCAN:
		bar_arc(cx, cy, s, ink, UI_METRIC_1, z);
		break;
	case UI_ICON_RESET:
		bar_arc(cx, cy, s, ink, -UI_METRIC_1, z);
		break;
	case UI_ICON_LEFT:
		C2D_DrawTriangle(cx - s * UI_METRIC_0_32F, cy, ink, cx + s * UI_METRIC_0_22F, cy - s * UI_METRIC_0_4F, ink, cx + s * UI_METRIC_0_22F, cy + s * UI_METRIC_0_4F, ink, z);
		break;
	case UI_ICON_RIGHT:
		C2D_DrawTriangle(cx + s * UI_METRIC_0_32F, cy, ink, cx - s * UI_METRIC_0_22F, cy - s * UI_METRIC_0_4F, ink, cx - s * UI_METRIC_0_22F, cy + s * UI_METRIC_0_4F, ink, z);
		break;
	case UI_ICON_FOLDER:
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_5F, cy - s * UI_METRIC_0_3F, z, s * UI_METRIC_0_4F, s * UI_METRIC_0_15F, ink);
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_5F, cy - s * UI_METRIC_0_18F, z, s, s * UI_METRIC_0_55F, ink);
		break;
	case UI_ICON_BACK:   // the mock's arrow: a head and a shaft
		C2D_DrawTriangle(cx - s * UI_METRIC_0_5F, cy, ink, cx, cy - s * UI_METRIC_0_42F, ink, cx, cy + s * UI_METRIC_0_42F, ink, z);
		C2D_DrawRectSolid(cx - UI_METRIC_1, cy - s * UI_METRIC_0_15F, z, s * UI_METRIC_0_5F + UI_METRIC_1, s * UI_METRIC_0_3F, ink);
		break;
	case UI_ICON_PLAY:
		C2D_DrawTriangle(cx - s * UI_METRIC_0_3F, cy - s * UI_METRIC_0_42F, ink, cx - s * UI_METRIC_0_3F, cy + s * UI_METRIC_0_42F, ink, cx + s * UI_METRIC_0_42F, cy, ink, z);
		break;
	case UI_ICON_START:   // back to the start: a bar, PLAY's triangle pointing back at it
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_45F, cy - s * UI_METRIC_0_42F, z, w, s * UI_METRIC_0_84F, ink);
		C2D_DrawTriangle(cx + s * UI_METRIC_0_42F, cy - s * UI_METRIC_0_42F, ink, cx + s * UI_METRIC_0_42F, cy + s * UI_METRIC_0_42F, ink, cx - s * UI_METRIC_0_24F, cy, ink, z);
		break;
	case UI_ICON_CHECK:
		C2D_DrawLine(cx - s * UI_METRIC_0_45F, cy, ink, cx - s * UI_METRIC_0_12F, cy + s * UI_METRIC_0_35F, ink, w * UI_METRIC_1_3F, z);
		C2D_DrawLine(cx - s * UI_METRIC_0_18F, cy + s * UI_METRIC_0_35F, ink, cx + s * UI_METRIC_0_48F, cy - s * UI_METRIC_0_38F, ink, w * UI_METRIC_1_3F, z);
		break;
	case UI_ICON_CROSS:
		C2D_DrawLine(cx - s * UI_METRIC_0_38F, cy - s * UI_METRIC_0_38F, ink, cx + s * UI_METRIC_0_38F, cy + s * UI_METRIC_0_38F, ink, w * UI_METRIC_1_3F, z);
		C2D_DrawLine(cx - s * UI_METRIC_0_38F, cy + s * UI_METRIC_0_38F, ink, cx + s * UI_METRIC_0_38F, cy - s * UI_METRIC_0_38F, ink, w * UI_METRIC_1_3F, z);
		break;
	case UI_ICON_PLUS:
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_45F, cy - w * UI_METRIC_0_65F, z, s * UI_METRIC_0_9F, w * UI_METRIC_1_3F, ink);
		C2D_DrawRectSolid(cx - w * UI_METRIC_0_65F, cy - s * UI_METRIC_0_45F, z, w * UI_METRIC_1_3F, s * UI_METRIC_0_9F, ink);
		break;
	case UI_ICON_TRASH:   // lid with a handle, a body with two slots
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_12F, cy - s * UI_METRIC_0_5F, z, s * UI_METRIC_0_24F, s * UI_METRIC_0_1F, ink);
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_45F, cy - s * UI_METRIC_0_4F, z, s * UI_METRIC_0_9F, s * UI_METRIC_0_12F, ink);
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_34F, cy - s * UI_METRIC_0_22F, z, s * UI_METRIC_0_68F, s * UI_METRIC_0_7F, ink);
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_14F, cy - s * UI_METRIC_0_12F, z + UI_METRIC_0_001F, UI_METRIC_1, s * UI_METRIC_0_5F, bg);
		C2D_DrawRectSolid(cx + s * UI_METRIC_0_12F, cy - s * UI_METRIC_0_12F, z + UI_METRIC_0_001F, UI_METRIC_1, s * UI_METRIC_0_5F, bg);
		break;
	case UI_ICON_SAVE:   // an arrow down into a tray
	case UI_ICON_LOAD:   // an arrow up out of it
		bar_vertical_arrow(cx, cy - s * UI_METRIC_0_1F, s * UI_METRIC_0_85F, ink, icon == UI_ICON_SAVE ? UI_METRIC_1 : -UI_METRIC_1, z);
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_48F, cy + s * UI_METRIC_0_42F, z, s * UI_METRIC_0_96F, w, ink);
		break;
	case UI_ICON_UP:
		bar_vertical_arrow(cx, cy, s, ink, -UI_METRIC_1, z);
		break;
	case UI_ICON_OPEN:   // a chevron
		C2D_DrawLine(cx - s * UI_METRIC_0_18F, cy - s * UI_METRIC_0_42F, ink, cx + s * UI_METRIC_0_24F, cy, ink, w * UI_METRIC_1_3F, z);
		C2D_DrawLine(cx + s * UI_METRIC_0_24F, cy, ink, cx - s * UI_METRIC_0_18F, cy + s * UI_METRIC_0_42F, ink, w * UI_METRIC_1_3F, z);
		break;
	case UI_ICON_CHANGE:   // two small arrow heads apart
		C2D_DrawTriangle(cx - s * UI_METRIC_0_5F, cy, ink, cx - s * UI_METRIC_0_1F, cy - s * UI_METRIC_0_36F, ink, cx - s * UI_METRIC_0_1F, cy + s * UI_METRIC_0_36F, ink, z);
		C2D_DrawTriangle(cx + s * UI_METRIC_0_5F, cy, ink, cx + s * UI_METRIC_0_1F, cy - s * UI_METRIC_0_36F, ink, cx + s * UI_METRIC_0_1F, cy + s * UI_METRIC_0_36F, ink, z);
		break;
	case UI_ICON_TOGGLE:   // a switch: a capsule and its knob
		C2D_DrawCircleSolid(cx - s * UI_METRIC_0_26F, cy, z, s * UI_METRIC_0_26F, ink);
		C2D_DrawCircleSolid(cx + s * UI_METRIC_0_26F, cy, z, s * UI_METRIC_0_26F, ink);
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_26F, cy - s * UI_METRIC_0_26F, z, s * UI_METRIC_0_52F, s * UI_METRIC_0_52F, ink);
		C2D_DrawCircleSolid(cx + s * UI_METRIC_0_26F, cy, z + UI_METRIC_0_001F, s * UI_METRIC_0_17F, bg);
		break;
	case UI_ICON_DISC:
		C2D_DrawCircleSolid(cx, cy, z, s * UI_METRIC_0_48F, ink);
		C2D_DrawCircleSolid(cx, cy, z + UI_METRIC_0_001F, s * UI_METRIC_0_13F, bg);
		break;
	case UI_ICON_EXIT:   // an open box and an arrow leaving it
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_48F, cy - s * UI_METRIC_0_42F, z, w, s * UI_METRIC_0_84F, ink);
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_48F, cy - s * UI_METRIC_0_42F, z, s * UI_METRIC_0_4F, w, ink);
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_48F, cy + s * UI_METRIC_0_42F - w, z, s * UI_METRIC_0_4F, w, ink);
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_18F, cy - w / UI_METRIC_2, z, s * UI_METRIC_0_4F, w, ink);
		C2D_DrawTriangle(cx + s * UI_METRIC_0_5F, cy, ink, cx + s * UI_METRIC_0_18F, cy - s * UI_METRIC_0_3F, ink, cx + s * UI_METRIC_0_18F, cy + s * UI_METRIC_0_3F, ink, z);
		break;
	case UI_ICON_TV:   // a CRT with rabbit ears
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_55F, cy - s * UI_METRIC_0_3F, z, s * UI_METRIC_1_1F, s * UI_METRIC_0_8F, ink);
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_4F, cy - s * UI_METRIC_0_18F, z + UI_METRIC_0_001F, s * UI_METRIC_0_6F, s * UI_METRIC_0_56F, bg);
		C2D_DrawLine(cx, cy - s * UI_METRIC_0_3F, ink, cx - s * UI_METRIC_0_32F, cy - s * UI_METRIC_0_62F, ink, w, z);
		C2D_DrawLine(cx, cy - s * UI_METRIC_0_3F, ink, cx + s * UI_METRIC_0_32F, cy - s * UI_METRIC_0_62F, ink, w, z);
		break;
	case UI_ICON_BOOKS:   // spines on a shelf, one leaning
		for (int i = UI_METRIC_0; i < UI_METRIC_3; i++)
			C2D_DrawRectSolid(cx - s * UI_METRIC_0_5F + i * s * UI_METRIC_0_3F, cy - s * (UI_METRIC_0_4F - i % UI_METRIC_2 * UI_METRIC_0_1F), z, s * UI_METRIC_0_2F, s * (UI_METRIC_0_8F - i % UI_METRIC_2 * UI_METRIC_0_1F), ink);
		C2D_DrawLine(cx + s * UI_METRIC_0_3F, cy + s * UI_METRIC_0_4F, ink, cx + s * UI_METRIC_0_55F, cy - s * UI_METRIC_0_3F, ink, w * UI_METRIC_1_3F, z);
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_6F, cy + s * UI_METRIC_0_4F, z, s * UI_METRIC_1_2F, w, ink);
		break;
	case UI_ICON_SCOPE:   // a screen with a trace, two knobs
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_6F, cy - s * UI_METRIC_0_42F, z, s * UI_METRIC_1_2F, s * UI_METRIC_0_84F, ink);
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_48F, cy - s * UI_METRIC_0_3F, z + UI_METRIC_0_001F, s * UI_METRIC_0_6F, s * UI_METRIC_0_6F, bg);
		for (int i = UI_METRIC_0; i < UI_METRIC_4; i++)
			C2D_DrawLine(cx - s * UI_METRIC_0_42F + i * s * UI_METRIC_0_12F, cy + (i % UI_METRIC_2 ? s : -s) * UI_METRIC_0_15F, ink,
				cx - s * UI_METRIC_0_3F + i * s * UI_METRIC_0_12F, cy + (i % UI_METRIC_2 ? -s : s) * UI_METRIC_0_15F, ink, w, z + UI_METRIC_0_002F);
		C2D_DrawCircleSolid(cx + s * UI_METRIC_0_35F, cy - s * UI_METRIC_0_12F, z + UI_METRIC_0_001F, s * UI_METRIC_0_1F, bg);
		C2D_DrawCircleSolid(cx + s * UI_METRIC_0_35F, cy + s * UI_METRIC_0_18F, z + UI_METRIC_0_001F, s * UI_METRIC_0_08F, bg);
		break;
	case UI_ICON_QR:   // three finder squares
		for (int i = UI_METRIC_0; i < UI_METRIC_3; i++) {
			float qx = cx + (i == UI_METRIC_1 ? s * UI_METRIC_0_1F : -s * UI_METRIC_0_5F), qy = cy + (i == UI_METRIC_2 ? s * UI_METRIC_0_1F : -s * UI_METRIC_0_5F);
			C2D_DrawRectSolid(qx, qy, z, s * UI_METRIC_0_4F, s * UI_METRIC_0_4F, ink);
			C2D_DrawRectSolid(qx + w, qy + w, z + UI_METRIC_0_001F, s * UI_METRIC_0_4F - w * UI_METRIC_2, s * UI_METRIC_0_4F - w * UI_METRIC_2, bg);
		}
		C2D_DrawRectSolid(cx + s * UI_METRIC_0_2F, cy + s * UI_METRIC_0_2F, z, s * UI_METRIC_0_2F, s * UI_METRIC_0_2F, ink);
		break;
	case UI_ICON_PADS:   // a controller
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_6F, cy - s * UI_METRIC_0_25F, z, s * UI_METRIC_1_2F, s * UI_METRIC_0_5F, ink);
		C2D_DrawCircleSolid(cx - s * UI_METRIC_0_5F, cy + s * UI_METRIC_0_15F, z, s * UI_METRIC_0_25F, ink);
		C2D_DrawCircleSolid(cx + s * UI_METRIC_0_5F, cy + s * UI_METRIC_0_15F, z, s * UI_METRIC_0_25F, ink);
		C2D_DrawCircleSolid(cx + s * UI_METRIC_0_3F, cy - s * UI_METRIC_0_05F, z + UI_METRIC_0_001F, s * UI_METRIC_0_08F, bg);
		C2D_DrawRectSolid(cx - s * UI_METRIC_0_45F, cy - s * UI_METRIC_0_05F, z + UI_METRIC_0_001F, s * UI_METRIC_0_25F, w, bg);
		break;
	case UI_ICON_TOGETHER:   // two controllers, one in front of the other (Play together)
		for (int i = UI_METRIC_0; i < UI_METRIC_2; i++) {
			float q = s * UI_METRIC_0_6F, px = cx + (i ? s * UI_METRIC_0_22F : -s * UI_METRIC_0_22F), py = cy + (i ? s * UI_METRIC_0_2F : -s * UI_METRIC_0_2F), zz = z + i * UI_METRIC_0_004F;
			if (i)   // the front one clears a margin round itself in the bar's colour
				C2D_DrawRectSolid(px - q * UI_METRIC_0_66F - UI_METRIC_1, py - q * UI_METRIC_0_36F - UI_METRIC_1, zz - UI_METRIC_0_001F, q * UI_METRIC_1_2F + UI_METRIC_2, q * UI_METRIC_0_66F + UI_METRIC_2, bg);
			C2D_DrawRectSolid(px - q * UI_METRIC_0_6F, py - q * UI_METRIC_0_25F, zz, q * UI_METRIC_1_2F, q * UI_METRIC_0_5F, ink);
			C2D_DrawCircleSolid(px - q * UI_METRIC_0_5F, py + q * UI_METRIC_0_15F, zz, q * UI_METRIC_0_25F, ink);
			C2D_DrawCircleSolid(px + q * UI_METRIC_0_5F, py + q * UI_METRIC_0_15F, zz, q * UI_METRIC_0_25F, ink);
			C2D_DrawRectSolid(px - q * UI_METRIC_0_45F, py - q * UI_METRIC_0_05F, zz + UI_METRIC_0_001F, q * UI_METRIC_0_25F, w, bg);
		}
		break;
	case UI_ICON_SLIDERS:   // three sliders, knobs at different places
		for (int i = UI_METRIC_0; i < UI_METRIC_3; i++) {
			float ly = cy + (i - UI_METRIC_1) * s * UI_METRIC_0_36F, kx = cx + ((i * UI_METRIC_2 + UI_METRIC_1) % UI_METRIC_3 - UI_METRIC_1) * s * UI_METRIC_0_26F;
			C2D_DrawRectSolid(cx - s * UI_METRIC_0_5F, ly - w / UI_METRIC_2, z, s, w, ink);
			C2D_DrawCircleSolid(kx, ly, z + UI_METRIC_0_001F, s * UI_METRIC_0_14F, ink);
			C2D_DrawCircleSolid(kx, ly, z + UI_METRIC_0_002F, s * UI_METRIC_0_06F, bg);
		}
		break;
	case UI_ICON_PAUSE:   // two bars: the in-game menu (it pauses)
		for (int i = UI_METRIC_0; i < UI_METRIC_2; i++)
			C2D_DrawRectSolid(cx + (i ? UI_METRIC_0_12F : -UI_METRIC_0_36F) * s, cy - s * UI_METRIC_0_42F, z, s * UI_METRIC_0_24F, s * UI_METRIC_0_84F, ink);
		break;
	case UI_ICON_FF:   // two arrow heads right
	case UI_ICON_REWIND: {   // and left
		float dir = icon == UI_ICON_FF ? UI_METRIC_1 : -UI_METRIC_1;
		for (int i = UI_METRIC_0; i < UI_METRIC_2; i++) {
			float tx = cx + (i ? UI_METRIC_0_26F : -UI_METRIC_0_26F) * s;
			C2D_DrawTriangle(tx + dir * s * UI_METRIC_0_24F, cy, ink, tx - dir * s * UI_METRIC_0_2F, cy - s * UI_METRIC_0_36F, ink,
				tx - dir * s * UI_METRIC_0_2F, cy + s * UI_METRIC_0_36F, ink, z);
		}
		break;
	}
	case UI_ICON_SUN:   // a disc with eight rays
		C2D_DrawCircleSolid(cx, cy, z, s * UI_METRIC_0_2F, ink);
		for (int i = UI_METRIC_0; i < UI_METRIC_8; i++) {
			float a = i * UI_CARD_SUN_STEP;
			C2D_DrawLine(cx + cosf(a) * s * UI_METRIC_0_34F, cy + sinf(a) * s * UI_METRIC_0_34F, ink,
				cx + cosf(a) * s * UI_METRIC_0_5F, cy + sinf(a) * s * UI_METRIC_0_5F, ink, w, z);
		}
		break;
	case UI_ICON_MOON: {   // a crescent as geometry (it paints no background): the band between the moon's
		// circle and an equal circle offset up-right, from one tip to the other, in strips
		float R = s * UI_METRIC_0_34F, ox = s * UI_METRIC_0_18F, oy = -s * UI_METRIC_0_12F, pi = (float)M_PI;
		float L = sqrtf(ox * ox + oy * oy), h = sqrtf(R * R - L * L / UI_METRIC_4), px = -oy / L * h, py = ox / L * h;
		float ax = ox / UI_METRIC_2 + px, ay = oy / UI_METRIC_2 + py, bx = ox / UI_METRIC_2 - px, by = oy / UI_METRIC_2 - py;
		float a0 = atan2f(ay, ax), b0 = atan2f(ay - oy, ax - ox);
		float da = remainderf(atan2f(by, bx) - a0, pi + pi), db = remainderf(atan2f(by - oy, bx - ox) - b0, pi + pi);
		da = da > UI_METRIC_0 ? da - pi - pi : da + pi + pi;   // the lit edge goes the long way round
		for (int i = UI_METRIC_0; i < UI_METRIC_12; i++) {
			float t0 = (float)i / UI_METRIC_12, t1 = (float)(i + UI_METRIC_1) / UI_METRIC_12;
			float o0x = cx + R * cosf(a0 + da * t0), o0y = cy + R * sinf(a0 + da * t0), o1x = cx + R * cosf(a0 + da * t1), o1y = cy + R * sinf(a0 + da * t1);
			float q0x = cx + ox + R * cosf(b0 + db * t0), q0y = cy + oy + R * sinf(b0 + db * t0);
			float q1x = cx + ox + R * cosf(b0 + db * t1), q1y = cy + oy + R * sinf(b0 + db * t1);
			C2D_DrawTriangle(o0x, o0y, ink, o1x, o1y, ink, q0x, q0y, ink, z);
			C2D_DrawTriangle(q0x, q0y, ink, o1x, o1y, ink, q1x, q1y, ink, z);
		}
		(void)bg;
		break;
	}
	}
}

void ui_icon_draw(int icon, float cx, float cy, float s, u32 ink, u32 bg) { bar_icon(icon, cx, cy, s, ink, bg, false); }

// buttons without their own icon: B goes back, A confirms
static int bar_icon_of(const ui_button_t *p)
{
	return p->icon ? p->icon : p->key == KEY_B ? UI_ICON_BACK : p->key == KEY_A ? UI_ICON_CHECK : UI_ICON_NONE;
}

// The controller glyph of a key in the system font (A B X Y L R; "" for the others): shown beside a bar icon
// (owner 2026-11-05: icons stand on their own, no words on buttons; the button glyph is fine).
static const char *key_glyph(u32 key)
{
	switch (key) {
	case KEY_A: return "\xee\x80\x80";
	case KEY_B: return "\xee\x80\x81";
	case KEY_X: return "\xee\x80\x82";
	case KEY_Y: return "\xee\x80\x83";
	case KEY_L: return "\xee\x80\x84";
	case KEY_R: return "\xee\x80\x85";
	}
	return "";
}
// SELECT and START have no glyph in the system font: their cue is the button's printed name in an outline,
// as on the console (a controller reference, not a caption)
static const char *key_word(u32 key)
{
	return key == KEY_SELECT ? "SEL" : key == KEY_START ? "START" : NULL;
}
// the width of a key's cue at the glyph size (0: a key with no cue, such as the room's places)
static float key_cue_width(u32 key)
{
	const char *g = key_glyph(key), *w = key_word(key);
	return g[UI_METRIC_0] ? text_width(BAR_GLYPH_SIZE, g) : w ? text_width(BAR_GLYPH_SIZE, w) + BAR_PILL_PAD * UI_METRIC_2 : UI_METRIC_0;
}
static void key_cue(u32 key, float x, float cy, u32 ink, u32 face)
{
	const char *g = key_glyph(key), *w = key_word(key);
	if (g[UI_METRIC_0]) {
		ui_text_ex(NULL, x, cy - UI_METRIC_15 * BAR_GLYPH_SIZE, BAR_GLYPH_SIZE, ink, UI_METRIC_0, g);
		return;
	}
	if (!w)
		return;
	float ww = key_cue_width(key), h = UI_METRIC_30 * BAR_GLYPH_SIZE;
	C2D_DrawRectSolid(x, cy - h / UI_METRIC_2, UI_METRIC_0_68F, ww, h, ink);   // the button's outline
	C2D_DrawRectSolid(x + UI_METRIC_1, cy - h / UI_METRIC_2 + UI_METRIC_1, UI_METRIC_0_68F + UI_METRIC_0_001F, ww - UI_METRIC_2, h - UI_METRIC_2, face);
	ui_text_ex(NULL, x + BAR_PILL_PAD, cy - UI_METRIC_15 * BAR_GLYPH_SIZE, BAR_GLYPH_SIZE, ink, UI_METRIC_0, w);
}
// one tab's icon and its button's cue, centred together in the bar
static void bar_mark(int icon, u32 key, float cx, u32 ink, u32 face, bool on)
{
	float gw = key_cue_width(key);
	float iw = icon != UI_ICON_NONE ? BAR_ICON_R * UI_METRIC_2 : UI_METRIC_0, gap = iw > UI_METRIC_0 && gw > UI_METRIC_0 ? BAR_GLYPH_GAP : UI_METRIC_0;
	float x = cx - (iw + gap + gw) / UI_METRIC_2;
	if (iw > UI_METRIC_0)
		bar_icon(icon, x + BAR_ICON_R, BAR_ICON_ALONE_Y, BAR_ICON_R, ink, face, on);
	if (gw > UI_METRIC_0)
		key_cue(key, x + iw + gap, BAR_ICON_ALONE_Y, ink, face);
}

// B first, A last, the others in the order given; every tab is the same width.
static int bar_layout(const ui_button_t *b, int n, int *order, float *x, float *w)
{
	int m = UI_METRIC_0;
	for (int i = UI_METRIC_0; i < n; i++) if (b[i].key == KEY_B) order[m++] = i;
	for (int i = UI_METRIC_0; i < n; i++) if (b[i].key != KEY_B && b[i].key != KEY_A) order[m++] = i;
	for (int i = UI_METRIC_0; i < n; i++) if (b[i].key == KEY_A) order[m++] = i;
	for (int i = UI_METRIC_0; i < m; i++) {
		x[i] = UI_BOTTOM_SCREEN_WIDTH_F * i / m;
		w[i] = UI_BOTTOM_SCREEN_WIDTH_F * (i + UI_METRIC_1) / m - x[i];
	}
	return m;
}

void ui_bar_draw(const ui_button_t *b, int n)
{
	if (n > UI_BAR_MAX) n = UI_BAR_MAX;
	// everything under the text, which ui_text_ex draws at 0.7
	const theme_t *th = theme();
	u32 strip = ui_bar_color(UI_COL_BAR);
	C2D_DrawRectSolid(UI_METRIC_0, BAR_TOP, UI_METRIC_0_64F, UI_BOTTOM_SCREEN_WIDTH, UI_SCREEN_HEIGHT - BAR_TOP, strip);
	int order[UI_BAR_MAX];
	float xs[UI_BAR_MAX], ws[UI_BAR_MAX];
	int m = bar_layout(b, n, order, xs, ws);
	u32 main_face = mix(th->select_bottom, th->select_top, UI_METRIC_0_2F, UI_METRIC_255);   // the mock's btnMainBottom
	u32 divider = theme_alpha(ui_bar_color(UI_COL_RULE), UI_METRIC_110);
	u32 ink = theme_row(th, UI_METRIC_0_6F, UI_METRIC_255), ink_main = theme_row(th, UI_METRIC_1, UI_METRIC_255);
	if (flash_t > UI_METRIC_0)
		flash_t--;
	for (int i = UI_METRIC_0; i < m; i++) {
		const ui_button_t *p = &b[order[i]];
		float x = xs[i], w = ws[i], cx = x + w / UI_METRIC_2;
		bool main = p->key == KEY_A, lit = flash_t > UI_METRIC_0 && flash_key == p->key;
		u32 face = main ? main_face : strip;
		if (main)
			C2D_DrawRectSolid(x, BAR_TOP, UI_METRIC_0_65F, w, UI_SCREEN_HEIGHT - BAR_TOP, main_face);
		else if (i)
			C2D_DrawRectSolid(x, BAR_TOP + UI_METRIC_4, UI_METRIC_0_65F, UI_METRIC_1, UI_SCREEN_HEIGHT - BAR_TOP - UI_METRIC_8, divider);
		if (lit)
			C2D_DrawRectSolid(x, BAR_TOP, UI_METRIC_0_655F, w, UI_SCREEN_HEIGHT - BAR_TOP, mix(th->select_top, th->text_bright, UI_METRIC_0_45F, UI_METRIC_110 * flash_t / UI_METRIC_9));
		u32 c = main || lit ? ink_main : ink;
		bar_mark(bar_icon_of(p), p->key, cx, c, face, p->on);   // no words: the icon and its button
	}
}

// Tabs (the room's places): the bar's look, the given order, one tab raised.
// Every tab is its icon alone (owner 2026-11-01 for the room's bar, 2026-11-05 for every bar), centred in the bar;
// the raised tab is told by its highlight, never by a word. Hit boxes are the full tab width.
static void tabs_draw(const ui_button_t *b, int n, int lit, bool icons_only)
{
	if (n > UI_BAR_MAX) n = UI_BAR_MAX;
	const theme_t *th = theme();
	u32 strip = ui_bar_color(UI_COL_BAR);
	C2D_DrawRectSolid(UI_METRIC_0, BAR_TOP, UI_METRIC_0_64F, UI_BOTTOM_SCREEN_WIDTH, UI_SCREEN_HEIGHT - BAR_TOP, strip);
	u32 main_face = mix(th->select_bottom, th->select_top, UI_METRIC_0_2F, UI_METRIC_255);
	u32 divider = theme_alpha(ui_bar_color(UI_COL_RULE), UI_METRIC_110);
	u32 ink = theme_row(th, UI_METRIC_0_6F, UI_METRIC_255), ink_main = theme_row(th, UI_METRIC_1, UI_METRIC_255);
	for (int i = UI_METRIC_0; i < n; i++) {
		float x = UI_BOTTOM_SCREEN_WIDTH_F * i / n, w = UI_BOTTOM_SCREEN_WIDTH_F * (i + UI_METRIC_1) / n - x, cx = x + w / UI_METRIC_2;
		bool on = i == lit;
		if (on)
			C2D_DrawRectSolid(x, BAR_TOP, UI_METRIC_0_65F, w, UI_SCREEN_HEIGHT - BAR_TOP, main_face);
		else if (i && i != lit + UI_METRIC_1)
			C2D_DrawRectSolid(x, BAR_TOP + UI_METRIC_4, UI_METRIC_0_65F, UI_METRIC_1, UI_SCREEN_HEIGHT - BAR_TOP - UI_METRIC_8, divider);
		u32 c = on ? ink_main : ink;
		(void)icons_only;
		bar_mark(b[i].icon, b[i].key, cx, c, on ? main_face : strip, b[i].on);
	}
}

void ui_tabs_draw(const ui_button_t *b, int n, int lit) { tabs_draw(b, n, lit, false); }
void ui_tabs_icons(const ui_button_t *b, int n, int lit) { tabs_draw(b, n, lit, true); }

int ui_tabs_hit(int n, u32 keys)
{
	if (!(keys & KEY_TOUCH) || n <= UI_METRIC_0)
		return -UI_METRIC_1;
	touchPosition p;
	hidTouchRead(&p);
	if (p.py < BAR_TOP || (!p.px && !p.py))
		return -UI_METRIC_1;
	int i = (int)(p.px * n / UI_BOTTOM_SCREEN_WIDTH);
	return i < n ? i : n - UI_METRIC_1;
}


u32 ui_bar_input(const ui_button_t *b, int n, u32 keys)
{
	if (n > UI_BAR_MAX) n = UI_BAR_MAX;
	for (int i = UI_METRIC_0; i < n; i++)
		if (keys & b[i].key) {
			flash_key = b[i].key;
			flash_t = UI_METRIC_9;
		}
	if (!(keys & KEY_TOUCH))
		return keys;
	touchPosition p;
	hidTouchRead(&p);
	if (p.py < BAR_TOP || (!p.px && !p.py))
		return keys;
	keys &= ~KEY_TOUCH;   // a tap on the bar is never a tap on the list above it
	int order[UI_BAR_MAX];
	float xs[UI_BAR_MAX], ws[UI_BAR_MAX];
	int m = bar_layout(b, n, order, xs, ws);
	for (int i = UI_METRIC_0; i < m; i++)
		if (p.px >= xs[i] && p.px < xs[i] + ws[i]) {   // tabs touch: exactly one
			keys |= b[order[i]].key;
			flash_key = b[order[i]].key;
			flash_t = UI_METRIC_9;
		}
	return keys;
}

// "A open    B back    X add code" -> buttons. Parts are split at two or more
// spaces; D-pad parts ("Left/Right change") are dropped (rows are tapped), L/R
// becomes two arrow buttons. Labels point into a static copy of hint.
int ui_bar_parse(const char *hint, ui_button_t *out, int max)
{
	static char copies[UI_METRIC_4][UI_METRIC_160];
	static int next;
	char *s = copies[next++ & UI_METRIC_3];
	snprintf(s, sizeof(copies[UI_METRIC_0]), "%s", hint);
	int n = UI_METRIC_0;
	for (char *part = s; *part && n < max; ) {
		while (*part == ' ') part++;
		if (!*part) break;
		char *end = strstr(part, "  ");
		if (end) *end = UI_METRIC_0;
		char *sp = strchr(part, ' ');
		char *label = sp ? sp + UI_METRIC_1 : "";
		if (sp) *sp = UI_METRIC_0;
		while (*label == ' ') label++;
		static const struct { const char *word; u32 key; } words[] = {
			{ "A", KEY_A }, { "B", KEY_B }, { "X", KEY_X }, { "Y", KEY_Y }, { "L", KEY_L }, { "R", KEY_R },
			{ "SELECT", KEY_SELECT }, { "START", KEY_START },
		};
		if (!strcmp(part, "L/R") && n + UI_METRIC_1 < max) {
			out[n++] = (ui_button_t){ KEY_L, NULL, UI_ICON_LEFT, false };
			out[n++] = (ui_button_t){ KEY_R, NULL, UI_ICON_RIGHT, false };
		} else
			for (size_t w = UI_METRIC_0; w < sizeof(words) / sizeof(words[UI_METRIC_0]); w++)
				if (!strcmp(part, words[w].word)) {
					if (*label >= 'a' && *label <= 'z') *label -= UI_METRIC_32;
					out[n++] = (ui_button_t){ words[w].key, *label ? label : NULL, UI_METRIC_0, false };
				}
		if (!end) break;
		part = end + UI_METRIC_2;
	}
	return n;
}

// A short notice in the middle of the top screen (sorted, favourites, rescan).
void ui_notice(const char *s, int frames)
{
	if (frames <= UI_METRIC_0 || !s || !s[UI_METRIC_0])
		return;
	int a = frames > UI_METRIC_20 ? UI_METRIC_255 : frames * UI_METRIC_255 / UI_METRIC_20;
	float w = text_width(UI_METRIC_0_48F, s) + UI_METRIC_28, x = UI_METRIC_200 - w / UI_METRIC_2;
	// under the text (ui_text_ex draws at 0.7), over covers and titles
	C2D_DrawRectSolid(x, UI_METRIC_104, UI_METRIC_0_66F, w, UI_METRIC_30, theme_alpha(ui_bar_color(UI_COL_NOTICE), a * UI_METRIC_225 / UI_METRIC_255));
	C2D_DrawRectSolid(x, UI_METRIC_104, UI_METRIC_0_665F, UI_METRIC_3, UI_METRIC_30, theme_alpha(theme()->accent[UI_METRIC_3], a));
	ui_text_ex(NULL, UI_METRIC_200, UI_METRIC_111, UI_METRIC_0_48F, theme_alpha(theme()->heading, a), C2D_AlignCenter, s);
}

// The first letter of a title for a placeholder card: a whole UTF-8 sequence,
// never half of one (out has room for 8 bytes).
void ui_first_letter(char *out, const char *title)
{
	size_t n = UI_METRIC_1;
	unsigned char c = (unsigned char)title[UI_METRIC_0];
	if (c >= UI_METRIC_0XF0) n = UI_METRIC_4;
	else if (c >= UI_METRIC_0XE0) n = UI_METRIC_3;
	else if (c >= UI_METRIC_0XC0) n = UI_METRIC_2;
	size_t i = UI_METRIC_0;
	for (; i < n && title[i]; i++)
		out[i] = title[i];
	out[i] = UI_METRIC_0;
}

// ▶▶ (dir > 0) or ◀◀ (dir < 0) in the top right corner of a game while
// fast-forward or rewind is held: the PS1 game screen's badge, for systems
// that present through the shared shell (GBA).
void ui_speed_badge(int dir)
{
	if (!dir)
		return;
	float x = UI_TOP_SCREEN_WIDTH - UI_METRIC_46 + ui_depth(-UI_METRIC_0_6F), y = UI_METRIC_10, h = UI_METRIC_16, w = UI_METRIC_13;
	u8 a = UI_METRIC_255;
	u32 c = ((g_game_palette.speed_ink & UI_METRIC_0XFFFFFF) | ((u32)a << UI_METRIC_24)), sh = g_game_palette.speed_shadow;
	C2D_DrawRectSolid(x - UI_METRIC_6, y - UI_METRIC_4, UI_METRIC_0_8F, UI_METRIC_2 * w + UI_METRIC_12, h + UI_METRIC_8, g_game_palette.speed_bg);
	for (int k = UI_METRIC_0; k < UI_METRIC_2; k++) {
		float x0 = x + k * w;
		for (int pass = UI_METRIC_0; pass < UI_METRIC_2; pass++) {
			float o = pass ? UI_METRIC_0 : UI_METRIC_1;
			u32 col = pass ? c : sh;
			if (dir > UI_METRIC_0)
				C2D_DrawTriangle(x0 + o, y + o, col, x0 + o, y + h + o, col, x0 + w + o, y + h / UI_METRIC_2 + o, col, UI_METRIC_0_81F + pass * UI_METRIC_0_01F);
			else
				C2D_DrawTriangle(x0 + w + o, y + o, col, x0 + w + o, y + h + o, col, x0 + o, y + h / UI_METRIC_2 + o, col, UI_METRIC_0_81F + pass * UI_METRIC_0_01F);
		}
	}
}

static float pause_progress = UI_METRIC_1_0F;
static u64 pause_open_tick;
static u64 pause_closed_tick;   // when the card closed; the first frame after it ends the dim's wait (logged once)
// A single atomic word publishes phase + a wrapping 24-bit millisecond clock.
// Only the menu thread owns buffered keys; PICA owns the first destination-frame clock.
u32 ui_resume_pending;
static u32 swap_keys, swap_buffer, swap_token, swap_buffer_token, swap_buffer_tick, swap_enter_tick;
static u32 motion_now(void)
{
 return (u32)(svcGetSystemTick()/(SYSCLOCK_ARM11/UI_MOTION_CLOCK_HZ)) & UI_METRIC_0XFFFFFF;
}
PSX3D_COLD void ui_pause_frame(void)
{
 if(pause_open_tick) {
  float t=(float)(svcGetSystemTick()-pause_open_tick)/SYSCLOCK_ARM11/UI_MOTION_UI_S;
  ui_pause_motion(t);
  if(t>=UI_METRIC_1)pause_open_tick = UI_METRIC_0;
 }
}
// Hardware START has no pause action. Remote requests use an unused private bit,
// inserted only after physical input is normalized.
u32 ui_swap_pause_keys(u32 keys) { return keys & ~(KEY_START | UI_SWAP_REMOTE); }
// Buffered edges belong to one handoff and expire while its destination comes up.
// The gate's sound for a press it lets through: A selects, B goes back. A screen names a different
// sound (or none, for a press that did nothing) after the gate. A game hand-over has its own
// (the launch, the pause card closing), so the keys in swap_mask get none here.
static u32 swap_sound(u32 keys)
{
 if(keys&KEY_B)sfx_default(SFX_BACK);
 else if(keys&KEY_A)sfx_default(SFX_SELECT);
 return keys;
}
u32 ui_swap_keys(u32 keys, u32 swap_mask)
{
 sfx_flush();   // the previous frame's sound: one frame after its press
 u32 state=__atomic_load_n(&ui_resume_pending,__ATOMIC_ACQUIRE);
 if(!state && !swap_keys && !swap_buffer && !(keys & swap_mask))return swap_sound(keys);
 u32 now=motion_now(),phase=state>>UI_METRIC_24;
 float t=((now-(state & UI_METRIC_0XFFFFFF)) & UI_METRIC_0XFFFFFF)/(UI_MOTION_CLOCK_HZ*UI_MOTION_DIM_S);
 if(swap_buffer && (swap_buffer_token!=swap_token ||
    ((now-swap_buffer_tick)&UI_METRIC_0XFFFFFF)>UI_MOTION_BUFFER_MS))swap_buffer=UI_METRIC_0;
 if(swap_keys || phase==UI_MOTION_PHASE_WAIT || (phase==UI_MOTION_PHASE_IN && t<UI_METRIC_1)) {
  if(keys) {swap_buffer |= keys;swap_buffer_token=swap_token;swap_buffer_tick=now;}
  if(!swap_keys || t<UI_METRIC_1)return UI_METRIC_0;
  keys=swap_keys;swap_keys=swap_buffer=UI_METRIC_0;
  // OUT edges addressed the old screen. Clear them at the handoff even when
  // the destination is a caller returning to its existing loop (no entry hook).
  __atomic_store_n(&ui_resume_pending,UI_MOTION_PHASE_WAIT<<UI_METRIC_24,__ATOMIC_RELEASE);
  return keys;
 }
 keys |= swap_buffer;swap_buffer=UI_METRIC_0;
 if(keys & swap_mask) {
  ++swap_token;
  swap_keys=keys;
  __atomic_store_n(&ui_resume_pending,(UI_MOTION_PHASE_OUT<<UI_METRIC_24)|now,__ATOMIC_RELEASE);
  return UI_METRIC_0;
 }
 return swap_sound(keys);
}
// A destructive dialog requires a new edge after it has been up UI_MOTION_CONFIRM_S
// (and any game hand-over dim is over). Never take a double-tap's second press as confirmation.
PSX3D_COLD u32 ui_swap_confirm_keys(u32 keys, u32 swap_mask)
{
 u32 phase=__atomic_load_n(&ui_resume_pending,__ATOMIC_ACQUIRE)>>UI_METRIC_24;
 if(phase==UI_MOTION_PHASE_WAIT || phase==UI_MOTION_PHASE_IN ||
    ((motion_now()-swap_enter_tick)&UI_METRIC_0XFFFFFF)<UI_MOTION_CONFIRM_MS) {
  sfx_flush();
  swap_buffer=UI_METRIC_0;
  return UI_METRIC_0;
 }
 return ui_swap_keys(keys,swap_mask);
}
void ui_swap_enter(void)
{
 sfx_flush();   // the press that opened (or closed back to) this screen
 // A destination can consume a completed OUT, never invent one. Drop edges
 // addressed to the previous screen, including taps made during its OUT.
 swap_keys=swap_buffer=UI_METRIC_0;
 swap_enter_tick=motion_now();
 u32 state=__atomic_load_n(&ui_resume_pending,__ATOMIC_ACQUIRE);
 if((state>>UI_METRIC_24)!=UI_MOTION_PHASE_WAIT)
  __atomic_store_n(&ui_resume_pending,UI_METRIC_0,__ATOMIC_RELEASE);
}
PSX3D_COLD void ui_swap_hold(void)
{
 sfx_flush();
 swap_keys=swap_buffer=UI_METRIC_0;
 __atomic_store_n(&ui_resume_pending,UI_MOTION_PHASE_HOLD<<UI_METRIC_24,__ATOMIC_RELEASE);
}
PSX3D_COLD void ui_swap_release(void)
{
 __atomic_store_n(&ui_resume_pending,UI_MOTION_PHASE_WAIT<<UI_METRIC_24,__ATOMIC_RELEASE);
}
// Called only during transitions. video.c loads the packed state once per present
// and uses this one clock sample/alpha for both eyes and the notebook.
PSX3D_COLD float ui_resume_sample(u32 state)
{
 if(!state)return UI_METRIC_0;
 u32 now=motion_now(),phase=state>>UI_METRIC_24;
 if(phase==UI_MOTION_PHASE_WAIT) {
  u32 next=(UI_MOTION_PHASE_IN<<UI_METRIC_24)|now;
  if(__atomic_compare_exchange_n(&ui_resume_pending,&state,next,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
   state=next;
   if(pause_closed_tick) {
    log_printf("swap: first compositor sample %u ms after the pause closed\n",(unsigned)((svcGetSystemTick()-pause_closed_tick)/(SYSCLOCK_ARM11/UI_MOTION_CLOCK_HZ)));
    pause_closed_tick=UI_METRIC_0;
   }
  }
  phase=state>>UI_METRIC_24;
 }
 if(phase==UI_MOTION_PHASE_HOLD)return UI_MOTION_DIM_ALPHA;
 float t=((now-(state & UI_METRIC_0XFFFFFF)) & UI_METRIC_0XFFFFFF)/(UI_MOTION_CLOCK_HZ*UI_MOTION_DIM_S);
 if(phase==UI_MOTION_PHASE_IN && t>=UI_METRIC_1) {
  __atomic_compare_exchange_n(&ui_resume_pending,&state,UI_METRIC_0,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
  return UI_METRIC_0;
 }
 float eased=ui_motion_ease(t);
 return UI_MOTION_DIM_ALPHA*(phase==UI_MOTION_PHASE_OUT ? eased : UI_METRIC_1-eased);
}
void ui_pause_motion(float progress) { pause_progress = ui_motion_ease(progress); }
void ui_motion_shade(float alpha, int width)
{
 if (alpha > UI_METRIC_0)
  C2D_DrawRectSolid(UI_METRIC_0, UI_METRIC_0, UI_METRIC_1_0F, width, UI_SCREEN_HEIGHT,
   theme_alpha(UI_METRIC_0, (int)(UI_METRIC_255 * fminf(alpha, UI_METRIC_1_0F))));
}
PSX3D_COLD bool ui_pause_open(void)
{
 if(!aptMainLoop() || !gspHasGpuRight())return false;
 sfx_cancel();(void)sfx_close_said();sfx_play(SFX_PAUSE_OPEN);   // whatever the game left (PS1's resume line) never plays; the card's first frame plays this
 swap_keys=swap_buffer=UI_METRIC_0;
 __atomic_store_n(&ui_resume_pending,UI_METRIC_0,__ATOMIC_RELEASE);
 pause_open_tick=svcGetSystemTick();
 ui_pause_motion(UI_METRIC_0);
 return true;
}
PSX3D_COLD bool ui_pause_close(void)
{
 if(!aptMainLoop() || !gspHasGpuRight())return false;
 // The card goes away (resume or quit): this sound, now. PS1 run_game's own sfx_play(SFX_BACK) after the resume
 // (pinned, unchanged) only queues; nothing flushes in a game and the next ui_pause_open cancels it.
 sfx_cancel();if(!sfx_close_said())sfx_now(SFX_PAUSE_CLOSE);
 pause_closed_tick=svcGetSystemTick();
 pause_open_tick=UI_METRIC_0;
 ui_swap_enter();
 return true;
}
// ---------------------------------------------------------------- the pause card
// The paused game in a wooden CRT on the wall with its hang tag (top screen), the menu on a
// ruled notebook card (bottom screen). One palette from the theme's roles (the room's paper,
// wood and ink; other themes their own), every number in ui_style.h. Cold: menus only.
typedef struct { u32 paper, edge, ink, soft, rule, lines, stamp, blue, green, wood, dark, rim, wall, light; } paper_t;

PSX3D_COLD static void paper_get(paper_t *p)
{
	const theme_t *th = theme();
	const theme_room_t *r = theme_room(th);
	if (r) {
		*p = (paper_t){ r->paper, r->paper_edge, r->ink, r->ink_soft, r->rule, r->rule_soft, r->stamp, r->pin[UI_METRIC_2], r->leaf[UI_METRIC_0],
			r->wood, r->back, r->upright, r->wall, r->light };
		return;
	}
	*p = (paper_t){ th->card_top, th->card_bottom, th->text_bright, th->text_muted, th->accent[UI_METRIC_0], th->section_line, th->danger,
		th->accent[UI_METRIC_2], th->accent[UI_METRIC_1], th->accent[UI_METRIC_0], th->card_bottom, th->card_top, th->surface, th->light };
}

PSX3D_COLD static void rrect(float x, float y, float w, float h, float r, u32 c, float z)
{
	C2D_DrawRectSolid(x + r, y, z, w - r * UI_METRIC_2, h, c);
	C2D_DrawRectSolid(x, y + r, z, r, h - r * UI_METRIC_2, c);
	C2D_DrawRectSolid(x + w - r, y + r, z, r, h - r * UI_METRIC_2, c);
	for (int i = UI_METRIC_0; i < UI_METRIC_4; i++)
		C2D_DrawCircleSolid(x + (i & UI_METRIC_1 ? w - r : r), y + (i & UI_METRIC_2 ? h - r : r), z, r, c);
}

// the outline of a rounded rectangle: a rrect and a smaller one in the paper's color on top
PSX3D_COLD static void rring(float x, float y, float w, float h, float r, float line, u32 c, u32 inside, float z)
{
	rrect(x, y, w, h, r, c, z);
	rrect(x + line, y + line, w - line * UI_METRIC_2, h - line * UI_METRIC_2, r - line, inside, z + UI_METRIC_0_001F);
}

// words wrapped to width; the last line takes whatever is left (the caller fits it)
PSX3D_COLD static int wrap_words(const char *s, float size, float width, char (*out)[UI_METRIC_48], int max)
{
	int n = UI_METRIC_0;
	char cur[UI_METRIC_96];
	cur[UI_METRIC_0] = UI_METRIC_0;
	const char *rest = s;
	while (*rest == ' ')
		rest++;
	while (*rest && n < max) {
		if (n == max - UI_METRIC_1) {
			snprintf(out[n++], UI_METRIC_48, "%s%s%s", cur, cur[UI_METRIC_0] ? " " : "", rest);
			return n;
		}
		char word[UI_METRIC_48], next[UI_METRIC_96];
		int k = UI_METRIC_0;
		while (*rest && *rest != ' ' && k < (int)sizeof(word) - UI_METRIC_1)
			word[k++] = *rest++;
		word[k] = UI_METRIC_0;
		while (*rest == ' ')
			rest++;
		snprintf(next, sizeof(next), "%s%s%s", cur, cur[UI_METRIC_0] ? " " : "", word);
		if (cur[UI_METRIC_0] && text_width(size, next) > width) {
			snprintf(out[n++], UI_METRIC_48, "%s", cur);
			snprintf(cur, sizeof(cur), "%s", word);
		} else
			snprintf(cur, sizeof(cur), "%s", next);
	}
	if (cur[UI_METRIC_0] && n < max)
		snprintf(out[n++], UI_METRIC_48, "%s", cur);
	return n;
}

PSX3D_COLD static void tag_shape(float x, float y, float w, float h, float cut, u32 c, float z)
{
	C2D_DrawRectSolid(x, y + cut, z, w, h - cut, c);
	C2D_DrawRectSolid(x + cut, y, z, w - cut * UI_METRIC_2, cut, c);
	C2D_DrawTriangle(x, y + cut, c, x + cut, y, c, x + cut, y + cut, c, z);
	C2D_DrawTriangle(x + w - cut, y, c, x + w, y + cut, c, x + w - cut, y + cut, c, z);
}

// A paper hang tag: its heading, a rule, a few small lines. Top left is (x, y); it hangs a little
// crooked from its string and comes out of the screen a little in 3D.
PSX3D_COLD bool ui_tag_body_fits(const char *body, float w)
{
	char lines[UI_TAG_BODY_LINES + UI_METRIC_1][UI_METRIC_48];
	float tw = (w > UI_METRIC_0 ? w : UI_TAG_W) - UI_TAG_PAD * UI_METRIC_2;
	int n = wrap_words(body, UI_TAG_LINE_SIZE, tw, lines, UI_TAG_BODY_LINES + UI_METRIC_1);
	if (n > UI_TAG_BODY_LINES)
		return false;
	for (int i = UI_METRIC_0; i < n; i++)   // one word wider than the tag
		if (text_width(UI_TAG_LINE_SIZE, lines[i]) > tw)
			return false;
	return true;
}
// one small line: shrunk to UI_FONT_CONTEXT before anything is cut
PSX3D_COLD static void tag_line(float x, float y, float tw, u32 c, const char *s)
{
	float size = UI_TAG_LINE_SIZE, lw = text_width(size, s);
	if (lw > tw)
		size = fmaxf(UI_FONT_CONTEXT, size * tw / lw);
	ui_text_ex(NULL, x, y, size, c, UI_METRIC_0, ui_fit(s, size, tw));
}
PSX3D_COLD void ui_tag(const ui_tag_t *t, float x, float y)
{
	paper_t p;
	paper_get(&p);
	const float w = t->w > UI_METRIC_0 ? t->w : UI_TAG_W, tw = w - UI_TAG_PAD * UI_METRIC_2;
	char lines[UI_TAG_TITLE_LINES][UI_METRIC_48], body[UI_TAG_BODY_LINES][UI_METRIC_48];
	int nt = wrap_words(t->title, UI_TAG_TITLE_SIZE, tw, lines, UI_TAG_TITLE_LINES), ns = UI_METRIC_0;
	while (ns < UI_TAG_LINES && t->line[ns])
		ns++;
	// the body's wrapping is kept while the sentence and the width stay the same (it is drawn every frame and eye)
	static char body_src[UI_METRIC_256];
	static float body_w;
	static int body_n;
	static char body_lines[UI_TAG_BODY_LINES][UI_METRIC_48];
	int nb = UI_METRIC_0;
	if (t->body && t->body[UI_METRIC_0]) {
		if (body_w != tw || strcmp(body_src, t->body)) {
			ui_fit_copy(body_src, sizeof(body_src), t->body, sizeof(body_src));
			body_w = tw;
			body_n = wrap_words(t->body, UI_TAG_LINE_SIZE, tw, body_lines, UI_TAG_BODY_LINES);
		}
		nb = body_n;
		memcpy(body, body_lines, sizeof(body));
	}
	float h = UI_TAG_TOP + nt * UI_TAG_TITLE_LINE + (ns + nb ? UI_TAG_GAP * UI_METRIC_2 + UI_METRIC_1 + (ns + nb) * UI_TAG_LINE : UI_METRIC_0) + UI_TAG_PAD;
	float hx = x + w / UI_METRIC_2, hy = y + UI_TAG_HOLE_Y;
	C3D_Mtx saved;
	C2D_ViewSave(&saved);
	C2D_ViewTranslate(hx + ui_depth(UI_TAG_DEPTH), hy);
	C2D_ViewRotate(UI_TAG_TILT);
	C2D_ViewTranslate(-hx, -hy);
	C2D_DrawLine(hx, hy, p.soft, hx + UI_TAG_STRING_DX, -UI_METRIC_20, p.soft, UI_TAG_STRING_W, UI_TAG_Z - UI_METRIC_0_02F);
	tag_shape(x + UI_TAG_SHADOW, y + UI_TAG_SHADOW, w, h, UI_TAG_CUT, theme_alpha(theme()->shadow, UI_TAG_SHADOW_ALPHA), UI_TAG_Z - UI_METRIC_0_02F);
	tag_shape(x - UI_METRIC_1, y - UI_METRIC_1, w + UI_METRIC_2, h + UI_METRIC_2, UI_TAG_CUT + UI_METRIC_1, p.edge, UI_TAG_Z - UI_METRIC_0_01F);
	tag_shape(x, y, w, h, UI_TAG_CUT, p.paper, UI_TAG_Z);
	C2D_DrawCircleSolid(hx, hy, UI_TAG_Z + UI_METRIC_0_001F, UI_TAG_HOLE_R + UI_METRIC_1, p.soft);
	C2D_DrawCircleSolid(hx, hy, UI_TAG_Z + UI_METRIC_0_002F, UI_TAG_HOLE_R, p.dark);
	float ty = y + UI_TAG_TOP;
	for (int i = UI_METRIC_0; i < nt; i++, ty += UI_TAG_TITLE_LINE)
		ui_text_ex(NULL, x + UI_TAG_PAD, ty, UI_TAG_TITLE_SIZE, p.ink, UI_METRIC_0, ui_fit(lines[i], UI_TAG_TITLE_SIZE, tw));
	if (ns + nb) {
		ty += UI_TAG_GAP - UI_METRIC_2;
		C2D_DrawRectSolid(x + UI_TAG_PAD, ty, UI_TAG_Z + UI_METRIC_0_001F, tw, UI_METRIC_1, theme_alpha(p.soft, UI_METRIC_120));
		ty += UI_TAG_GAP;
		for (int i = UI_METRIC_0; i < ns; i++, ty += UI_TAG_LINE)
			tag_line(x + UI_TAG_PAD, ty, tw, i == t->hot ? p.stamp : p.soft, t->line[i]);
		for (int i = UI_METRIC_0; i < nb; i++, ty += UI_TAG_LINE)
			tag_line(x + UI_TAG_PAD, ty, tw, p.soft, body[i]);
	}
	C2D_ViewRestore(&saved);
}

// Wall, CRT, and the hang tag. The glass is the largest box of the game's aspect that fits; game() draws a
// full-screen frame and is moved and scaled so the game's own box (x, y, w, h) fills the glass.
/* The SAVED postmark: stamped on the paused picture (the moment the postcard holds), in the glass's corner
 * opposite the pause mark, never over the card's words (deep UI test F13). k 0..1 fades it in. */
static void caps(char *out, size_t n, const char *s);
static float pause_stamp_k;
static char pause_stamp_when[UI_METRIC_24];
PSX3D_COLD void ui_pause_stamp(float k, const char *when)
{
	pause_stamp_k = k;
	snprintf(pause_stamp_when, sizeof(pause_stamp_when), "%s", when ? when : "");
}
static void pause_stamp_draw(const paper_t *p, float x, float y)
{
	char up[UI_METRIC_32];
	C3D_Mtx saved;
	C2D_ViewSave(&saved);
	C2D_ViewTranslate(x, y);
	C2D_ViewRotate(UI_CARD_STAMP_TILT);
	u32 c = theme_alpha(p->stamp, (int)(UI_METRIC_255 * pause_progress * fminf(pause_stamp_k, UI_METRIC_1_0F)));
	C2D_DrawCircleSolid(UI_METRIC_0, UI_METRIC_0, UI_CRT_Z_MARK, UI_CARD_STAMP_R, theme_alpha(p->paper, (int)(UI_PAUSE_STAMP_PAPER * pause_progress * fminf(pause_stamp_k, UI_METRIC_1_0F))));
	for (int k = UI_METRIC_0; k < UI_METRIC_2; k++)
		for (int i = UI_METRIC_0; i < UI_CARD_STAMP_SEGS; i++) {
			float rr = UI_CARD_STAMP_R - k * UI_METRIC_3, a0 = i * UI_PC_TAU / UI_CARD_STAMP_SEGS, a1 = (i + UI_METRIC_1) * UI_PC_TAU / UI_CARD_STAMP_SEGS;
			C2D_DrawLine(cosf(a0) * rr, sinf(a0) * rr, c, cosf(a1) * rr, sinf(a1) * rr, c, UI_METRIC_1_5F, UI_CRT_Z_MARK + UI_METRIC_0_01F);
		}
	ui_text_ex(NULL, UI_METRIC_0, -UI_METRIC_9, UI_FONT_VALUE, c, C2D_AlignCenter, "SAVED");
	if (pause_stamp_when[UI_METRIC_0]) {
		caps(up, sizeof(up), pause_stamp_when);
		ui_text_ex(NULL, UI_METRIC_0, UI_METRIC_2, UI_FONT_CONTEXT, c, C2D_AlignCenter, up);
	}
	C2D_ViewRestore(&saved);
}

PSX3D_COLD void ui_pause_scene(const ui_tag_t *tag, const float *box, void (*game)(void *), void *ud)
{
	paper_t p;
	paper_get(&p);
	const theme_t *th = theme();
	C2D_DrawRectSolid(UI_METRIC_0, UI_METRIC_0, UI_CRT_Z_WALL, UI_TOP_SCREEN_WIDTH, UI_SCREEN_HEIGHT, p.wall);
	for (float px = UI_CRT_PLANK; px < UI_TOP_SCREEN_WIDTH; px += UI_CRT_PLANK)
		C2D_DrawRectSolid(px, UI_METRIC_0, UI_CRT_Z_WALL + UI_METRIC_0_001F, UI_METRIC_1, UI_SCREEN_HEIGHT, theme_alpha(th->shadow, UI_CRT_PLANK_ALPHA));
	float bx = box ? box[UI_METRIC_0] : UI_METRIC_0, by = box ? box[UI_METRIC_1] : UI_METRIC_0;
	float bw = box ? box[UI_METRIC_2] : UI_TOP_SCREEN_WIDTH, bh = box ? box[UI_METRIC_3] : UI_SCREEN_HEIGHT;
	float k = pause_progress, fit = fminf(UI_CRT_GLASS_W / bw, UI_CRT_GLASS_H / bh);
	float s = UI_METRIC_1_0F + (fit - UI_METRIC_1_0F) * k, gw = bw * s, gh = bh * s;
	float gx = bx + (UI_CRT_CX - bw * fit / UI_METRIC_2 - bx) * k;
	float gy = by + (UI_CRT_CY - bh * fit / UI_METRIC_2 - by) * k, o = UI_CRT_PAD * k;
	rrect(gx - o + UI_CRT_SHADOW_X, gy - o + UI_CRT_SHADOW_Y, gw + o * UI_METRIC_2, gh + o * UI_METRIC_2, UI_CRT_RADIUS,
		mix(p.wall, th->shadow, UI_METRIC_0_5F, UI_METRIC_255), UI_CRT_Z_BEZEL - UI_METRIC_0_01F);
	rrect(gx - o, gy - o, gw + o * UI_METRIC_2, gh + o * UI_METRIC_2, UI_CRT_RADIUS, p.dark, UI_CRT_Z_BEZEL);
	C2D_DrawRectSolid(gx - o + UI_CRT_RADIUS, gy - o + UI_METRIC_1, UI_CRT_Z_BEZEL + UI_METRIC_0_01F, gw + o * UI_METRIC_2 - UI_CRT_RADIUS * UI_METRIC_2,
		UI_METRIC_1, theme_alpha(p.light, UI_CRT_GLOW_ALPHA));
	C2D_DrawRectSolid(gx - UI_CRT_LIP, gy - UI_CRT_LIP, UI_CRT_Z_BEZEL + UI_METRIC_0_02F, gw + UI_CRT_LIP * UI_METRIC_2, gh + UI_CRT_LIP * UI_METRIC_2, th->shadow);
	C2D_DrawRectSolid(gx, gy, UI_CRT_Z_GLASS, gw, gh, th->shadow);
	if (game) {
		C3D_Mtx saved;
		C2D_ViewSave(&saved);
		C2D_ViewTranslate(gx, gy);
		C2D_ViewScale(s, s);
		C2D_ViewTranslate(-bx, -by);
		game(ud);
		C2D_ViewRestore(&saved);
	}
	for (int i = UI_METRIC_0; i < UI_METRIC_2; i++)   // the pause mark, with its shadow
		for (int pass = UI_METRIC_0; pass < UI_METRIC_2; pass++)
			C2D_DrawRectSolid(gx + UI_CRT_PAUSE_X + i * UI_CRT_PAUSE_GAP + (pass ? UI_METRIC_0 : UI_METRIC_1), gy + gh - UI_CRT_PAUSE_Y + (pass ? UI_METRIC_0 : UI_METRIC_1),
				UI_CRT_Z_MARK + pass * UI_METRIC_0_01F, UI_CRT_PAUSE_W, UI_CRT_PAUSE_H, pass ? p.paper : th->shadow);
	if (pause_stamp_k > UI_METRIC_0_0F)
		pause_stamp_draw(&p, gx + gw - UI_CARD_STAMP_R - UI_CRT_PAUSE_X, gy + gh - UI_CARD_STAMP_R - UI_CRT_PAUSE_X);
	if (tag)
		ui_tag(tag, UI_TAG_X + (UI_TOP_SCREEN_WIDTH - UI_TAG_X) * (UI_METRIC_1_0F - pause_progress), UI_TAG_Y);
}

PSX3D_COLD void ui_card_update(flist_t *f, int sel, int n)
{
	float dt = flist_dt(f);
	float lift_step = UI_METRIC_1 - expf(-UI_MOTION_SETTLE_RATE * dt);
	if (!f->init) {
		f->hl = sel;
		f->init = true;
	}
	int top = (int)(f->scroll_target + UI_METRIC_0_5F);
	if (sel < top)
		top = sel;
	if (sel > top + UI_CARD_ROWS - UI_METRIC_1)
		top = sel - UI_CARD_ROWS + UI_METRIC_1;
	if (top > n - UI_CARD_ROWS)
		top = n - UI_CARD_ROWS;
	if (top < UI_METRIC_0)
		top = UI_METRIC_0;
	f->scroll_target = top;
	ui_motion_settle(&f->hl, &f->hl_v, sel, UI_MOTION_SETTLE_RATE, dt);
	ui_motion_settle(&f->scroll, &f->scroll_v, top, UI_MOTION_SETTLE_RATE, dt);
	for (int i = UI_METRIC_0; i < FLIST_MAX && i < n; i++)
		f->lift[i] += ((i == sel ? UI_METRIC_1_0F : UI_METRIC_0_0F) - f->lift[i]) * lift_step;
}

PSX3D_COLD u32 ui_card_input(const flist_t *f, int n, int *sel, u32 keys)
{
	u32 touch = keys & KEY_TOUCH;
	keys = ui_list_input(f, n, sel, keys & ~KEY_TOUCH, false);
	if (touch && n > UI_METRIC_0) {
		touchPosition t;
		hidTouchRead(&t);
		int row = (int)floorf(f->scroll + (t.py - UI_CARD_ROWS_Y) / (float)UI_CARD_ROW);
		if (t.px >= UI_CARD_X && t.px < UI_CARD_X + UI_CARD_W && t.py >= UI_CARD_ROWS_Y && t.py < UI_CARD_ROWS_Y + UI_CARD_ROWS * UI_CARD_ROW
		    && row >= UI_METRIC_0 && row < n) {
			if (row == *sel)
				keys |= KEY_A;
			else
				sfx_now(SFX_MOVE);
			*sel = row;
		} else
			keys |= touch;
	}
	return keys;
}

PSX3D_COLD static void caps(char *out, size_t n, const char *s)
{
	size_t i = UI_METRIC_0;
	for (; s && s[i] && i + UI_METRIC_1 < n; i++)
		out[i] = s[i] >= 'a' && s[i] <= 'z' ? s[i] - 'a' + 'A' : s[i];
	out[i] = UI_METRIC_0;
}

PSX3D_COLD static u32 tone_color(const paper_t *p, int tone)
{
	return tone == UI_TONE_RED ? p->stamp : tone == UI_TONE_BLUE ? p->blue : tone == UI_TONE_GREEN ? p->green : p->soft;
}

// The header row (the title on the red rule) under the touch: tapping the title goes back (docs/settings.md).
PSX3D_COLD bool ui_card_title_hit(void)
{
	touchPosition t;
	hidTouchRead(&t);
	return t.px >= UI_CARD_X && t.px < UI_CARD_X + UI_CARD_W && t.py >= UI_CARD_Y && t.py < UI_CARD_ROWS_Y;
}

// The notebook card: a header (the title on the red rule), ruled rows with a margin icon, value on the right.
// One card row's text fitted to the card: the value right-aligned (at most UI_CARD_VALUE_MAX of the row beside
// a long label, arrows around it when left / right change it), the label in the room left of it. Shared by the
// card and the cut test, so the test sees what the card draws.
// The label is fitted to its room at the full nudge whatever the row's lift, so a label never changes size while
// it slides, and the fitted texts are copied out of ui_fit_ex's 32-entry cache: a row that refitted its label every
// frame of its ~1 s slide (1 - e^-18t only reaches 1.0f after 0.92 s) cycled the cache, and the label's insert could
// overwrite the slot the value had just been read from, so the value showed another text for a frame (the GBA
// settings flicker, 2026-11-12, tests/ui_card/flicker.c).
typedef struct { char label[UI_METRIC_256], value[UI_METRIC_256]; float lx, lsize, vx, vsize, vw; } card_fit_t;
PSX3D_COLD static void card_row_fit(const ui_card_row_t *r, float lift, card_fit_t *o)
{
	const float right = UI_CARD_X + UI_CARD_W - UI_CARD_RIGHT;
	float vx = right;
	o->value[UI_METRIC_0] = UI_METRIC_0;
	o->vsize = o->vw = UI_METRIC_0;
	if (r->value && r->value[UI_METRIC_0]) {
		float lw0 = r->label ? fminf(ui_text_width(NULL, UI_CARD_ROW_SIZE, r->label), (right - UI_CARD_TEXT_X) * UI_CARD_VALUE_MAX) : UI_METRIC_0;
		float vroom = (right - UI_CARD_TEXT_X) - lw0 - UI_METRIC_8 - (r->arrows ? UI_CARD_ARROW * UI_METRIC_3 + UI_METRIC_4 + UI_CARD_ARROW * UI_METRIC_2 : UI_METRIC_0);
		ui_fit_copy(o->value, sizeof(o->value), ui_fit_ex(NULL, r->value, UI_CARD_VALUE_SIZE, UI_TEXT_FLOOR, vroom, &o->vsize), sizeof(o->value));
		o->vw = ui_text_width(NULL, o->vsize, o->value);
		if (r->arrows) vx = right - UI_CARD_ARROW * UI_METRIC_3;
		o->vx = vx;
		vx -= o->vw + UI_METRIC_8;
	} else o->vx = vx;
	o->lx = UI_CARD_TEXT_X + UI_CARD_NUDGE * lift;
	ui_fit_copy(o->label, sizeof(o->label), ui_fit_ex(NULL, r->label, UI_CARD_ROW_SIZE, UI_TEXT_FLOOR, vx - UI_CARD_TEXT_X - UI_CARD_NUDGE, &o->lsize), sizeof(o->label));
}
// True when the card cuts this row (label or value ends in "..." at the floor) as the focused row draws it.
PSX3D_COLD bool ui_card_row_cut(const ui_card_row_t *r)
{
	card_fit_t o;
	card_row_fit(r, UI_METRIC_1_0F, &o);
	return (r->label && strcmp(o.label, r->label)) || (o.value[UI_METRIC_0] && strcmp(o.value, r->value));
}
#include "components/value_card.inc"
static bool notice_calm;   // ui_card_notice_calm: the next card's notice is good news
PSX3D_COLD void ui_card_notice_calm(void) { notice_calm = true; }
PSX3D_COLD void ui_card_header(ui_text_fn text, float x, float right, float rule, const char *title, const char *eyebrow, u32 ink, u32 red)
{
	char up[UI_METRIC_48];
	float ew = UI_METRIC_0, size;
	bool title_wide = title && ui_text_width(libui_font(), UI_CARD_TITLE_MIN, title) > (right - x) / UI_METRIC_2;
	if (eyebrow && eyebrow[UI_METRIC_0] && !title_wide) {   // a long title keeps the whole row
		caps(up, sizeof(up), eyebrow);
		const char *e = ui_fit_ex(NULL, up, UI_TEXT_FLOOR, UI_TEXT_FLOOR, (right - x) / UI_METRIC_2, &size);
		ew = text_width(size, e) + UI_METRIC_8;
		text(NULL, right, UI_PAPER_TOP(rule, size), size, red, C2D_AlignRight, e);   // on the red rule
	}
	if (title) {
		const char *t = ui_fit_ex(libui_font(), title, UI_CARD_TITLE_SIZE, UI_CARD_TITLE_MIN, right - x - ew, &size);
		text(libui_font(), x, UI_PAPER_TITLE_TOP(rule, size), size, ink, UI_METRIC_0, t);   // on the red rule
	}
}
PSX3D_COLD void ui_card_draw(const flist_t *f, int n, int sel, void (*row)(int, void *, ui_card_row_t *), void *ud, const ui_card_t *card)
{
	bool calm = notice_calm;
	notice_calm = false;
	paper_t p;
	paper_get(&p);
	const theme_t *th = theme();
	const float cx = UI_CARD_X, cy = UI_CARD_Y, cw = UI_CARD_W, ch = UI_CARD_H, right = cx + cw - UI_CARD_RIGHT;
	C2D_DrawRectSolid(UI_METRIC_0, UI_METRIC_0, UI_CARD_Z_SHADOW - UI_METRIC_0_01F, UI_BOTTOM_SCREEN_WIDTH, UI_SCREEN_HEIGHT, th->surface);
	C3D_Mtx motion_saved;
	C2D_ViewSave(&motion_saved);
	C2D_ViewTranslate(UI_METRIC_0, (UI_METRIC_1_0F - pause_progress) * UI_MOTION_CARD_TRAVEL);
	p.paper = theme_alpha(p.paper, (int)(UI_METRIC_255 * pause_progress));
	p.edge = theme_alpha(p.edge, (int)(UI_METRIC_255 * pause_progress));
	p.ink = theme_alpha(p.ink, (int)(UI_METRIC_255 * pause_progress));
	p.soft = theme_alpha(p.soft, (int)(UI_METRIC_255 * pause_progress));
	p.rule = theme_alpha(p.rule, (int)(UI_METRIC_255 * pause_progress));
	p.lines = theme_alpha(p.lines, (int)(UI_METRIC_255 * pause_progress));
	p.stamp = theme_alpha(p.stamp, (int)(UI_METRIC_255 * pause_progress));
	p.wood = theme_alpha(p.wood, (int)(UI_METRIC_255 * pause_progress));
	C2D_DrawRectSolid(cx + UI_CARD_SHADOW, cy + UI_CARD_SHADOW, UI_CARD_Z_SHADOW, cw, ch, theme_alpha(th->shadow, (int)(UI_CRT_SHADOW_ALPHA * pause_progress)));
	C2D_DrawRectangle(cx, cy, UI_CARD_Z_PAPER, cw, ch, p.paper, p.paper, p.edge, p.paper);
	// ruled lines every UI_PAPER_LINE from the red rule to the card's foot: the rows' and the notice's text sits on them
	for (int i = UI_METRIC_0; UI_CARD_RULE_Y + i * UI_CARD_ROW < cy + ch; i++)
		C2D_DrawRectSolid(cx, UI_CARD_RULE_Y + i * UI_CARD_ROW, UI_CARD_Z_RULE, cw, UI_METRIC_1, p.lines);
	C2D_DrawRectSolid(cx, cy + UI_CARD_RULE_Y - UI_CARD_Y, UI_CARD_Z_RULE, cw, UI_METRIC_1, p.rule);
	C2D_DrawRectSolid(UI_CARD_MARGIN_X, cy + UI_CARD_RULE_Y - UI_CARD_Y, UI_CARD_Z_RULE, UI_METRIC_1, ch - UI_CARD_RULE_Y + UI_CARD_Y, p.rule);
	// The header row: the title with the right-hand eyebrow. The left eyebrow ("SETTINGS", "PAUSED") is not drawn:
	// the title and the top screen say it (no text without a purpose; room for 10 px text).
	ui_card_header(ui_text_ex, UI_CARD_TEXT_X, right, UI_CARD_RULE_Y, card->title, card->eyebrow_right, p.ink, p.stamp);
	// The selection: the highlighter's stroke behind the selected row's text (ui_mark.h), drawn out as it arrives.
	float sy = UI_CARD_ROWS_Y + (sel - f->scroll) * UI_CARD_ROW;
	if (sel >= UI_METRIC_0 && sel < n && sy > UI_CARD_ROWS_Y - UI_CARD_ROW / UI_METRIC_2 && sy < UI_CARD_ROWS_Y + (UI_CARD_ROWS - UI_METRIC_0_5F) * UI_CARD_ROW) {
		ui_card_row_t r = { UI_METRIC_0 };
		row(sel, ud, &r);
		if (!r.heading) {
			float lift = sel < FLIST_MAX ? f->lift[sel] : UI_METRIC_1_0F;
			card_fit_t fit;
			card_row_fit(&r, lift, &fit);
			float x1 = fit.value[UI_METRIC_0] ? (r.arrows ? right : fit.vx) : fit.lx + ui_text_width(NULL, fit.lsize, fit.label);
			ui_mark_row(fit.lx, x1, sy + UI_CARD_ROW - UI_METRIC_1, sel, ui_mark_progress_from_lift(lift), pause_progress, UI_CARD_Z_SELECT);
		}
	}
	int first = (int)floorf(f->scroll);
	for (int i = first < UI_METRIC_0 ? UI_METRIC_0 : first; i < n && i <= first + UI_CARD_ROWS; i++) {
		float ry = UI_CARD_ROWS_Y + (i - f->scroll) * UI_CARD_ROW, edge = UI_METRIC_1_0F;
		const float line = ry + UI_CARD_ROW - UI_METRIC_1, mark = UI_PAPER_MARK_Y(line);   // the row's text sits on this ruled line
		if (ry < UI_CARD_ROWS_Y)
			edge = UI_METRIC_1_0F - (UI_CARD_ROWS_Y - ry) / UI_CARD_ROW;
		if (ry + UI_CARD_ROW > UI_CARD_ROWS_Y + UI_CARD_ROWS * UI_CARD_ROW)
			edge = UI_METRIC_1_0F - (ry + UI_CARD_ROW - (UI_CARD_ROWS_Y + UI_CARD_ROWS * UI_CARD_ROW)) / UI_CARD_ROW;
		if (edge <= UI_METRIC_0_02F)
			continue;
		ui_card_row_t r = { UI_METRIC_0 };
		row(i, ud, &r);
		float lift = i < FLIST_MAX ? f->lift[i] : (i == sel);
		int a = (int)(UI_METRIC_255 * pause_progress * edge * (r.dim && !r.heading ? UI_CARD_DIM_ALPHA / (float)UI_METRIC_255 : UI_METRIC_1_0F));
		u32 ink = theme_alpha(r.heading ? p.rule : p.ink, a), soft = theme_alpha(r.icon && lift > UI_METRIC_0_5F ? p.wood : p.soft, a);   // headings in the margin's red
		if(r.source!=BB_SOURCE_NONE)ui_source_mark(r.source,UI_CARD_ICON_X,mark,soft,p.paper);
  else bar_icon(r.icon, UI_CARD_ICON_X, mark, UI_CARD_ICON_SIZE, soft, p.paper, false);
		card_fit_t fit;
		card_row_fit(&r, lift, &fit);
		if (fit.value[UI_METRIC_0]) {
			u32 vc = theme_alpha(tone_color(&p, r.tone), a);
			float vx = fit.vx;
			if (r.arrows) {
				C2D_DrawTriangle(right, mark, vc, right - UI_CARD_ARROW * UI_METRIC_2, mark - UI_CARD_ARROW, vc,
					right - UI_CARD_ARROW * UI_METRIC_2, mark + UI_CARD_ARROW, vc, UI_METRIC_0_65F);
				C2D_DrawTriangle(vx - fit.vw - UI_METRIC_4 - UI_CARD_ARROW * UI_METRIC_2, mark, vc, vx - fit.vw - UI_METRIC_4, mark - UI_CARD_ARROW, vc,
					vx - fit.vw - UI_METRIC_4, mark + UI_CARD_ARROW, vc, UI_METRIC_0_65F);
			}
			ui_text_ex(NULL, vx, UI_PAPER_TOP(line, fit.vsize), fit.vsize, vc, C2D_AlignRight, fit.value);
		}
		ui_text_ex(NULL, fit.lx, UI_PAPER_TOP(line, fit.lsize), fit.lsize, ink, UI_METRIC_0, fit.label);
	}
	if (n > UI_CARD_ROWS)
		C2D_DrawRectSolid(cx + cw - UI_CARD_SCROLLBAR_W - UI_METRIC_1, UI_CARD_ROWS_Y + UI_CARD_ROWS * UI_CARD_ROW * f->scroll / n, UI_CARD_Z_SELECT + UI_METRIC_0_01F,
			UI_CARD_SCROLLBAR_W, UI_CARD_ROWS * UI_CARD_ROW * UI_CARD_ROWS / (float)n, theme_alpha(th->scrollbar, (int)(UI_METRIC_255 * pause_progress)));
	if (card->notice && card->notice[UI_METRIC_0]) {
		float ny = cy + ch - UI_CARD_NOTICE_H;
		C2D_DrawRectSolid(cx + UI_METRIC_1, ny, UI_CARD_Z_SELECT + UI_METRIC_0_02F, cw - UI_METRIC_2, UI_CARD_NOTICE_H - UI_METRIC_1, p.edge);
		float nsize;
		const char *notice = ui_fit_ex(NULL, card->notice, UI_CARD_NOTICE_SIZE, UI_TEXT_FLOOR, right - UI_CARD_TEXT_X, &nsize);
		ui_text_ex(NULL, UI_CARD_TEXT_X, UI_PAPER_TOP(ny + UI_CARD_NOTICE_H - UI_METRIC_1, nsize), nsize, calm ? p.ink : p.stamp, UI_METRIC_0, notice);   // on the last line
	}
	// the paper clip: two loops
	rring(UI_CARD_CLIP_X, UI_CARD_CLIP_Y, UI_CARD_CLIP_W, UI_CARD_CLIP_H, UI_CARD_CLIP_W / UI_METRIC_2, UI_CARD_CLIP_LINE, p.soft, p.paper, UI_CARD_Z_CLIP);
	rring(UI_CARD_CLIP_X + UI_METRIC_3, UI_CARD_CLIP_Y + UI_METRIC_8, UI_CARD_CLIP_W - UI_METRIC_6, UI_CARD_CLIP_H - UI_METRIC_8, (UI_CARD_CLIP_W - UI_METRIC_6) / UI_METRIC_2,
		UI_CARD_CLIP_LINE, p.soft, p.paper, UI_CARD_Z_CLIP + UI_METRIC_0_01F);
	C2D_ViewRestore(&motion_saved);
}

/* Three monochrome marks, shared by every settings card. */
/* No mark for a System value: it was on nearly every row and said nothing (settings overhaul judges, 2026-11-12). */
PSX3D_COLD void ui_source_mark(bb_setting_source source,float x,float y,u32 ink,u32 paper)
{
 if(source==BB_SOURCE_CHANGED) {
  C2D_DrawLine(x-UI_SOURCE_RADIUS,y+UI_SOURCE_RADIUS,ink,x+UI_SOURCE_RADIUS,y-UI_SOURCE_RADIUS,ink,UI_SOURCE_STROKE,UI_SOURCE_Z);
  C2D_DrawLine(x-UI_SOURCE_RADIUS,y+UI_SOURCE_RADIUS,ink,x+UI_SOURCE_RADIUS,y+UI_SOURCE_RADIUS,ink,UI_METRIC_1,UI_SOURCE_Z);
 } else if(source==BB_SOURCE_RECOMMENDED || source==BB_SOURCE_ALL) {
  C2D_DrawCircleSolid(x,y,UI_SOURCE_Z,UI_SOURCE_RADIUS,ink);
  C2D_DrawCircleSolid(x,y,UI_SOURCE_Z+UI_METRIC_0_001F,UI_SOURCE_RADIUS-UI_METRIC_1,paper);
  if(source==BB_SOURCE_ALL)C2D_DrawLine(x-UI_SOURCE_RADIUS,y,ink,x+UI_SOURCE_RADIUS,y,ink,UI_METRIC_1,UI_SOURCE_Z+UI_METRIC_0_01F);
  else C2D_DrawCircleSolid(x,y,UI_SOURCE_Z+UI_METRIC_0_01F,UI_SOURCE_DOT,ink);
 }
}

/* Shared long-help dialog. Bound each page and split only at a word boundary. */
PSX3D_COLD void ui_help_pages(const char *title,const char *text,bool show_game)
{
 size_t offsets[UI_HELP_MAX_PAGES+UI_METRIC_1]={UI_METRIC_0};unsigned pages=UI_METRIC_0;
 size_t length=strlen(text),position=UI_METRIC_0;
 do {
  size_t end=position+UI_HELP_PAGE_CHARS;
  if(end>length)end=length;
  else {
   size_t word=end;while(word>position && text[word]!=' ' && text[word]!='\n')word--;
   if(word>position)end=word;
   else while(end>position && ((unsigned char)text[end]&UI_TEXT_UTF8_MASK)==UI_TEXT_UTF8_CONT)end--;
  }
  offsets[++pages]=end;position=end;
 } while(position<length && pages<UI_HELP_MAX_PAGES);
 for(unsigned page=UI_METRIC_0;page<pages;) {
  char body[UI_HELP_PAGE_CHARS+UI_METRIC_1],head[UI_METRIC_160];
  size_t n=offsets[page+UI_METRIC_1]-offsets[page];memcpy(body,text+offsets[page],n);body[n]=UI_METRIC_0;
  snprintf(head,sizeof(head),"%s %u/%u",title,page+UI_METRIC_1,pages);
  bool next=ui_confirm_choice(head,body,page+UI_METRIC_1==pages?"A  Done":"A  Next",page?"B  Back":"B  Close",show_game);
  if(!aptMainLoop())return;
  if(next)page++;else if(page)page--;else return;
 }
}
