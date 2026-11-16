#include <stddef.h>
#include <assert.h>
#include <string.h>
#include "ui.h"
#include "ui_pause.h"
#include "theme.h"
static theme_t colors;
static int rectangles,texts;
u32 theme_alpha(u32 color,int alpha){return (color&0xffffff)|((u32)alpha<<24);}
const theme_t *theme(void){return &colors;}
u32 ui_bar_color(int which){assert(which==UI_COL_NOTICE);return 0xffabcdef;}
void C2D_DrawRectSolid(float x,float y,float z,float w,float h,u32 color)
{
 (void)z;(void)color;assert(x==0 && y==UI_NOTICE_TOP && w==UI_TOP_SCREEN_WIDTH);
 assert(h==UI_NOTICE_HEIGHT || h==UI_NOTICE_RULE);rectangles++;
}
const char *ui_fit(const char *s,float size,float width)
{assert(size==UI_CARD_ROW_SIZE && width==UI_TOP_SCREEN_WIDTH-UI_NOTICE_INSET*2);return s;}
void ui_text_ex(C2D_Font font,float x,float y,float size,u32 color,u32 flags,const char *s)
{
 (void)font;(void)color;assert(x==UI_NOTICE_INSET && y>=UI_NOTICE_TOP && y<UI_SCREEN_HEIGHT);
 assert(size==UI_CARD_ROW_SIZE && flags==0 && s[0]);texts++;
}
int main(void)
{
 ui_notice_bar(NULL,60);ui_notice_bar("",60);ui_notice_bar("Hidden",0);assert(!rectangles && !texts);
 const char *messages[]={"Update found","Ready. Reopen to install","Battery low. Plug in","Opening your collection"};
 for(unsigned i=0;i<sizeof(messages)/sizeof(*messages);i++)ui_notice_bar(messages[i],60);
 assert(rectangles==8 && texts==4);ui_notice_bar("Leaving",1);assert(texts==5);
}
