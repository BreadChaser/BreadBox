/* Shared catalog touch geometry; the room and list controllers use the same targets. */
#include <math.h>
#include "shelf.h"
#include "ui_style.h"
#include "cold_code.h"
#define X0 40.0f
PSX3D_COLD float bb_library_strip_length(const shelf_row *r) { return fmaxf(r->end - X0, 400); }
PSX3D_COLD float shelf_scene_strip_position(const shelf_row *r, float x) { return 12 + (x - X0) / bb_library_strip_length(r) * 296; }
PSX3D_COLD int shelf_scene_strip_hit(const shelf_view *v, float x, float y)
{
	if (v->level != SHELF_ONE || y >= 44) return -1;
	const shelf_row *r = &v->rows[v->row];
	int best = -1;
	float bd = 1e9f;
	for (int i = 0; i < r->n; i++) {
		float d = fabsf(shelf_scene_strip_position(r, r->box[i].x) - x);
		if (d < bd) { bd = d; best = i; }
	}
	return best;
}
static float home_row_y(const shelf_view *v, int i) { (void)v; return UI_ROOM_ROWS_Y + i * UI_ROOM_LIST_STEP; }
PSX3D_COLD int shelf_scene_card_hit(const shelf_view *v, float x, float y)
{
	(void)x;
	for (int i = 0; i < v->nrows; i++) {   // a row's band: between the ruled line above it and its own
		float ry = UI_ROOM_CARD_Y + home_row_y(v, i);
		if (y >= ry - UI_PAPER_LINE + 1 && y < ry + 1) return i;   // from the line above to its own line
	}
	return -1;
}

