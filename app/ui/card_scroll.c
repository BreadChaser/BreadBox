/* Read-only ruled cards use the list's repeat and settling, with whole-row drag targets. */
#include "ui_card_scroll.h"
#include "ui_pause.h"
#include "cold_code.h"

PSX3D_COLD u32 ui_card_scroll_input(ui_card_scroll_t *s,flist_t *list,int rows,u32 keys,input_contact_t touch)
{
 int unused=0,limit=rows-UI_CARD_ROWS;if(limit<0)limit=0;
 keys=ui_list_input(list,-1,&unused,keys&~KEY_TOUCH,false);
 if(keys&(KEY_UP|KEY_CPAD_UP))s->top--;
 if(keys&(KEY_DOWN|KEY_CPAD_DOWN))s->top++;
 if(touch.down && touch.x>=UI_CARD_X && touch.x<UI_CARD_X+UI_CARD_W &&
    touch.y>=UI_CARD_ROWS_Y && touch.y<UI_CARD_ROWS_Y+UI_CARD_ROWS*UI_CARD_ROW) {
  s->dragging=true;s->drag_y=touch.y;s->drag_top=s->top;
 }
 if(s->dragging && touch.held)s->top=s->drag_top+(int)roundf((s->drag_y-touch.y)/(float)UI_CARD_ROW);
 if(!touch.held)s->dragging=false;
 if(s->top<0)s->top=0;if(s->top>limit)s->top=limit;
 list->scroll_target=s->top;ui_card_update(list,s->top,rows);
 /* This card has no selected row and therefore no selection lift. */
 if(s->top<FLIST_MAX)list->lift[s->top]=0;
 if(fabsf(list->scroll-s->top)<0.002f){list->scroll=s->top;list->scroll_v=0;}
 return keys;
}
