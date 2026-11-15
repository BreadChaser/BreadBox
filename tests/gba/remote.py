#!/usr/bin/env python3
"""Run the actual BreadBox remote dispatcher against the existing fake console."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2]
s='#define DEV_CHECK_OWN_REGISTRY\n'+(r/'tests/app/dev/check.c').read_text()
stubs='''
#include "bb_registry.h"
static bool room_diag;
void room_pace_diagnostic(void){room_diag=true;}
bool gba_active(void) {return false;}
bool gba_debug_key(const char *s) {(void)s;return false;}
bool gba_debug_state(int slot,bool save) {(void)slot;(void)save;return true;}
bool gba_remote_resolve(const char *title,char *path,size_t cap) {
 if(strcmp(title,"TMNT"))return false;
 snprintf(path,cap,"sdmc:/roms/TMNT.gba");return true;
}
static const bb_shell_ops gba_shell={.resolve=gba_remote_resolve};
bool emu_debug_state(int slot,bool save);
static const bb_dev_ops test_dev={.state=emu_debug_state};
static const bb_system_t test_ps1={.id="ps1",.dev=&test_dev};
static const bb_system_t test_gba={.id="gba",.shell=&gba_shell,.dev=&test_dev};
const bb_system_t *const bb_systems[]={&test_ps1,&test_gba};
const unsigned bb_system_count=2;
const bb_system_t *bb_session_system(void){return &test_ps1;}
const bb_system_t *bb_system_for_path(const char *path){return strstr(path,".gba")?&test_gba:&test_ps1;}
size_t bb_mem_heap_largest(void){return 1u<<20;}

'''
s=s.replace('#include "../../../app/ui/devcontrol.c"',stubs+'\n#include "../../../app/ui/devcontrol.c"')
test='''
 command("gba-load",false);assert(strstr(response,"false") && !load_pending);
 command("gba-load TMNT state=oops",false);assert(strstr(response,"false") && !load_pending);
 command("gba-load TMNT state=12",false);assert(strstr(response,"false") && !load_pending);
 command("gba-load Missing",false);assert(strstr(response,"false") && !load_pending);
 command("gba-load TMNT state=8 layer3d=3",false);assert(strstr(response,"false") && !load_pending);
 command("load TMNT system=missing state=8",false);assert(strstr(response,"false") && !load_pending);
 command("load TMNT system=gba state=8 stereo=2",false);
 assert(load_pending && bb_remote_stereo==2);
 command("load Crash_Bandicoot state=2",false);assert(bb_remote_slot==-1 && bb_remote_stereo==-1);
 command("quit",false);dev_next_game(path,save,title);dev_local_game();
 command("bench game=Crash_Bandicoot state=2 renderer=gpu warmup=1 seconds=2 shot=0",false);
 assert(dev_next_game(path,save,title) && !strstr(path,"TMNT"));
 command("quit",false);dev_game_ended(true);dev_next_game(path,save,title);dev_local_game();
 command("gba-load TMNT state=8 layer3d=1",false);
 assert(load_pending && bb_remote_slot==8 && bb_remote_stereo==1);
 dev_local_game(); // old game's teardown must retain replacement options
 assert(bb_remote_slot==8 && bb_remote_stereo==1);
 assert(dev_next_game(path,save,title) && !strcmp(path,"sdmc:/roms/TMNT.gba") && !save[0]);
 dev_game_started(title,true);assert(strstr(response,"loaded"));
 dev_system_sample("gba",2.0,120,110,100,12,1,"audio underruns=3 drop=0",60.0,"layer3d=1 display3d=0");
 assert(captured.v[0]==50 && captured.v[41]==3 && captured.v[39]==110);
 assert(strstr(captured.probe,"emulated=120") && strstr(captured.probe,"layer3d=1 display3d=0"));
 command("quit",true);dev_game_ended(true);dev_next_game(path,save,title);dev_local_game();
 assert(bb_remote_slot==-1 && bb_remote_stereo==-1);
'''
s=s.replace('\tdev_shutdown();',test+'\n\tdev_shutdown();')
# Relative source includes resolve from the original directory.
with tempfile.TemporaryDirectory() as d:
 p=Path(d);src=p/'check.c';src.write_text(s.replace('../../../',str(r)+'/'))
 cmd=['cc','-D_GNU_SOURCE','-std=gnu11','-I'+str(r/'tests/app/host/skip'),'-Wall','-Wextra','-Werror','-Wno-format-truncation','-fsanitize=address,undefined','-I'+str(r/'core/deps/libretro-common/include'),'-I'+str(r/'app/ui'),'-I'+str(r/'include/breadbox'),'-I'+str(r/'platform/3ds'),'-I'+str(r/'systems/gba'),str(src),str(r/'systems/ps1/clip.c'),str(r/'systems/ps1/benchstate.c'),str(r/'tools/input-replay/mock-core.c'),'-lz','-o',str(p/'check')]
 subprocess.run(cmd,check=True);subprocess.run([str(p/'check')],cwd=d,check=True)
