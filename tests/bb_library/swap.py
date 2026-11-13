#!/usr/bin/env python3
"""Execute the production load loop with the real shared swap controller."""
from pathlib import Path
import os,re,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
def fn(source,name):
 m=re.search(r'(?:PSX3D_COLD )?(?:static )?(?:void|u32|bool|float|library_result) '+name+r'\([^;{}]*?\)\s*\{',source);assert m,name
 a=source.index('{',m.start());i=a+1;n=1
 while n:
  n+=(source[i]=='{')-(source[i]=='}');i+=1
 return source[m.start():i].replace('PSX3D_COLD ','')
u=(root/'app/ui/ui.c').read_text();lib=(root/'systems/ps1/library.inc').read_text()
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "ui_motion.h"
typedef uint32_t u32;typedef uint64_t u64;
@LIBRARY_RESULT@
#define SYSCLOCK_ARM11 1000000ULL
#define KEY_B 2u
#define UI_SWAP_REMOTE (1u<<30)
#define UI_SWAP_MENU 0u
static u64 tick,index_wait_started;
static u64 pause_closed_tick;   // production ui.c state read by ui_resume_sample (its timing log)
#define log_printf(...) ((void)0)
static bool index_wait_slow,index_progress_shown,ps1_loaded,ps1_force;
static u32 ui_resume_pending,swap_keys,swap_buffer,swap_token,swap_buffer_token,swap_buffer_tick,swap_enter_tick;
static int frames,index_frames,adapter_frames,out_frames,game_count,close_at,cancel_at;
static int hz=60,stall_at;
static u64 svcGetSystemTick(void){return tick;}
static u64 osGetTime(void){return tick/1000;}
static bool aptMainLoop(void){assert(frames<180);return close_at<0||frames<close_at;}
static void bb_index_start(const char *r,bool b){(void)r;(void)b;}
static void bb_index_service(void){}
static bool bb_index_complete(void){return frames>=index_frames;}
static bool bb_index_busy(void){return !bb_index_complete();}
static bool library_index_busy(void){return frames<adapter_frames;}
static void dev_tick_keys(void){}
static void input_poll(void){}
static u32 input_keys_down(void){return frames==cancel_at?KEY_B:0;}
static void library_poll_index(void){}
static int library_count(void){return game_count;}
static int bb_system_total(void){return game_count;}
static void ui_begin(void){}
static void draw_index_progress(void *p){(void)p;}
static float ui_resume_sample(u32);
static void video_present_vsync(bool b,void *a,void (*draw)(void *),void *p){
 (void)b;(void)a;draw(p);
 if(ui_resume_pending>>24==UI_MOTION_PHASE_OUT)out_frames++;
 ui_resume_sample(ui_resume_pending);
 tick+=1000000/hz+(frames==stall_at?60000:0);frames++;
}
'''
code=code.replace('@LIBRARY_RESULT@',re.search(r'typedef enum \{[^}]*\} library_result;',(root/'include/breadbox/library.h').read_text()).group(0))   # the production enum, not a copy
code+='#define KEY_A 1u\nenum {SFX_SELECT,SFX_BACK};static void sfx_flush(void){}static void sfx_default(int e){(void)e;}\n'
code+='\n'.join(fn(u,n) for n in ['motion_now','swap_sound','ui_swap_keys','ui_swap_enter','ui_resume_sample'])
code+='\n'+fn(lib,'wait_index')+'\n'+fn(lib,'library_load_result')
code+=r'''
static void reset(bool wait,int scan,int adapter){
 tick=1000000;frames=out_frames=0;index_frames=scan;adapter_frames=adapter;
 swap_keys=swap_buffer=swap_token=swap_buffer_token=swap_buffer_tick=0;
 ui_resume_pending=wait?UI_MOTION_PHASE_WAIT<<24:0;
 close_at=cancel_at=stall_at=-1;game_count=1;
}
int main(void){
 for(hz=30;hz<=60;hz+=30){
 for(int wait=0;wait<2;wait++){
  reset(wait,0,0);assert(library_load_result("games",false)==LIBRARY_LOADED);
  assert(frames==0 && out_frames==0); // instant/warm: no extra OUT or present
  if(wait)assert(ui_resume_pending>>24==UI_MOTION_PHASE_WAIT);
  reset(wait,0,1);assert(library_load_result("games",false)==LIBRARY_LOADED);
  assert(out_frames==0 && frames<60); // menu to menu: completion leaves at once (an incoming WAIT -> IN first), never dims out
  if(!wait)assert(frames==1);
  reset(wait,120,122);assert(library_load_result("games",true)==LIBRARY_LOADED);
  assert(out_frames==0 && frames<160 && index_wait_slow);
  reset(wait,120,122);cancel_at=0;
  assert(library_load_result("games",false)==LIBRARY_CANCELLED && frames<60);
  if(!wait)assert(frames==0 && out_frames==0); // B acts on the frame it arrives
  reset(wait,120,122);close_at=2;
  assert(library_load_result("games",false)==LIBRARY_CANCELLED && frames==2);
 }
 // Completion is buffered early in IN. A late 60ms frame crosses both IN's
 // end and the original token's 150ms expiry; only persistent completion exits.
 reset(true,0,1);stall_at=hz==60?8:4;
 assert(library_load_result("games",false)==LIBRARY_LOADED && frames<60);
 // Old-screen edges must be cleared on entry, including an unfinished OUT.
 reset(false,0,1);swap_keys=KEY_B;swap_buffer=KEY_B;
 ui_resume_pending=(UI_MOTION_PHASE_OUT<<24)|motion_now();
 assert(library_load_result("games",false)==LIBRARY_LOADED && frames<60);
 assert(!swap_keys && !swap_buffer);
 // Once loading has finished, B buffered during IN must not label it cancelled.
 reset(true,0,1);cancel_at=1;
 assert(library_load_result("games",false)==LIBRARY_LOADED && frames<60);
 // The index may finish while wait_index's buffered B is still in flight.
 reset(true,1,1);cancel_at=0;
 assert(library_load_result("games",false)==LIBRARY_LOADED && frames<60);
 // B on the frame the library finishes publishing: loaded (not cancelled), the finished scan is shown
 reset(false,0,3);cancel_at=3;   // the shared scan is done, the PS1 adapter publishes up to that frame
 assert(library_load_result("games",false)==LIBRARY_LOADED && frames==3 && out_frames==0);
 // B while the PS1 adapter still publishes acts at once (no dim gives it time to finish): a cancel
 reset(false,0,5);cancel_at=3;
 assert(library_load_result("games",false)==LIBRARY_CANCELLED && frames==3 && out_frames==0);
 reset(false,200,200);cancel_at=3;   // still scanning: a real cancel, at once
 assert(library_load_result("games",false)==LIBRARY_CANCELLED && frames==3 && out_frames==0);
 reset(false,200,200);cancel_at=3;
 assert(library_load_result("games",false)==LIBRARY_CANCELLED && frames<60);
 reset(true,0,1);cancel_at=1;game_count=0;
 assert(library_load_result("games",false)==LIBRARY_EMPTY && frames<60);
 reset(true,0,0);game_count=0;assert(library_load_result("games",false)==LIBRARY_EMPTY);
 }
 puts("Real library/swap chain PASS: 30/60Hz, jittered IN completion, stale OUT entry, WAIT, idle, instant/warm, slow progress, persistent cancel, close, empty, no menu dim");
}
'''
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
 p=Path(tmp);(p/'check.c').write_text(code)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'app/ui'),'-I'+str(root/'include/breadbox'),'-I'+str(root/'platform/3ds'),str(p/'check.c'),'-lm','-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
