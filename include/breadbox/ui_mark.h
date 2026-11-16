#ifndef UI_MARK_H
#define UI_MARK_H
/* The selection highlighter (owner 2026-11-12: "a yellow highlighter with some randomness and scratchiness"):
 * a marker stroke multiplied over the paper behind the selected row of every notebook card (ui_card_draw) and
 * of the room's and Collection's lists (shelf_scene_draw.c). One routine, ui_mark_row (app/ui/ui_mark.c).
 *
 * The stroke is a 3-slice of one of UI_MARK_VARIANTS masks (tools/ui/mark_masks.py -> ui_mark_masks.h, an
 * 8 KiB L8 texture): the nib's start cap, the middle stretched to the text, the dry tail. The variant, a 0/1 px
 * drop and a -1/0/+1 px tilt come from the row's index, so a row's stroke never changes from frame to frame.
 * When the selection moves, the stroke is drawn left to right in UI_MARK_DRAW_S.
 * Pure C (no citro2d): the draw and the host tests use the same layout. */
#include <math.h>
#include "ui_style.h"

#define UI_MARK_VARIANTS 4
#define UI_MARK_TEX_W 128             // one stroke per 128 x 16 cell; the four stacked: a 128 x 64 texture
#define UI_MARK_TEX_H 16
#define UI_MARK_CAP 12                // texels [0, 12): the start cap; [96, 128): the dry tail
#define UI_MARK_TAIL 32

typedef struct {
	float x0, x1;                     // screen x of the slice's ends
	float u0, u1;                     // texel columns of the mask it shows
} ui_mark_slice;
typedef struct {
	int variant;                      // the mask (row index % UI_MARK_VARIANTS)
	int n;                            // slices drawn (0 when nothing shows yet)
	ui_mark_slice s[3];
	float top, bottom;                // the quads' top and bottom at the stroke's start (x = x0)
	float tilt;                       // y change over the whole stroke (the right end sits tilt lower)
	float x0, x1;                     // the whole stroke when drawn out
} ui_mark_geo;

static inline unsigned ui_mark_hash(int row)
{
	unsigned h = (unsigned)row * 2654435761u;
	return h ^ (h >> 15);
}

/* progress 0..1 of the draw-on (eased); the stroke covers [x0, x1] once it is 1. */
static inline void ui_mark_layout(float x0, float x1, float line, int row, float progress, ui_mark_geo *g)
{
	unsigned h = ui_mark_hash(row);
	if (row < 0) row = -row;
	g->variant = row % UI_MARK_VARIANTS;
	x0 -= UI_MARK_BEFORE;
	x1 += UI_MARK_AFTER;
	if (x1 - x0 < UI_MARK_MIN_W) x1 = x0 + UI_MARK_MIN_W;
	g->x0 = x0;
	g->x1 = x1;
	g->top = line - UI_MARK_ABOVE + (float)((h >> 3) & 1u);
	g->bottom = g->top + UI_MARK_HEIGHT;
	g->tilt = (float)((int)((h >> 5) % 3u) - 1);
	progress = progress < 0 ? 0 : progress > 1 ? 1 : progress;
	progress = progress * progress * (3 - 2 * progress);
	float w = (x1 - x0) * progress;
	g->n = 0;
	if (w < 1.0f) return;
	const float cap = UI_MARK_CAP, tail = UI_MARK_TAIL, ends = UI_MARK_CAP + UI_MARK_TAIL;
	float k = w < ends ? w / ends : 1.0f;   // shorter than both ends (the first frames of the draw-on): scale them
	float a = x0 + cap * k, b = x0 + w - tail * k;
	g->s[g->n++] = (ui_mark_slice){ x0, a, 0, cap };
	if (b > a) g->s[g->n++] = (ui_mark_slice){ a, b, cap, UI_MARK_TEX_W - tail };
	g->s[g->n++] = (ui_mark_slice){ b > a ? b : a, x0 + w, UI_MARK_TEX_W - tail, UI_MARK_TEX_W };
}

/* The draw-on's progress from the card's row lift (ui_card_update: 1 - e^(-rate t) toward the selection):
 * complete UI_MARK_DRAW_S after the selection moved. */
static inline float ui_mark_progress_from_lift(float lift)
{
	return lift / (1.0f - expf(-UI_MOTION_SETTLE_RATE * UI_MARK_DRAW_S));
}

/* The stroke's darkening per channel (multiply: paper x (1 - k x UI_MARK_INK)), 0..1. */
#define UI_MARK_INK_R ((255.0f - UI_MARK_YELLOW_R) / 255.0f * UI_MARK_STRENGTH)
#define UI_MARK_INK_G ((255.0f - UI_MARK_YELLOW_G) / 255.0f * UI_MARK_STRENGTH)
#define UI_MARK_INK_B ((255.0f - UI_MARK_YELLOW_B) / 255.0f * UI_MARK_STRENGTH)

/* The stroke behind a row whose text runs from x0 to x1 and sits on `line`. strength 0..1 fades it with its card;
 * z: the card's selection depth. */
void ui_mark_row(float x0, float x1, float line, int row, float progress, float strength, float z);
#endif
