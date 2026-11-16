#!/usr/bin/env python3
"""Execute the production alternation tracker and the mono phase-lock cadence.

Pictures that alternate every frame (Classic NES Series line/column selection,
30 Hz sprite flicker) lock mono to one phase after four alternating checks,
like stereo's 30-pair cadence: the other frames run without video except two
unshown probe frames every 16 frames. Evidence needs three consecutive rendered
frames, so frameskip with slow animation and palette cycles never lock or keep
a lock (both happened on the Old in Metroid Fusion). Static and scrolling
pictures never lock.
"""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2]
s=(r/'systems/gba/gba.c').read_text()
a=s.index('static void gba_track_alternation(const void *data, size_t pitch)\n{');b=s.index('\n}\n',a)+3
c=s.index('        phase_lock=lock_hold');d=s.index('skip_video=true;}\n',c)+len('skip_video=true;}\n')
with tempfile.TemporaryDirectory(prefix='gba-alt-') as td:
 p=Path(td)/'check.c'
 p.write_text(r'''
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint32_t u32;typedef uint16_t u16;typedef uint8_t u8;
static unsigned frames,lock_hold,alternation_streak,probe_count;
static bool probe_frame,phase_lock,ui_wants_3d,skip_video,ff;   /* fast-forward off */
static struct {bool core_frame_blend;} gba_settings;
'''+s[a:b]+r'''
static u16 pic[160*240];
static void make(unsigned scroll,unsigned phase){
 /* Rows 0-39 are a still HUD; the playfield below scrolls. */
 for(unsigned y=0;y<160;y++)for(unsigned x=0;x<240;x++)pic[y*240+x]=(u16)((x+(y<40?0:scroll))*3+y*7);
 /* Classic NES style: every third row comes from another source line. */
 if(phase)for(unsigned y=1;y<160;y+=3)for(unsigned x=0;x<240;x++)pic[y*240+x]^=0x1234;
}
/* One emulated frame: the main-loop decision, then the video callback. */
static unsigned shown,rendered,shown_phase[2];
static void frame(unsigned scroll,int alternating,bool late){
 skip_video=late;
'''+s[c:d]+r'''
 bool video=!skip_video || probe_frame;
 if(video){make(scroll,alternating?frames&1:0);if(!ui_wants_3d)gba_track_alternation(pic,480);rendered++;
  if(!probe_frame){shown++;shown_phase[frames&1]++;}}
 frames++;
}
int main(void){
 /* Static and scrolling pictures without alternation never lock. */
 for(unsigned f=0;f<200;f++)frame(0,0,false);
 for(unsigned f=0;f<200;f++)frame(f,0,false);
 assert(!lock_hold && shown==400);
 /* Alternation locks after four alternating checks (six frames). */
 unsigned start=frames;
 while(!lock_hold)frame(0,1,false);
 assert(frames-start==6);
 /* Locked: only one parity is shown, the other runs without video except
    a probe every 16 frames; late frames (skips) never show the other phase. */
 shown=rendered=shown_phase[0]=shown_phase[1]=0;
 for(unsigned f=0;f<320;f++)frame(0,1,f%7==3);
 assert(shown_phase[1]==0 && shown_phase[0]>130 && rendered-shown>=25 && rendered-shown<=40 && lock_hold==20);
 /* A still HUD over a scrolling playfield keeps the lock. */
 for(unsigned f=0;f<400;f++){frame(f,1,false);}
 assert(lock_hold==20);
 /* Alternation stops: unlock about 20 probes later, then every frame shows. */
 unsigned stop=frames;
 while(lock_hold)frame(0,0,false);
 assert(frames-stop>250 && frames-stop<700);
 /* Locked, then a non-alternating picture whose HUD changes every 10 frames
    (shown frames vs a stale probe would look alternating): still unlocks. */
 while(!lock_hold)frame(0,1,false);
 stop=frames;
 while(lock_hold){make(0,0);for(unsigned x=0;x<240;x++)pic[4*240+x]=(u16)(frames/10);
  skip_video=false;'''+s[c:d]+r'''
  if(!skip_video||probe_frame){gba_track_alternation(pic,480);}
  frames++;assert(frames-stop<700);}
 shown=0;for(unsigned f=0;f<60;f++)frame(0,0,false);
 assert(shown==60);
 /* Heavy game with frameskip (about half the frames drawn) and a row that
    changes every 8 frames (blinking light): never locks. */
 lock_hold=0;alternation_streak=0;
 for(unsigned f=0;f<2000;f++){
  skip_video=(f*7919u>>3)%9<4;
'''+s[c:d]+r'''
  if(!skip_video){make(0,0);for(unsigned x=0;x<240;x++)pic[12*240+x]=(u16)(frames/8);gba_track_alternation(pic,480);}
  frames++;assert(!lock_hold);}
 /* Locked, then a 16-frame palette cycle without alternation: unlocks. */
 while(!lock_hold)frame(0,1,false);
 stop=frames;
 while(lock_hold){skip_video=false;
'''+s[c:d]+r'''
  if(!skip_video||probe_frame){make(0,0);for(unsigned x=0;x<240;x++)pic[12*240+x]=(u16)((frames/8)&1);gba_track_alternation(pic,480);}
  frames++;assert(frames-stop<700);}
 /* Classic NES Excitebike's race (owner 2026-11-12, Old, 2D flicker): the whole
    picture scrolls, and the only steadily alternating row is the status row,
    which also holds the race timer that changes every frame. Whole-row hashes
    never matched two frames back there, so the race never locked. */
 lock_hold=0;ui_wants_3d=false;gba_settings.core_frame_blend=false;
 for(unsigned f=0;f<60;f++)frame(f*2,0,false);
 assert(!lock_hold);
 for(unsigned f=0;f<900;f++){
  if(f==20)shown_phase[0]=shown_phase[1]=0;
  skip_video=f%7==3;
'''+s[c:d]+r'''
  if(!skip_video||probe_frame){
   for(unsigned y=0;y<160;y++)for(unsigned x=0;x<240;x++)pic[y*240+x]=(u16)((x+frames*2)*3+y*7);
   for(unsigned y=136;y<160;y++)for(unsigned x=0;x<240;x++){
    pic[y*240+x]=(u16)(x*5+y*11);
    if(y==148&&(frames&1))pic[y*240+x]^=0x0421;              /* the alternating status row */
    if(x>=208)pic[y*240+x]=(u16)(frames*13+x);}              /* the timer, new every frame */
   gba_track_alternation(pic,480);
   if(!probe_frame)shown_phase[frames&1]++;}
  frames++;
  if(f>=20)assert(lock_hold);}
 /* Locked: only one phase reaches the screen for the whole race. */
 assert(shown_phase[1]==0 && shown_phase[0]>350);
 /* Kirby & The Amazing Mirror's animated game-select menu: one small piece of
    a row matches two frames back three checks in a row, then moves on. That
    is chance, not alternation: it must not lock (it did with pieces and the
    three-check entry). */
 lock_hold=0;
 for(unsigned f=0;f<1200;f++){
  skip_video=false;
'''+s[c:d]+r'''
  if(!skip_video||probe_frame){
   unsigned run=f/6,k=f%6;                                   /* A B A B A, then new art */
   for(unsigned y=0;y<160;y++)for(unsigned x=0;x<240;x++)pic[y*240+x]=(u16)(x*3+y*7);
   for(unsigned x=60;x<120;x++)pic[76*240+x]=(u16)(run*977+(k==5?k*31:(k&1)?13:0)+x);
   gba_track_alternation(pic,480);}
  frames++;
  assert(!lock_hold);}
 /* Stereo (ui_wants_3d) and the core's frame mixing never phase-lock. */
 lock_hold=300;ui_wants_3d=true;shown=0;for(unsigned f=0;f<20;f++)frame(0,0,false);assert(shown==20);
 ui_wants_3d=false;gba_settings.core_frame_blend=true;shown=0;for(unsigned f=0;f<20;f++)frame(0,0,false);assert(shown==20);
 return 0;
}
''')
 subprocess.run(['cc','-O2','-Wall','-Wno-unused-function','-fsanitize=address,undefined',str(p),'-o',str(Path(td)/'check')],check=True)
 subprocess.run([str(Path(td)/'check')],check=True)
print("GBA alternation tracker and mono phase lock (one phase shown, probes, unlock, alternating status row beside a ticking timer): PASS")
