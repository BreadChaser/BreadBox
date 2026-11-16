#!/usr/bin/env python3
"""Run actual shared scroll/hit-test/navigation functions, including old-metric callers."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2];s=(root/'app/ui/ui.c').read_text()
def function(name):
 import re
 m=re.search(r'(?:static )?(?:float|void|int|u32) '+name+r'\([^;]*?\)\n\{',s)
 a=m.start();i=m.end();depth=1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
h=(root/'include/breadbox/ui_list.h').read_text();a=h.index('#define FLIST_MAX');b=h.index('void flist_update',a)
code='''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "3ds.h"
#include "ui_style.h"
#include "ui_motion.h"
#define SYSCLOCK_ARM11 60000ULL
static u64 fixture_tick;
static u64 svcGetSystemTick(void){fixture_tick+=1000;return fixture_tick;}
static u32 held;
static touchPosition touch;
u32 input_keys_held(void){return held;}
void hidTouchRead(touchPosition *p){*p=touch;}
enum {SFX_MOVE};static int moves;static void sfx_now(int e){(void)e;moves++;}
'''+'#include "ui_card_scroll.h"\n'+'\n'.join(function(n) for n in ['flist_dt','flist_update','flist_snap','flist_row_at','list_keys','ui_list_input','ui_card_update','ui_card_input'])+r'''
int main(void){
 flist_t f={0};int sel=0;
 flist_snap(&f,0,315,7);assert(f.scroll==0);
 // Canonical geometry even when an old caller still supplies y40/21.
 assert(flist_row_at(&f,40,21,4)==0);
 assert(flist_row_at(&f,40,21,27)==0);
 assert(flist_row_at(&f,40,21,28)==1);
 assert(flist_row_at(&f,40,21,196)==-1);
 assert(flist_row_at(&f,40,21,0)==-1);
 touch=(touchPosition){20,28};u32 k=ui_list_input(&f,315,&sel,KEY_TOUCH,false);
 assert(sel==1 && !(k&KEY_A));k=ui_list_input(&f,315,&sel,KEY_TOUCH,false);assert(k&KEY_A);
 touch.py=210;k=ui_list_input(&f,315,&sel,KEY_TOUCH,false);assert(sel==1 && !(k&KEY_A));
 touch=(touchPosition){319,52};k=ui_list_input(&f,315,&sel,KEY_TOUCH,false);assert(sel==1);
 sel=0;ui_list_input(&f,315,&sel,KEY_UP,false);assert(sel==0);
 sel=314;ui_list_input(&f,315,&sel,KEY_DOWN,false);assert(sel==314);
 // Settings Left/Right remain values rather than navigation.
 sel=2;ui_list_input(&f,4,&sel,KEY_LEFT,false);assert(sel==2);
 held=KEY_DOWN;sel=0;ui_list_input(&f,315,&sel,KEY_DOWN,false);assert(sel==1);
 for(int i=0;i<18;i++)ui_list_input(&f,315,&sel,0,false);assert(sel==1);
 for(int i=0;i<4;i++)ui_list_input(&f,315,&sel,0,false);assert(sel==2);
 held=0;sel=0;k=ui_list_input(&f,0,&sel,KEY_A,false);assert(!(k&KEY_A));
 for(int i=0;i<180;i++)flist_update(&f,314,315,7);
 assert(fabsf(f.scroll-(315-UI_LIST_ROWS))<.01f && fabsf(f.hl-314)<.01f);   /* the last page of UI_LIST_ROWS rows */
 // Refresh to an empty list clamps the scroll back to zero.
 for(int i=0;i<180;i++)flist_update(&f,0,0,7);assert(fabsf(f.scroll)<.01f);
 // The notebook card: a page of UI_CARD_ROWS rows fits without scrolling; more scroll by whole rows; its own touch rows.
 {
  flist_t c={0};int cs=UI_CARD_ROWS-1;
  for(int i=0;i<180;i++)ui_card_update(&c,cs,UI_CARD_ROWS);assert(fabsf(c.scroll)<.01f && fabsf(c.hl-(UI_CARD_ROWS-1))<.01f);
  cs=11;for(int i=0;i<180;i++)ui_card_update(&c,cs,12);assert(fabsf(c.scroll-(12-UI_CARD_ROWS))<.01f);
  c.scroll=c.scroll_target=0;cs=0;
  touch=(touchPosition){UI_CARD_TEXT_X,UI_CARD_ROWS_Y+3*UI_CARD_ROW+UI_CARD_ROW/2};
  k=ui_card_input(&c,12,&cs,KEY_TOUCH);assert(cs==3 && !(k&KEY_A));
  k=ui_card_input(&c,12,&cs,KEY_TOUCH);assert(cs==3 && (k&KEY_A));
  touch.py=UI_CARD_ROWS_Y+UI_CARD_ROWS*UI_CARD_ROW+UI_CARD_ROW;   /* below the card's rows: the bar's tap, not ours */
  k=ui_card_input(&c,12,&cs,KEY_TOUCH);assert(cs==3 && (k&KEY_TOUCH) && !(k&KEY_A));
  touch=(touchPosition){UI_CARD_TEXT_X,UI_CARD_ROWS_Y-2};k=ui_card_input(&c,12,&cs,KEY_TOUCH);assert(cs==3 && (k&KEY_TOUCH));
  cs=0;k=ui_card_input(&c,12,&cs,KEY_UP);assert(cs==0);k=ui_card_input(&c,12,&cs,KEY_DOWN|KEY_RIGHT);assert(cs==1 && (k&KEY_RIGHT));
  cs=11;ui_card_input(&c,12,&cs,KEY_DOWN);assert(cs==11);
 }
 // Read-only cards use these same production repeat and motion functions.
 {
  flist_t reader={0};ui_card_scroll_t scroll={0};input_contact_t contact={0};
  held=KEY_CPAD_DOWN;ui_card_scroll_input(&scroll,&reader,12,held,contact);
  for(int i=0;i<180;i++)ui_card_scroll_input(&scroll,&reader,12,0,contact);
  assert(scroll.top==6 && reader.scroll==6 && !reader.scroll_v);
  held=KEY_UP;ui_card_scroll_input(&scroll,&reader,12,held,contact);
  for(int i=0;i<180;i++)ui_card_scroll_input(&scroll,&reader,12,0,contact);
  assert(scroll.top==0 && reader.scroll==0);
  held=0;contact=(input_contact_t){UI_CARD_TEXT_X,UI_CARD_ROWS_Y+4*UI_CARD_ROW,true,true};
  ui_card_scroll_input(&scroll,&reader,12,0,contact);contact.down=false;contact.y-=3*UI_CARD_ROW;
  for(int i=0;i<180;i++)ui_card_scroll_input(&scroll,&reader,12,0,contact);
  assert(scroll.top==3 && reader.scroll==3 && !reader.scroll_v);
  contact.held=false;assert(ui_card_scroll_input(&scroll,&reader,12,KEY_B,contact)==KEY_B);
  ui_card_scroll_input(&scroll,&reader,3,KEY_DOWN,contact);assert(!scroll.top);
 }
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'check.c').write_text(code)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-include','stddef.h','-fsanitize=address,undefined','-I'+str(root/'tests/app/host'),'-I'+str(root/'app/ui'),'-I'+str(root/'include/breadbox'),'-I'+str(root/'platform/3ds'),str(p/'check.c'),str(root/'app/ui/card_scroll.c'),'-lm','-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
print('Shared list canonical geometry, section navigation, tap/open, repeat, empty/clamped scroll, pause card scroll and touch rows PASS')
