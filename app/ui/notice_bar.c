#include "ui.h"
#include "ui_pause.h"
#include "theme.h"
#include "cold_code.h"
#include "components/notice_bar.inc"

PSX3D_COLD void ui_notice_bar(const char *s,int frames)
{
 if(frames<=0 || !s || !*s)return;
 int alpha=frames>UI_NOTICE_FADE?UI_ALPHA_OPAQUE:frames*UI_ALPHA_OPAQUE/UI_NOTICE_FADE;
 notice_bar_draw(s,alpha);
}
