#!/usr/bin/env python3
from pathlib import Path
import tempfile,subprocess,re
r=Path(__file__).resolve().parents[2]
# A warm cache is usable before the worker finishes validation. Keep new room
# intro/shelf gates from accidentally turning background activity into a wait.
# Scan every shared C file/component. Busy is only background activity:
# functional UI uses must explicitly qualify that no usable snapshot exists.
for path in [*(r/'app').rglob('*'), *(r/'platform/3ds').rglob('*')]:
 if path.suffix not in ('.c','.inc') or path.name=='bb_index.c':continue
 source=path.read_text()
 for line in source.splitlines():
  if 'bb_index_busy(' in line:
   assert '!bb_index_complete()' in line,str(path)+': use snapshot readiness: '+line
 assert not re.search(r'extern.*library_(?:index_busy|poll_index)',source),str(path)
# A publication used to stat/read Continue again (175.7 ms on warm Azahar).
home=(r/'app/ui/home.c').read_text()
publication=home[home.index('if (laid_count != library_count()'):home.index('if (svcGetSystemTick() - l0')]
assert 'cont_probe(' not in publication,'index publication must not re-read Continue on the room thread'
entry=home[home.index('int home_run('):home.index('covers_resume();',home.index('int home_run('))]
assert 'cont_request_refresh();' in entry
assert 'continue_pending &&' not in home
assert 'bb_index_poll(true))shelf_library_changed();' in home[home.index('int home_empty_library_run('):]
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'roms').mkdir();(p/'roms/snes').mkdir()
 (p/'roms/snes/game.sfc').write_bytes(bytes(0x7fc0)+b"\x5a"+bytes(0x3f))
 (p/'test.c').write_text(r'''
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include "bb_index.h"
#include "bb_registry.h"
extern const bb_system_t bb_system_snes;
static const char *const collide[]={"bin",NULL};
static int polls;static bool permit;
static bool poll(bool refresh){polls++;permit=refresh;return true;}
static const bb_shell_ops operations={.poll_library=poll};
static const bb_system_t disc={.abi=BB_SYSTEM_ABI,.id="disc",.extensions=collide,.shell=&operations};
static const bb_system_t cart={.abi=BB_SYSTEM_ABI,.id="cart",.extensions=collide};
const bb_system_t *const bb_systems[]={&bb_system_snes,&disc,&cart};
const unsigned bb_system_count=3;
bool bb_platform_budget(const bb_system_t *s) {(void)s;return true;}
bool bb_platform_claim(const bb_system_t *s) {(void)s;return true;}
void bb_platform_drop(void) {}
bool bb_platform_release_menu(void) {return true;}
void bb_platform_restore_menu(void) {}
int bb_platform_count(const bb_system_t *s) {return bb_index_count(s);}
int main(void) {
 assert(bb_index_poll(true) && permit);assert(bb_index_poll(false) && permit && polls==2);
 bb_index_start("roms",true);
 for(unsigned i=0;i<1000 && bb_index_busy();i++)usleep(10000);
 assert(bb_index_complete() && bb_system_library_count(&bb_system_snes)==1);
 char path[512];bb_entry e;assert(bb_index_get(&bb_system_snes,0,path,sizeof(path),&e));
 assert(bb_system_for_path(path)==&bb_system_snes);
 bb_launch l={.path=path,.entry=&e};
 assert(bb_system_snes.identify(path,&e,&l.id,NULL,0));
 assert(bb_session_begin(&bb_system_snes,&l));
 assert(bb_system_snes.run(&l,NULL,0)==BB_RUN_QUIT);
 bb_session_tick();bb_session_end();
 FILE *f=fopen("roms/unknown.bin","wb");assert(f);fputs("unknown",f);fclose(f);
 assert(!bb_system_for_path("roms/unknown.bin"));
 bb_index_start("roms",true);
 for(unsigned i=0;i<1000 && bb_index_busy();i++)usleep(10000);
 assert(bb_index_complete() && bb_index_status()==BB_INDEX_COMPLETE);
 assert(bb_system_library_count(&bb_system_snes)==1);
 bb_index_shutdown();puts("real index/router/session third adapter PASS");
}
''')
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-DBB_INDEX_TEST','-DBB_INDEX_CACHE="index"','-I'+str(r/'app/ui'),'-I'+str(r/'include/breadbox'),'-I'+str(r/'platform/3ds'),'-I'+str(r/'systems/gba'),str(p/'test.c'),str(r/'tests/system_iface/sys_snes_example.c'),str(r/'app/ui/bb_session.c'),str(r/'app/ui/bb_order.c'),str(r/'app/ui/bb_index.c'),str(r/'app/ui/safefile.c'),'-pthread','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],cwd=p,check=True)

# Settings/held-box menus service publication even without a remote developer
# connection. A running core owns its arena: never publish over that arena.
ui=(r/'app/ui/ui.c').read_text()
body=ui[ui.index('void ui_begin(void)'):ui.index('void ui_text(')]
with tempfile.TemporaryDirectory() as td:
 p=Path(td)
 (p/'test.c').write_text(r'''
#include <assert.h>
#include <stdbool.h>
static int textbuf, polls, imports;
static bool active,installing,committed;
static bool bb_update_installing(void){return installing;}
static bool bb_update_committed(void){return committed;}
static void C2D_TextBufClear(int b){(void)b;}
static void breadbox_import_poll(void){imports++;}
static void bb_index_publish(void){polls++;}
static void *bb_session_system(void){return active?&textbuf:0;}
'''+body+r'''
int main(void){
 for(int screen=0;screen<4;screen++)ui_begin();
 assert(polls==4 && imports==4);
 active=true;ui_begin();assert(polls==4 && imports==5);
 active=false;installing=true;ui_begin();assert(polls==4 && imports==6); /* updater installing: index worker is shut down */
 installing=false;committed=true;ui_begin();assert(polls==4 && imports==7);
 committed=false;ui_begin();assert(polls==5 && imports==8);
}
''')
 subprocess.run(['cc','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
 print('Shared menu publication service / active core exclusion PASS')
