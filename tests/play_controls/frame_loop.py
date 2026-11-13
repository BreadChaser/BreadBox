"""Compile the production GBA loop's HID/contact dispatch with its real adapters.
Core/video/SD services are fakes: this checks dispatch sequencing, not GPU timing.
"""
from pathlib import Path
import subprocess,tempfile,os
r=Path(__file__).resolve().parents[2]
gba=(r/'systems/gba/gba.c').read_text()
a=gba.index('        input_poll();',gba.index('while(loaded &&'))
b=gba.index('        if(test_mode>=2)',a)
dispatch=gba[a:b]
merge=next(line for line in gba.splitlines() if 'held=(held & ~(GBA_TOUCH_BITS' in line)
start=gba.index('static void touch_intro_did_paint(void)')
end=gba.index('static bool environment(',start)
intro_paint=gba[start:end]
prepare=gba[gba.index('static void touch_prepare(void)'):gba.index('static bool fps_pending;')]
prefix=(r/'tests/gba_speed/check.c').read_text().split('int main(void)')[0]
source=prefix+r'''
#include <string.h>
#include "gba_touch.h"
#include "game_palette.h"
game_palette_t g_game_palette;
void ui_play_draw(ui_play_controls *p){p->draws++;}
static input_contact_t contact;
static u32 physical,held;
static struct {uint32_t keys[16],turbo_a_key,turbo_b_key;} descriptor;
static int bb_active_controls;
static bool bottom_dirty;
static void bb_controls_map(int *unused,uint32_t *mapped,uint32_t *a,uint32_t *b){(void)unused;memset(mapped,0,16*sizeof(*mapped));*a=*b=0;}
static unsigned cycles,intro_writes;
u32 ui_resume_pending;
static bool touch_intro_pending,touch_intro_painted,touch_intro_visible,notice_visible;
static u64 notice_until;
#define SYSCLOCK_ARM11 1000
static u64 fake_tick;
u64 svcGetSystemTick(void){return fake_tick;}
void gba_touch_intro_shown(void){intro_writes++;}
void input_poll(void){}
u32 input_keys_held(void){return physical;}
input_contact_t input_contact(void){return contact;}
static void statsview_cycle(void){gba_touch_settings.fps=(gba_touch_settings.fps+1)%3;cycles++;}
static bool statsview_redraw(int unused){(void)unused;return false;}
'''+prepare+intro_paint+r'''
static unsigned frame(u32 buttons,int x,int y,bool touched,bool down)
{
 physical=buttons;contact=(input_contact_t){x,y,touched,down};
'''+dispatch+merge+r'''
 return touch_events;
}
int main(void)
{
 for(int newer=0;newer<2;newer++) {
  g_new3ds=newer;gba_touch_settings=GBA_TOUCH_DEFAULT;
  gba_speed_begin(0);assert(!gba_speed_history_kib());touch_prepare();frame(0,0,0,false,false);
  for(int f=0;f<600;f++)assert(!frame(0,0,0,false,false));
  unsigned e=frame(KEY_B,newer?250:140,60,true,true);
  assert((e&GBA_TOUCH_DIRTY_EVENT) && (held&KEY_B) && (held&descriptor.keys[8]));
  assert(!(e&GBA_TOUCH_MENU_EVENT));
  e=frame(0,20,20,true,false);assert(!(held&descriptor.keys[8]) && !(e&GBA_TOUCH_MENU_EVENT));
  frame(0,0,0,false,false);e=frame(0,20,20,true,true);assert(e&GBA_TOUCH_MENU_EVENT);
  gba_speed_pause(0);touch_prepare();frame(0,0,0,false,false);
  frame(0,30,90,true,true);assert(gba_speed_ff());
  frame(0,30,90,true,false);assert(gba_speed_ff());
  frame(0,0,0,false,false);frame(0,30,90,true,true);assert(!gba_speed_ff());
  if(newer) {frame(0,0,0,false,false);frame(0,30,150,true,true);assert(gba_speed_rewinding() && !gba_speed_ff());}
  gba_speed_pause(0);touch_prepare();frame(0,0,0,false,false);assert(!gba_speed_rewinding());
  gba_touch_settings.fps=1;touch_prepare();frame(0,0,0,false,false);
  e=frame(0,30,210,true,true);assert(cycles && (e&GBA_TOUCH_STATS_EVENT) && gba_touch_settings.fps==2);
  for(int i=0;i<4;i++)gba_touch_settings.binding[i]=-1;
  for(int fps=0;fps<3;fps++) {
   gba_touch_settings.fps=fps;touch_prepare();frame(0,0,0,false,false);
   assert(frame(0,160,110,true,true)&GBA_TOUCH_MENU_EVENT);
  }
  touch_intro_pending=touch_intro_visible=notice_visible=true;notice_until=0;fake_tick=1000;
  touch_prepare();assert(!frame(0,160,110,true,true) && !intro_writes);
  ui_resume_pending=1;touch_intro_did_paint();assert(!touch_intro_painted && !intro_writes && notice_until==0);
  fake_tick+=5000;touch_intro_did_paint();assert(!touch_intro_painted && !intro_writes && notice_until==0);
  ui_resume_pending=0;touch_intro_did_paint();assert(notice_until==fake_tick+3ULL*SYSCLOCK_ARM11);
  assert(!frame(0,160,110,true,true) && intro_writes==1);
  touch_intro_visible=notice_visible=false;frame(0,160,110,true,false);
  frame(0,0,0,false,false);assert(frame(0,160,110,true,true)&GBA_TOUCH_MENU_EVENT);
  intro_writes=0;
  gba_speed_end();
 }
 puts("PASS: production GBA loop dispatch: Old/New 600 idle frames, physical+touch guest bits, drag-out, Menu, FF, rewind, stats and hidden-button pause");
}
'''
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
 p=Path(tmp);(p/'loop.c').write_text(source)
 # The included real speed implementation uses a path relative to tests/gba_speed.
 (p/'loop.c').write_text(source.replace('#include "../../systems/gba/gba_speed.c"','#include "'+str(r/'systems/gba/gba_speed.c')+'"'))
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-fsanitize=address,undefined','-g','-DUI_PLAY_HOST',*[f'-I{r/d}' for d in ['tests/app/host','app/ui','include/breadbox','platform/3ds','systems/gba','systems/common','core/deps/libretro-common/include']],str(p/'loop.c'),str(r/'tests/play_controls/storage_stub.c'),str(r/'app/ui/ui_play_controls.c'),str(r/'systems/gba/gba_touch.c'),str(r/'systems/common/control_schemas.c'),'-lm','-o',str(p/'loop')],check=True)
 subprocess.run([str(p/'loop')],check=True)

# Exercise the actual retained-target submission branch with duplicate game pictures.
a=gba.index('        bottom_dirty |= __atomic_load_n(&ui_resume_pending')
b=gba.index('        u64 present_cost=',a)
paint=gba[a:b]
a=gba.index('static void message(const char *text)');b=gba.index('static void touch_intro_did_paint',a)
notice_message=gba[a:b]
notice_expire=next(l for l in gba.splitlines() if 'notice_visible && frame_now>=notice_until' in l)
source=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;
static bool bottom_dirty,top_dirty,stereo_frame,notice_visible,touch_intro_visible;
static char notice[120];
static u64 notice_until,fake_tick;
#define SYSCLOCK_ARM11 1000
static u64 svcGetSystemTick(void){return fake_tick;}
static uint32_t ui_resume_pending;
static bool clear_shade;
static unsigned rendered,skipped,top_calls,bottom_calls,submits;
static u64 wait_top_latch(void){return 0;}
static void ui_begin(void){}
static void draw_game_top(void *p){(void)p;top_calls++;}
static void draw_bottom(void *p){(void)p;bottom_calls++;}
static void video_present(bool game,void(*top)(void *),void(*bottom)(void *),void *p){assert(!game);if(clear_shade)ui_resume_pending=0;submits++;if(top)top(p);if(bottom)bottom(p);}
static void video_present_screens(bool game,bool repaint,void(*top)(void *),void(*bottom)(void *),void *p){assert(repaint==(top!=NULL));video_present(game,top,bottom,p);}
'''+notice_message+r'''
static void expire(void){u64 frame_now=fake_tick;
'''+notice_expire+r'''
}
static void present(bool skip_video){u64 latch_wait=0;
'''+paint+r'''
(void)latch_wait;}
int main(void){
 bottom_dirty=true;present(true);assert(bottom_calls==1 && !top_calls && !bottom_dirty);
 for(int f=0;f<600;f++)present(true);
 assert(bottom_calls==1 && submits==1);
 bottom_dirty=true;present(false);assert(top_calls==1 && bottom_calls==2 && !bottom_dirty);
 // A held shade, a released fade and its final clear must all repaint even
 // when the core supplies duplicate frames. Idle suppression resumes afterward.
 for(int skip=0;skip<2;skip++) {
  ui_resume_pending=1;clear_shade=false;unsigned old=bottom_calls;
  for(int f=0;f<10;f++)present(skip);
  assert(bottom_calls==old+10);
  clear_shade=true;present(skip);assert(!ui_resume_pending && bottom_calls==old+11);
  clear_shade=false;present(true);assert(bottom_calls==old+11);
 }
 // Notice appearance and expiry repaint retained top even on a skipped frame.
 for(int expired=0;expired<2;expired++) {
  unsigned old_top=top_calls,old_bottom=bottom_calls;
  if(!expired)message("Saved postcard");
  else {fake_tick=notice_until;expire();}
  assert(top_dirty && bottom_dirty);present(true);
  assert(top_calls==old_top+1 && bottom_calls==old_bottom+1 && !top_dirty);
  present(true);assert(top_calls==old_top+1);
 }
 puts("PASS: production GBA presentation: changed bottom paints on duplicate game frames, 600 idle duplicates submit nothing");
}
'''
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
 p=Path(tmp);(p/'paint.c').write_text(source)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'paint.c'),'-o',str(p/'paint')],check=True)
 subprocess.run([str(p/'paint')],check=True)
