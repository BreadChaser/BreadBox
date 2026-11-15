// The card's notice strip with the ruled line its text stands on. Its own file: ui_card_draw's cold code sits
// right ahead of the GBA save dialogs in the cold run, so this draw call lives where the link order puts new
// code (last) and ui_card_draw keeps its size (tools/layout: those dialogs must not move).
#include <citro2d.h>
#include "cold_code.h"
#include "ui_pause.h"
#include "ui_style.h"

PSX3D_COLD void ui_card_notice_strip(float x, float y, float z, float w, float h, u32 color, u32 lines)
{
	C2D_DrawRectSolid(x, y, z, w, h, color);
	C2D_DrawRectSolid(x, y + UI_CARD_NOTICE_H - UI_CARD_FOOT_MARGIN, UI_CARD_Z_NOTICE_LINE, w, UI_METRIC_1, lines);
}
