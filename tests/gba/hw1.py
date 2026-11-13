#!/usr/bin/env python3
"""Normal-play PCM observations and pause accounting use actual adapter functions."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2]
exec((r/'tests/gba/run.py').read_text().split('with tempfile.TemporaryDirectory')[0])
# The shared host now owns the counters; run its real callback suite.
subprocess.run(['bash',str(r/'tests/retro_host/run.sh')],check=True)
print('Normal-play core PCM observation, including unavailable output: PASS')

# Exercise the adapter's actual pause-accounting block with a long user pause.
s=(r/'systems/gba/gba.c').read_text()
a=s.index('            u64 pause_start=',s.index('        if((actions&1)'));b=s.index('        u64 frame_now=',a)
block=s[a:b].replace('            if(quit) break;','            if(quit) return;')
# The closing brace belongs to the caller's input condition.
block=block[:block.rfind('        }')]
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint64_t u64;
#define SYSCLOCK_ARM11 1000
#include "gba_timing.h"
static u64 tick=1000,paused_ticks,report,emu_ticks,present_ticks,max_emu,last_video,latch_ticks;static unsigned latch_waits;
static unsigned report_frames,rendered,held,dev_drawn;
static bool clock_reset;static gba_clock clock;
static int bb_active_controls;
static struct {uint32_t keys[16],turbo_a_key,turbo_b_key;} descriptor;
static unsigned remaps;
static void bb_controls_map(const int *c,uint32_t *keys,uint32_t *a,uint32_t *b){assert(c==&bb_active_controls && keys==descriptor.keys && a==&descriptor.turbo_a_key && b==&descriptor.turbo_b_key);remaps++;}
static u64 svcGetSystemTick(void) {return tick;}
static bool bb_pause_run(void) {tick+=5000;clock_reset=true;return false;}
static unsigned input_keys_held(void) {return 0;}
static void gba_speed_pause(unsigned h) {(void)h;}
static unsigned fps_flushes,touch_prepares;
static bool g_new3ds=true;
static struct {int fps;} gba_touch_settings;
static const char *name="fixture";
static void flush_fps(void) {fps_flushes++;}
static void touch_prepare(void) {touch_prepares++;}
static bool control_side(void) {return false;}
static void save_fps(void) {}
static void statsview_context(int *fps,bool left,void (*save)(void)) {assert(fps==&gba_touch_settings.fps && !left && save==save_fps);}
static void statsview_begin(const char *title) {assert(title==name);}
static bool statsview_redraw(int osd) {assert(!osd);return true;}

static void pause_input(void) {
'''+block+r'''
}
int main(void) {
 gba_clock_reset(&clock,tick,SYSCLOCK_ARM11);report_frames=60;
 pause_input();assert(paused_ticks==5000 && report==6000 && !report_frames && last_video==6000);
 assert(!clock_reset && clock.next==6000);
 // A second pause adds its duration once, even with a HOME restore inside.
 pause_input();assert(paused_ticks==10000 && clock.next==11000);
 assert((tick-1000)-paused_ticks==0 && remaps==2 && fps_flushes==2 && touch_prepares==2);
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'check.c').write_text(code)
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-I'+str(r/'app/ui'),'-I'+str(r/'include/breadbox'),'-I'+str(r/'platform/3ds'),'-I'+str(r/'systems/gba'),'-fsanitize=address,undefined',str(p/'check.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
print('Normal pause + HOME restore: exclude paused time once, restart reporting/pacing: PASS')
