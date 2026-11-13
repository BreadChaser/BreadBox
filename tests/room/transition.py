#!/usr/bin/env python3
"""Execute the actual shared dim compositor/controller with 30/60 Hz clocks."""
from pathlib import Path
import os,re,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'app/ui/ui.c').read_text()
def function(name, s=s):
 m=re.search(r'(?:PSX3D_COLD )?(?:static )?(?:void|u32|bool|float|int) '+name+r'\([^;{}]*?\)\s*\{',s);assert m,name
 a=s.index('{',m.start());i=a+1;n=1
 while n:
  n+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[m.start():i].replace('PSX3D_COLD ','')
for relative,fn in [('app/ui/ui.c','confirm_run'),('app/ui/ui.c','ui_confirm_pick_update'),('app/ui/ui.c','ui_confirm_pick_n'),('app/ui/bb_pause.c','bb_pause_confirm')]:
 assert 'ui_swap_confirm_keys(' in function(fn,(root/relative).read_text()),relative
emu=(root/'systems/ps1/emu.c').read_text()
resume=emu.index('            resume_picture();',emu.index('if (!menu_run(screen))'))
assert emu.index('statsview_begin(game_title);',emu.index('if (!menu_run(screen))'))<resume
assert emu.index('fps_frames = present_count = 0;',emu.index('if (!menu_run(screen))'))<resume
assert 'game_frames < 2 || stats_changed' in emu
assert 'changed = changed || swap_active;' in emu and 'repaint = repaint || swap_active;' in emu
assert 'bool swap_active = __atomic_load_n(&ui_resume_pending,__ATOMIC_ACQUIRE) != 0;' in emu
assert re.search(r'if \(steps \|\| step < 0\)\s*\{\s*pending_keys \|= KEY_B;\s*continue;\s*\}',emu)
assert 'if (!__atomic_load_n(&ui_resume_pending,__ATOMIC_ACQUIRE))' in (root/'systems/gba/gba.c').read_text()
body='\n'.join(function(n) for n in ['motion_now','ui_pause_frame','ui_swap_pause_keys','swap_sound','ui_swap_keys','ui_swap_confirm_keys','ui_swap_enter','ui_swap_hold','ui_resume_sample','ui_pause_motion','ui_pause_open','ui_pause_close'])
# Keep the behavioral normalizer regression tied to the two production callers.
for relative in ['systems/ps1/emu.c','app/ui/bb_pause.c']:
 source=(root/relative).read_text()
 normalized=source.index('k=ui_swap_pause_keys(k);')
 gate=source.index('k=ui_swap_keys(k,swaps);',normalized)
 segment=source[normalized:gate]
 assert 'k|=UI_SWAP_REMOTE' in segment, relative
 assert 'KEY_START' not in segment, relative
 assert not re.search(r'if\s*\(k\s*&\s*KEY_START\)',source[normalized:]), relative
header=(root/'include/breadbox/ui_pause.h').read_text()
remote_define=re.search(r'^#define UI_SWAP_REMOTE[^\n]*',header,re.M).group()
video=(root/'platform/3ds/video.c').read_text()
present=function('video_present_screens',video)
assert present.count('__atomic_load_n(&ui_resume_pending')==1
assert present.count('ui_resume_sample(')==1
postcards=(root/'app/ui/postcards.c').read_text()
body+='\n'+function('pc_animate',postcards)+'\n'+function('pc_tick',postcards)
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "ui_motion.h"
typedef uint32_t u32;typedef uint64_t u64;
#define SYSCLOCK_ARM11 1000000ULL
static u64 tick,pause_open_tick,pause_closed_tick;
#define log_printf(...) ((void)0)
static u32 ui_resume_pending,swap_keys,swap_buffer,swap_token,swap_buffer_token,swap_buffer_tick,swap_enter_tick;
#define UI_SWAP_MENU 0u
#define KEY_START (1u<<3)
static float pause_progress,alpha;
static int draws,game_frames;
static bool alive=true,gpu=true;
static u64 svcGetSystemTick(void){return tick;}
static bool aptMainLoop(void){return alive;}
static bool gspHasGpuRight(void){return gpu;}
static void ui_pause_motion(float);
#define PC_CARDS 13
#define PC_ANIM_NONE 0
#define PC_ANIM_STAMP 1
#define PC_ANIM_TOSS 2
typedef struct {float lift,lift_v;} pc_card;
typedef struct {pc_card card[PC_CARDS];int sel,anim,anim_card;float anim_t,notice_t,t;bool anim_fresh;} pc_view;
static void big_update(pc_view *v){(void)v;}
#define KEY_A (1u<<0)
#define KEY_B (1u<<1)
enum {SFX_MOVE,SFX_SELECT,SFX_BACK,SFX_LAUNCH,SFX_SAVE,SFX_TOSS,SFX_PAUSE_OPEN,SFX_PAUSE_CLOSE};
static void sfx_flush(void){}static void sfx_default(int e){(void)e;}static void sfx_cancel(void){}static void sfx_play(int e){(void)e;}static void sfx_now(int e){(void)e;}static bool sfx_close_said(void){return false;}
static void ui_motion_shade(float a,int width){assert(width==400||width==320);assert(a>=0 && a<1);alpha=a;draws++;}
''' +remote_define+'\n'+body+r'''
static void draw_pair(void){
 float sampled=ui_resume_sample(__atomic_load_n(&ui_resume_pending,__ATOMIC_ACQUIRE));
 ui_motion_shade(sampled,400);float top=alpha;tick+=1500; // eyes/bottom draw later
 ui_motion_shade(sampled,320);assert(fabsf(top-alpha)<.00001f);
}
int main(void){
 for(int hz=30;hz<=60;hz+=30){
  tick=1000000;swap_keys=swap_buffer=ui_resume_pending=0;draws=game_frames=0;
  assert(ui_pause_open());assert(pause_progress==0);
  ui_swap_enter();assert(!ui_resume_pending); // an unmatched destination cannot invent a dim
  ui_swap_hold();draw_pair();assert(fabsf(alpha-UI_MOTION_DIM_ALPHA)<.0001f);
  tick+=1000000;draw_pair();assert(fabsf(alpha-UI_MOTION_DIM_ALPHA)<.0001f);
  ui_swap_enter();assert(!ui_resume_pending); // quit hold cannot invent a later IN
  assert(ui_resume_sample(ui_resume_pending)==0);
  assert(ui_swap_pause_keys(KEY_START|UI_SWAP_REMOTE)==0);
  assert(ui_swap_keys(ui_swap_pause_keys(KEY_START),UI_SWAP_REMOTE|1|2)==0);
  assert(!ui_resume_pending); // actual physical START has no pause action
  assert(ui_swap_pause_keys(KEY_START|1)==1);
  // Only the remote producer can add the private token after normalization.
  assert(ui_swap_keys(ui_swap_pause_keys(KEY_START)|UI_SWAP_REMOTE,UI_SWAP_REMOTE)==0);
  tick+=170000;assert(ui_swap_keys(0,UI_SWAP_REMOTE)==UI_SWAP_REMOTE);
  ui_swap_enter();draw_pair();tick+=170000;draw_pair();assert(!ui_resume_pending);
  for(int i=0;i<hz/3;i++){tick+=1000000/hz;ui_pause_frame();}
  assert(pause_progress==1); // nested loops keep opening live
  u64 start=tick;assert(ui_swap_keys(1,1)==0 && tick==start);
  float last=0;bool buffered=false;u32 action=0;
  while(!action){
   tick+=1000000/hz;draw_pair();assert(alpha>=last);last=alpha;
   action=ui_swap_keys(buffered?0:2,1);buffered=true;
  }
  assert(action==1 && tick-start<200000); // 150 ms out; no blocking helper
  assert((ui_resume_pending>>24)==UI_MOTION_PHASE_WAIT);
  assert(!swap_buffer);ui_swap_enter();assert(!swap_buffer); // OUT taps target the source, including returns without an entry hook
  tick+=2000000; // slow destination preparation must not eat its reveal
  assert(ui_swap_keys(0,0)==0);
  assert(ui_pause_close());draw_pair();assert(fabsf(alpha-UI_MOTION_DIM_ALPHA)<.0001f); // no black first game frame
  draw_pair(); // Resume immediately returns to running gameplay
  start=tick;last=alpha;
  while(ui_resume_pending){
   tick+=1000000/hz;game_frames++;draw_pair();
   if(ui_resume_pending)assert(alpha<=last);
   last=alpha;
  }
  assert(game_frames>0 && tick-start<200000);
  assert(ui_swap_keys(0,0)==0); // no source tap leaks through destination IN
  // A recent same-token tap during IN is delivered once to this destination.
  assert(ui_swap_keys(4,4)==0);tick+=170000;assert(ui_swap_keys(0,4)==4);
  ui_swap_enter();draw_pair();tick+=100000;assert(ui_swap_keys(2,0)==0 && swap_buffer==2);
  tick+=70000;draw_pair();assert(ui_swap_keys(0,0)==2 && !swap_buffer);
  assert(ui_swap_keys(0,0)==0);
  // Explicit token mismatch and expiry reject buffered input even mid-IN.
  assert(ui_swap_keys(4,4)==0);tick+=170000;assert(ui_swap_keys(0,4)==4);
  ui_swap_enter();draw_pair();assert(ui_swap_keys(2,0)==0);
  ++swap_token;assert(ui_swap_keys(0,0)==0 && !swap_buffer);
  assert(ui_swap_keys(2,0)==0);swap_buffer_tick-=UI_MOTION_BUFFER_MS+1;
  assert(ui_swap_keys(0,0)==0 && !swap_buffer);
  tick+=170000;draw_pair();
  // A blind double tap cannot confirm a destructive destination. Neither
  // WAIT nor any part of its IN may queue the confirming edge.
  assert(ui_swap_keys(1,1)==0);tick+=170000;assert(ui_swap_keys(0,1)==1);
  assert(ui_swap_confirm_keys(1,1)==0 && !swap_buffer);
  ui_swap_enter();draw_pair();tick+=50000;
  assert(ui_swap_confirm_keys(1,1)==0 && !swap_buffer);
  tick+=120000;draw_pair();assert(!ui_resume_pending);
  assert(ui_swap_confirm_keys(0,1)==0 && !swap_buffer);
  tick+=200000;
  assert(ui_swap_confirm_keys(1,1)==0); // a new visible press starts OUT
  tick+=170000;assert(ui_swap_confirm_keys(0,1)==1);
  ui_swap_enter();draw_pair();tick+=170000;draw_pair();
  // Menu to menu (UI_SWAP_MENU): the press acts on the frame it arrives, no dim frame is drawn,
  // and no press is held back or replayed on the next screen.
  draws=0;
  for(int f=0;f<hz;f++){
   u32 press=f%3==0?2:0;u64 at=tick;
   assert(ui_swap_keys(press,UI_SWAP_MENU)==press && tick==at);
   if(press)ui_swap_enter(); // the next menu comes up
   assert(!ui_resume_pending && !swap_buffer && !swap_keys);
   draw_pair();assert(alpha==0);
   tick+=1000000/hz;
  }
  assert(draws==2*hz);
  // A double tap: the first press opens a destructive dialog at once; its second tap
  // (100 ms, 250 ms later) never confirms; a new press once it has been up does.
  assert(ui_swap_keys(1,UI_SWAP_MENU)==1);ui_swap_enter();draw_pair();assert(alpha==0);
  tick+=100000;assert(ui_swap_confirm_keys(1,UI_SWAP_MENU)==0);
  tick+=150000;assert(ui_swap_confirm_keys(1,UI_SWAP_MENU)==0);
  tick+=60000;assert(ui_swap_confirm_keys(1,UI_SWAP_MENU)==1);
  assert(!ui_resume_pending);
  // A game launch still dims out and back in; presses during the hand-over reach neither the
  // game nor the next menu (dropped at the destination's entry).
  assert(ui_swap_keys(1,1)==0);draw_pair();
  tick+=60000;draw_pair();assert(alpha>0);
  assert(ui_swap_keys(2,UI_SWAP_MENU)==0); // during OUT: held back
  tick+=100000;assert(ui_swap_keys(0,1)==1);ui_swap_enter(); // the game takes over
  assert(!swap_buffer && ui_swap_keys(0,UI_SWAP_MENU)==0);
  draw_pair();assert(fabsf(alpha-UI_MOTION_DIM_ALPHA)<.0001f); // first game frame still dark, then lifts
  tick+=170000;draw_pair();assert(!ui_resume_pending && alpha==0);
  // Quitting a game hands back the same way: a press made while the game dims out never reaches the room.
  assert(ui_swap_keys(1,1)==0);tick+=80000;assert(ui_swap_keys(4,UI_SWAP_MENU)==0);
  tick+=90000;assert(ui_swap_keys(0,1)==1);ui_swap_enter(); // the room comes up
  for(int f=0;f<hz/2;f++){draw_pair();assert(ui_swap_keys(0,UI_SWAP_MENU)==0);tick+=1000000/hz;}
  assert(!ui_resume_pending && alpha==0);
  // Clock rollover on either phase is defined and does not tear the publication.
  tick=((1ULL<<24)-60)*1000;assert(ui_swap_keys(4,4)==0);
  tick+=170000;draw_pair();assert(ui_swap_keys(0,4)==4);draw_pair();
  tick+=170000;draw_pair();assert(!ui_resume_pending);
  for(int kind=PC_ANIM_STAMP;kind<=PC_ANIM_TOSS;kind++) {
   pc_view v={0};pc_animate(&v,kind,0);pc_tick(&v,2.0f);
   assert(v.anim==kind && v.anim_t==0); // save I/O preceded this new animation
   int frames=0;
   while(v.anim!=PC_ANIM_NONE && frames++<hz)pc_tick(&v,1.0f/hz);
   assert(v.anim==PC_ANIM_NONE && fabsf(frames/(float)hz-UI_MOTION_TURN_S)<2.0f/hz);
  }
  alive=false;assert(!ui_pause_open());alive=true;gpu=false;
  assert(!ui_pause_close());gpu=true;
 }
 puts("Shared dim-through PASS: 30/60 Hz, 150 ms phases, token/expiry-scoped buffered taps, blind confirmation taps rejected, menu swaps undimmed and immediate, game launch/quit still dimmed, physical START ignored, unmatched IN clear, one-sample two screens, first-ready-frame reveal, nonblack resume, atomic clock rollover, lifecycle guards, postcards survive slow save I/O");
}
'''
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
 p=Path(tmp);(p/'check.c').write_text(code)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'app/ui'),'-I'+str(root/'include/breadbox'),'-I'+str(root/'platform/3ds'),str(p/'check.c'),'-lm','-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
