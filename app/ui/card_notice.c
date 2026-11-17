// The card's notice strip with the ruled line its text stands on, touches on the Room's tilted cards. Its own file: ui_card_draw's cold code sits
// right ahead of the GBA save dialogs in the cold run, so this draw call lives where the link order puts new
// code (last) and ui_card_draw keeps its size (tools/layout: those dialogs must not move).
#include <citro2d.h>
#include "cold_code.h"
#include "ui_pause.h"
#include <math.h>
#include <3ds.h>
#include "ui_style.h"
#include "look_theme.h"

PSX3D_COLD void ui_card_notice_strip(float x, float y, float z, float w, float h, u32 color, u32 lines)
{
	C2D_DrawRectSolid(x, y, z, w, h, color);
	C2D_DrawRectSolid(x, y + UI_CARD_NOTICE_H - UI_CARD_FOOT_MARGIN, UI_CARD_Z_NOTICE_LINE, w, 1, lines);
}

// The Room's cards lie turned by UI_CARD_TILT about their middle: a touch is turned back onto the card, so a row
// is hit where it is drawn. The list looks' cards are straight.
PSX3D_COLD void ui_card_touch(touchPosition *t)
{
	hidTouchRead(t);
	float a = -ui_card_tilt(), mx = UI_CARD_X + UI_CARD_W / 2.0f, my = UI_CARD_Y + UI_CARD_H / 2.0f, dx = t->px - mx, dy = t->py - my;
	t->px = (u16)lroundf(mx + dx * cosf(a) - dy * sinf(a));
	t->py = (u16)lroundf(my + dx * sinf(a) + dy * cosf(a));
}
/* Turn what is drawn next about (cx, cy) by the card tilt (the caller saves and restores the view). */
PSX3D_COLD void ui_card_turn(float cx, float cy)
{
	C2D_ViewTranslate(cx, cy);
	C2D_ViewRotate(ui_card_tilt());
	C2D_ViewTranslate(-cx, -cy);
}

// The header row (the title on the red rule) under the touch: tapping the title goes back (docs/settings.md).
PSX3D_COLD bool ui_card_title_hit(void)
{
	touchPosition t;
	ui_card_touch(&t);
	return t.px >= UI_CARD_X && t.px < UI_CARD_X + UI_CARD_W && t.py >= UI_CARD_Y && t.py < UI_CARD_ROWS_Y;
}
