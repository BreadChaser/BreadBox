// A Room card lies turned by UI_CARD_TILT; a touch on any row, as drawn, maps back to that row. List looks: straight.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <citro2d.h>
#include "ui_pause.h"
#include "ui_style.h"
static bool room;
static touchPosition scripted;
bool theme_is_room(void) { return room; }
void hidTouchRead(touchPosition *t) { *t = scripted; }
void C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 c) { (void)x; (void)y; (void)z; (void)w; (void)h; (void)c; }
#include "../../app/ui/card_notice.c"
float ui_card_tilt(void) { return room ? UI_CARD_TILT : UI_PC_STRAIGHT; }   /* ui.c's, which links with the card */
int main(void)
{
	for (room = false; ; room = true) {
		float a = ui_card_tilt(), mx = UI_CARD_X + UI_CARD_W / 2.0f, my = UI_CARD_Y + UI_CARD_H / 2.0f;
		assert(room ? a == UI_CARD_TILT && a != 0 : a == 0);
		for (int row = 0; row < UI_CARD_ROWS; row++)
			for (float x = UI_CARD_X + 2; x < UI_CARD_X + UI_CARD_W - 2; x += 10) {
				/* the middle of the row's band at x, turned as the card is drawn */
				float y = UI_CARD_ROWS_Y + row * UI_CARD_ROW + UI_CARD_ROW / 2.0f, dx = x - mx, dy = y - my;
				scripted = (touchPosition){ (u16)lroundf(mx + dx * cosf(a) - dy * sinf(a)), (u16)lroundf(my + dx * sinf(a) + dy * cosf(a)) };
				touchPosition t;
				ui_card_touch(&t);
				assert((int)floorf((t.py - UI_CARD_ROWS_Y) / (float)UI_CARD_ROW) == row && fabsf(t.px - x) <= 1);
			}
		if (room) break;
	}
	puts("PASS: a touch on a tilted Room card's row hits that row; list looks' cards are straight");
	return 0;
}
