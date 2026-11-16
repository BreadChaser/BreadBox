#include <assert.h>
#include <stdbool.h>
#include "cold_code.h"
#undef PSX3D_COLD
#define PSX3D_COLD
bool gspHasGpuRight(void);
#include "../../app/ui/whatsnew_ui.c"
#include "card_advance.h"
static unsigned font_index;
float ui_text_width(C2D_Font font,float size,const char *s)
{(void)font;float width=0;for(;*s;s++)width+=advance[font_index][(unsigned char)*s];return width*size;}
static float measured(const char *s,float size,void *ud){(void)ud;return ui_text_width(NULL,size,s);}
/* The input policy delegates repeat and motion to these shared list operations. */
u32 ui_list_input(const flist_t *f,int n,int *sel,u32 keys,bool horizontal)
{(void)f;(void)sel;assert(n==-1);assert(!horizontal);return keys;}
void ui_card_update(flist_t *f,int sel,int n)
{(void)n;f->scroll=sel;f->lift[sel]=1;}
int main(void)
{
 for(font_index=0;font_index<sizeof(advance)/sizeof(*advance);font_index++) {
 char release[]=BB_CHANGELOG_TEXT;ui_fit_line release_lines[6];
 int count=prepare(release,release_lines);assert(count>0 && count<=6);
 for(int i=0;i<count;i++) {
  char fitted[UI_FIT_LINE_MAX];bool cut;
  float size=ui_fit_bounded(release_lines[i],UI_CARD_ROW_SIZE,UI_CARD_ROW_SIZE,
   UI_CARD_X+UI_CARD_W-UI_CARD_RIGHT-UI_CARD_TEXT_X-UI_CARD_NUDGE,measured,NULL,fitted,sizeof(fitted),&cut);
  assert(!cut && size==UI_CARD_ROW_SIZE && !strcmp(fitted,release_lines[i]));
 }
 }
 font_index=0;
 char text[]="One change\nAnother change\nThird change";ui_fit_line lines[3];
 assert(prepare(text,lines)==3);assert(!strcmp(lines[0],"One change"));assert(!strcmp(lines[2],"Third change"));
 char longline[128];memset(longline,'W',sizeof(longline)-1);longline[127]=0;assert(!prepare(longline,NULL));
 flist_t list={0};ui_card_scroll_t scroll={0};input_contact_t touch={0};
 for(int i=0;i<20;i++)ui_card_scroll_input(&scroll,&list,12,KEY_DOWN,touch);
 assert(scroll.top==6 && list.scroll==6 && list.lift[6]==0);
 ui_card_scroll_input(&scroll,&list,12,KEY_CPAD_UP,touch);assert(scroll.top==5);
 for(int i=0;i<20;i++)ui_card_scroll_input(&scroll,&list,12,KEY_UP,touch);
 assert(scroll.top==0);
 touch=(input_contact_t){UI_CARD_TEXT_X,UI_CARD_ROWS_Y+UI_CARD_ROW*4,true,true};
 ui_card_scroll_input(&scroll,&list,12,0,touch);touch.down=false;touch.y-=UI_CARD_ROW*3;
 ui_card_scroll_input(&scroll,&list,12,0,touch);assert(scroll.top==3);
 touch.held=false;assert(ui_card_scroll_input(&scroll,&list,12,KEY_A|KEY_B,touch)==(KEY_A|KEY_B));assert(!scroll.dragging);
 ui_card_scroll_input(&scroll,&list,3,KEY_DOWN,touch);assert(!scroll.top && !list.scroll);
 puts("One highlight per line; button, Circle Pad and drag scroll bounds: PASS");
}
