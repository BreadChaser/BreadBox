#!/usr/bin/env python3
"""Run the production optional room-preload pause call site, including abandoned calls."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2]
s=(r/'app/ui/shelf.c').read_text();a=s.index('static int preload_step;');b=s.index('bool shelf_wall_pose(',a)
code=s[a:b]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef uint64_t u64;
#define SYSCLOCK_ARM11 1000000
static bool paused,busy,built;static unsigned generation;
static int nboxes;static struct {int nrows;} view;
static void *boxes;
static unsigned library_key(void){return 2;}
static bool room_pace_return_pending(void){return false;}
static bool restore_boxes(void){return false;}
static void refresh_played(void){}
static bool library_settled(int n){(void)n;return true;}
static bool covers_stop(void){paused=true;return !busy;}
static void covers_resume(void){paused=false;}
static u64 svcGetSystemTick(void){return 1;}
static void ensure_built(void){assert(paused);built=true;generation=2;boxes=&built;}
#define log_printf(...) ((void)0)
#define shelf_scene_prepare() ((void)0)
#define theme_is_room() true
#define theme() 0
#define theme_room(t) false
#define room_hands_preload_step() true
'''
main=r'''
int main(void){
 busy=true;assert(!shelf_preload_step());assert(paused);
 assert(!shelf_preload_step());assert(paused); // the worker sees a pause spanning quiet frames
 shelf_preload_end(true);assert(!paused); // Settings/input abandonment gives it back
 assert(!shelf_preload_step());assert(paused);shelf_preload_end(false);assert(paused); // launch transfers it
 covers_resume();
 busy=false;assert(!shelf_preload_step());assert(!paused && built);
 while(!shelf_preload_step())assert(!paused);
 assert(!paused);puts("Production room preload: pending/abandoned, successful and ready paths give pause back PASS");
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(prefix+code+main)
 subprocess.run(['cc','-std=gnu11','-Wall','-Werror','-Wno-unused-variable','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

# The caller must end optional ownership on input, child menus and every home exit.
home=(r/'app/ui/home.c').read_text()
assert 'else shelf_preload_end(true);' in home
assert 'if(k || screen != DEV_UI_NONE)shelf_preload_end(true);' in home
assert 'shelf_preload_end(false);' in home[home.index('void leave(void)'):home.index('static void launch_leave(void)')]
