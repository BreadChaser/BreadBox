#!/usr/bin/env python3
"""Execute the mono present latch against a model of libctru/GSP double buffering.

libctru flips its back-buffer index at every swap; GSP shows a presented buffer
only at the next VBlank. A second swap inside one refresh therefore draws into
the buffer on screen (tear) and replaces the unshown picture (drop). The model
paces GBA frames (59.73 Hz) with jittered emulation cost against a 59.83 Hz
LCD, as the Old 3DS does in mono, with and without the real wait_top_latch.
"""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2]
s=(r/'systems/gba/gba.c').read_text()
a=s.index('/* libctru flips its back-buffer index');b=s.index('static u64 host_ticks',a)
c=s.index('static u64 wait_top_latch(void)\n{');e=s.index('\n}\n',c)+3
with tempfile.TemporaryDirectory(prefix='gba-latch-') as td:
 p=Path(td)/'check.c'
 p.write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef uint64_t u64;typedef uint32_t u32;
#define SYSCLOCK_ARM11 268111856ULL
#define GSP_SCREEN_TOP 0
static int gba_remote_latch=1;
static u64 now;            /* simulated ARM11 ticks */
static const u64 vblank=SYSCLOCK_ARM11*1000/59831;  /* LCD refresh */
static u64 next_vblank;
static int cur,shown,pending=-1;   /* libctru index, LCD buffer, GSP request */
static unsigned drops,tears,latched,stuck;
static void gsp_advance(u64 t)
{
 while(next_vblank<=t){if(pending>=0 && !stuck){shown=pending;pending=-1;latched++;}next_vblank+=vblank;}
 now=t;
}
static bool gspIsPresentPending(unsigned screen){(void)screen;return pending>=0;}
static u64 svcGetSystemTick(void){return now;}
static void svcSleepThread(long long ns){gsp_advance(now+(u64)ns*SYSCLOCK_ARM11/1000000000ULL);}
'''+s[a:b]+s[c:e]+r'''
/* video_present: transfer into libctru's back buffer, swap when the GPU ends. */
static void present(u64 gpu)
{
 gsp_advance(now+gpu);
 int back=cur^1;
 if(back==shown)tears++;
 if(pending>=0)drops++;
 cur=back;pending=cur;
}
static unsigned lcg=12345;
static unsigned rnd(unsigned n){lcg=lcg*1103515245u+12345u;return (lcg>>16)%n;}
static void run(int latch,unsigned frames,unsigned *d,unsigned *t,u64 *waited)
{
 gba_remote_latch=latch;now=0;next_vblank=vblank/3;cur=shown=0;pending=-1;drops=tears=latched=0;
 latch_waits=0;latch_ticks=0;lcg=12345;
 const u64 frame=SYSCLOCK_ARM11*280896/16777216;
 u64 next=0;
 for(unsigned f=0;f<frames;f++){
  if(now<next)gsp_advance(next);
  /* Classic NES-like mono cost: 4..14 ms emulation, ~1 ms GPU. */
  gsp_advance(now+SYSCLOCK_ARM11*(4000+rnd(10000))/1000000);
  wait_top_latch();
  present(SYSCLOCK_ARM11/1000);
  next+=frame;
 }
 *d=drops;*t=tears;*waited=latch_ticks;
}
int main(void){
 /* Unit: nothing pending costs nothing and counts nothing. */
 gba_remote_latch=1;now=0;next_vblank=vblank;pending=-1;latch_waits=0;
 assert(wait_top_latch()==0 && latch_waits==0);
 /* Pending: waits exactly until the next VBlank latch (0.1 ms polling). */
 now=vblank/2;pending=1;u64 w=wait_top_latch();
 assert(pending<0 && latch_waits==1 && now>=vblank && now<vblank+SYSCLOCK_ARM11/5000 && w==latch_ticks);
 /* A latch that never comes (screen off) is capped at 20 ms. */
 stuck=1;pending=1;latch_ticks=0;u64 start=now;w=wait_top_latch();stuck=0;
 assert(w>=SYSCLOCK_ARM11/50 && w<SYSCLOCK_ARM11/50+SYSCLOCK_ARM11/5000 && now-start==w);
 /* latch=0 (development A/B): counts the would-be loss, never waits. */
 gba_remote_latch=0;pending=1;latch_waits=0;start=now;
 assert(wait_top_latch()==0 && latch_waits==1 && now==start);pending=-1;
 /* Gameplay model: 60 s of mono pictures. */
 unsigned d0,t0,d1,t1;u64 w0,w1;
 run(0,3584,&d0,&t0,&w0);run(1,3584,&d1,&t1,&w1);
 printf("unsynchronized: drops=%u tears=%u\nlatched: drops=%u tears=%u wait=%.1f ms/60 s\n",
  d0,t0,d1,t1,w1*1000.0/SYSCLOCK_ARM11);
 assert(d0>50 && t0>50);   /* the defect is reproduced by the model */
 assert(d1==0 && t1==0);   /* every presented picture reaches the LCD intact */
 return 0;
}
''')
 subprocess.run(['cc','-O2','-Wall','-Wno-unused-function','-fsanitize=address,undefined',str(p),'-o',str(Path(td)/'check')],check=True)
 out=subprocess.run([str(Path(td)/'check')],check=True,capture_output=True,text=True).stdout
 print(out,end='')
print('GBA mono present waits for the VBlank latch: no drop, no tear in the double-buffer model: PASS')
