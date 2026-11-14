/* The selection highlighter's geometry (include/breadbox/ui_mark.h) and masks (ui_mark_masks.h), production code:
 * the stroke covers the row's text from UI_MARK_BEFORE left of it to UI_MARK_AFTER past it, stays inside its row
 * (clear of the line above's text and the next row's capitals), is the same every frame for a row, grows left to
 * right while it is drawn, and its three slices join. The masks: a dry stroke (inked middle, fading tail). */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ui_mark.h"
#include "ui_mark_masks.h"
static void slices_join(const ui_mark_geo *g, float x0, float w)
{
	assert(g->n >= 2 && g->n <= 3);
	assert(fabsf(g->s[0].x0 - x0) < 1e-3f && fabsf(g->s[g->n - 1].x1 - (x0 + w)) < 1e-3f);
	for (int i = 0; i < g->n; i++) {
		assert(g->s[i].x1 >= g->s[i].x0 - 1e-3f && g->s[i].u1 > g->s[i].u0);
		assert(g->s[i].u0 >= 0 && g->s[i].u1 <= UI_MARK_TEX_W);
		if (i) assert(fabsf(g->s[i].x0 - g->s[i - 1].x1) < 1e-3f && (g->s[i].u0 == g->s[i - 1].u1 || g->n == 2));   /* a short stroke: cap then tail */
	}
	assert(g->s[0].u0 == 0 && g->s[g->n - 1].u1 == UI_MARK_TEX_W);   /* the cap starts, the tail ends the stroke */
}
int main(void)
{
	const float line = 87;   /* a card row's ruled line */
	int strokes = 0;
	for (int row = 0; row < 40; row++)
		for (float w = 2; w < 260; w += 7) {
			float x0 = 54, x1 = x0 + w;
			ui_mark_geo a, b;
			memset(&a, 0xa5, sizeof(a)); memset(&b, 0x5a, sizeof(b));
			ui_mark_layout(x0, x1, line, row, 1, &a);
			ui_mark_layout(x0, x1, line, row, 1, &b);
			/* Only active slices belong to the output contract; unused slots and
			 * padding keep the deliberately different poison above. */
			assert(a.variant == b.variant && a.n == b.n);
			assert(a.top == b.top && a.bottom == b.bottom && a.tilt == b.tilt);
			assert(a.x0 == b.x0 && a.x1 == b.x1);
			for (int i = 0; i < a.n; i++) {
				assert(a.s[i].x0 == b.s[i].x0 && a.s[i].x1 == b.s[i].x1);
				assert(a.s[i].u0 == b.s[i].u0 && a.s[i].u1 == b.s[i].u1);
			}
			assert(a.variant == row % UI_MARK_VARIANTS);
			assert(fabsf(a.x0 - (x0 - UI_MARK_BEFORE)) < 1e-3f);
			assert(a.x1 >= x1 + UI_MARK_AFTER - 1e-3f && a.x1 - a.x0 >= UI_MARK_MIN_W - 1e-3f);
			assert(a.x1 - a.x0 < UI_MARK_MIN_W + 1e-3f || fabsf(a.x1 - (x1 + UI_MARK_AFTER)) < 1e-3f);
			/* inside its row: below the line above's text, above the next row's capitals */
			assert(a.top >= line - UI_PAPER_LINE + 1 && a.bottom + fabsf(a.tilt) <= line + UI_PAPER_LINE - UI_PAPER_CAP);
			assert(a.bottom - a.top == UI_MARK_HEIGHT && fabsf(a.tilt) <= 1);
			slices_join(&a, a.x0, a.x1 - a.x0);
			/* the draw-on: nothing, then growing from the left, never past the end */
			ui_mark_geo p;
			ui_mark_layout(x0, x1, line, row, 0, &p);
			assert(p.n == 0);
			float last = 0;
			for (float t = .05f; t <= 1.0001f; t += .05f) {
				ui_mark_layout(x0, x1, line, row, t, &p);
				if (!p.n) continue;
				float end = p.s[p.n - 1].x1;
				assert(end >= last - 1e-3f && end <= a.x1 + 1e-3f && fabsf(p.s[0].x0 - a.x0) < 1e-3f);
				assert(p.top == a.top && p.variant == a.variant);
				slices_join(&p, p.x0, end - p.x0);
				last = end;
			}
			assert(fabsf(last - a.x1) < 1e-3f);
			strokes++;
		}
	/* drawn out UI_MARK_DRAW_S after the selection moved (the card's lift: 1 - e^-rate t) */
	assert(ui_mark_progress_from_lift(1 - expf(-UI_MOTION_SETTLE_RATE * UI_MARK_DRAW_S)) > .999f);
	assert(ui_mark_progress_from_lift(1 - expf(-UI_MOTION_SETTLE_RATE * UI_MARK_DRAW_S / 2)) < .75f);
	/* the masks: every stroke inked through its middle, lighter at its dry tail, empty at the cell's edges */
	assert(sizeof(ui_mark_masks) == 8192);
	for (int v = 0; v < UI_MARK_VARIANTS; v++) {
		double mid = 0, tail = 0;
		for (int y = 0; y < UI_MARK_TEX_H; y++)
			for (int x = 0; x < UI_MARK_TEX_W; x++) {
				if (x >= UI_MARK_CAP && x < UI_MARK_TEX_W - UI_MARK_TAIL) mid += ui_mark_masks[v][y][x];
				else if (x >= UI_MARK_TEX_W - UI_MARK_TAIL) tail += ui_mark_masks[v][y][x];
			}
		mid /= (UI_MARK_TEX_W - UI_MARK_CAP - UI_MARK_TAIL) * UI_MARK_TEX_H;
		tail /= UI_MARK_TAIL * UI_MARK_TEX_H;
		assert(mid > 120 && tail < mid * .7);
		for (int x = 0; x < UI_MARK_TEX_W; x++) assert(!ui_mark_masks[v][0][x] && !ui_mark_masks[v][UI_MARK_TEX_H - 1][x]);
		/* the middle is inked across: no column of it is empty (a stretched gap would cut the stroke) */
		for (int x = UI_MARK_CAP; x < UI_MARK_TEX_W - UI_MARK_TAIL; x++) {
			int col = 0;
			for (int y = 0; y < UI_MARK_TEX_H; y++) col += ui_mark_masks[v][y][x];
			assert(col > 255 * 4);
		}
	}
	/* multiply: the darkest the paper gets is the marker yellow at UI_MARK_STRENGTH (the mockup's contrast) */
	assert(UI_MARK_INK_R < .01f && UI_MARK_INK_G > .1f && UI_MARK_INK_B > .6f && UI_MARK_INK_B < .7f);
	printf("ui_mark PASS: %d strokes (40 rows x widths 2..254): stable, inside the row, slices joined, drawn out left to right; 4 masks of 128 x 16 (8 KiB)\n", strokes);
	return 0;
}
