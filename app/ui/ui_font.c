// The loaded fonts' own metrics, read from their glyphs (owner 2026-11-12: on a real 3DS the card text sat ~3 px
// under the ruled lines while it sat on them in Azahar). citro2d draws a glyph's cell top at the y it is given and
// scales every font to a 30 px cell; where the letters stand in that cell is the font's own. Nintendo's system font
// stands at about 24-25 (hardware stills); Azahar's replacement declares 25 but its letters stand at 20; the title
// font (romfs) stands at 24.3. A fixed number fits one font only, so the baseline is the bottom of the font's
// flat letters, measured once per face from its glyph sheet, with the declared baselinePos as the fallback.
#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include "ui.h"
#include "ui_font.h"
#include "libui.h"
#include "app_log.h"
#include "devtools.h"
#include "cold_code.h"

#define UI_FONT_CELL 30.0f            // citro2d draws every font's cell this tall at size 1
#define UI_FONT_FLAT "HEILTxz"        // letters whose bottoms are flat on the baseline (no overshoot, no descender)
#define UI_FONT_CAPS "HEILT"          // flat-topped capitals
#define UI_FONT_INK_HALF 128          // a texel at least half covered is ink (0..255)

// A texel's place in a tiled (8x8 Morton) A4/A8 sheet.
static unsigned font_texel(int x, int y, int width)
{
	unsigned morton = (x&1) | ((y&1)<<1) | ((x&2)<<1) | ((y&2)<<2) | ((x&4)<<2) | ((y&4)<<3);
	return ((y/8)*(width/8)+x/8)*64 + morton;
}

// The first and last cell rows (0 = the cell's top) that hold ink of the letters of s at min_alpha (1..255).
// False when the sheet is not A4/A8 or no letter has ink.
bool ui_font_ink_rows(C2D_Font font, const char *s, int min_alpha, int *top, int *bottom)
{
	const TGLP_s *g = C2D_FontGetInfo(font)->tglp;
	int t = g->cellHeight, b = -1;
	if (g->sheetFmt != GPU_A4 && g->sheetFmt != GPU_A8) return false;
	for (const char *p = s; *p; p++) {
		int index = C2D_FontGlyphIndexFromCodePoint(font, (unsigned char)*p);
		int per_sheet = g->nRows*g->nLines, local = index%per_sheet;
		const u8 *sheet = g->sheetData+(index/per_sheet)*g->sheetSize;
		int ox = (local%g->nRows)*(g->cellWidth+1)+1;
		int oy = (local/g->nRows)*(g->cellHeight+1)+1;
		int width = C2D_FontGetCharWidthInfo(font, index)->glyphWidth;
		for (int row = 0; row < g->cellHeight; row++)
			for (int col = 0; col < width; col++) {
				unsigned pos = font_texel(ox+col, oy+row, g->sheetWidth);
				int alpha = g->sheetFmt == GPU_A8 ? sheet[pos] : ((sheet[pos/2] >> (4*(pos&1))) & 15) * 17;
				if (alpha >= min_alpha) { if (row < t) t = row; if (row > b) b = row; }
			}
	}
	if (b < t) return false;
	*top = t; *bottom = b;
	return true;
}

static C2D_Font face_font(int face) { return face == UI_FACE_TITLE ? libui_font() : NULL; }

// The font's baseline in its 30 px cell: the bottom edge of its flat letters' ink, else its declared baselinePos.
PSX3D_COLD static float measure_baseline(C2D_Font font, bool *measured)
{
	const TGLP_s *g = C2D_FontGetInfo(font)->tglp;
	int top, bottom;
	*measured = ui_font_ink_rows(font, UI_FONT_FLAT, UI_FONT_INK_HALF, &top, &bottom);
	return (*measured ? bottom + 1 : g->baselinePos) * UI_FONT_CELL / g->cellHeight;
}

static float baseline[2];
static bool measured[2], ready[2];

float ui_face_baseline(int face)
{
	face = face == UI_FACE_TITLE;
	if (!ready[face]) {
		baseline[face] = measure_baseline(face_font(face), &measured[face]);
		ready[face] = true;
	}
	return baseline[face];
}

PSX3D_COLD void ui_font_log(void)
{
	if (!dev_enabled()) return;
	for (int face = UI_FACE_SYSTEM; face <= UI_FACE_TITLE; face++) {
		C2D_Font font = face_font(face);
		const FINF_s *f = C2D_FontGetInfo(font);
		int top = -1, bottom = -1;
		ui_font_ink_rows(font, UI_FONT_CAPS, UI_FONT_INK_HALF, &top, &bottom);
		float base = ui_face_baseline(face);
		log_printf("font %s: cell %dx%d baselinePos %d ascent %d lineFeed %d fmt %d; letters' baseline %.2f/30 (%s), caps rows %d..%d\n",
			face == UI_FACE_TITLE ? "title" : "system", f->tglp->cellWidth, f->tglp->cellHeight, f->tglp->baselinePos,
			f->ascent, f->lineFeed, f->tglp->sheetFmt, base, measured[face] ? "measured" : "declared", top, bottom);
	}
}
