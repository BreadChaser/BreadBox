#!/usr/bin/env python3
"""The GBA list the shelf reads (gba_catalog), copied from a growing index worker snapshot."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'systems/gba/gba_library.c').read_text()
s=s[s.index('#define GBA_MAX'):]   # the list the shelf reads (gba_catalog) and its index sync
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include "bb_system.h"
#include "ui_format.h"
const bb_system_t *bb_system_by_id(const char *id) {(void)id;return NULL;}
#define PATH_LEN 512
#define GBA_ID_LEN 32
static void gba_saveid_header(const unsigned char *h,uint32_t crc,char *id) {(void)h;(void)crc;snprintf(id,32,"gba-TEST-12345678");}
static void playlog_adopt_key_later(const char *old,const char *next) {(void)old;(void)next;}
typedef struct {char *title,*cover_key,*group;int ndiscs;char *discs[8];} game_t;
static bool covers_library_try_begin(void) {return true;}
static bool playlog_poll_adoptions(void) {return false;}
int bb_system_cover_id(const bb_system_t *s,int row) {(void)s;return row;}
static void covers_library_begin(void) {}
static void covers_library_end(void) {}
typedef uint32_t u32; typedef uint64_t u64;
static struct { char games_dir[512]; } g_cfg;
static unsigned generation=1;
static int available;
static bool busy=true;
static unsigned bb_index_generation(void) { return generation; }
static int bb_index_gba_count(void) { return available; }
static bool bb_index_busy(void) { return busy; }
enum { BB_INDEX_COMPLETE, BB_INDEX_LIMITED, BB_INDEX_ERROR };
static int scan_status=BB_INDEX_COMPLETE;
static int bb_index_status(void) {return scan_status;}
static bool bb_index_complete(void) { return !busy; }
static void bb_index_start(const char *r,bool refresh) { (void)r;(void)refresh; }
static bool bb_index_get(const bb_system_t *system,int i,char *p,size_t cap,bb_entry *entry) {
 (void)system;unsigned char h[192];
 assert(i>=0 && i<available);snprintf(p,cap,"roms/Game %04d.gba",2000-i);
 memset(h,0,192); memcpy(h+0xac,"TEST",4); snprintf(entry->key,sizeof(entry->key),"gba-TEST-%08x",i);snprintf(entry->cover,sizeof(entry->cover),"TEST");return true;
}
static bool playlog_favourite(const char *key) { (void)key;return true; }
static u32 playlog_seconds(const char *key) { (void)key;return 0; }
static u32 playlog_last(const char *key) { (void)key;return 0; }
#define SYSCLOCK_ARM11 268111856ULL
static u64 svcGetSystemTick(void) {return 1000;}
static u64 osGetTime(void) { return 1000; }
static void log_printf(const char *fmt,...) { (void)fmt; }
'''
# Route the real growing GBA presentation through the shared dispatcher with the
# populated-Library hint (false). Before RC5b this only polled playlog adoptions.
adapter=(root/'systems/gba/sys_gba.c').read_text()
adapter=adapter[adapter.index('static bool gba_library_poll_adapter'):adapter.index('static const bb_shell_ops shell=')]
index=(root/'app/ui/bb_index.c').read_text()
dispatch=index[index.index('static bool publication_pending;'):index.index('bool bb_index_busy(')]
s += "\n" + adapter + r'''
static const bb_shell_ops shell={.poll_library=gba_library_poll_adapter};
static const bb_system_t gba_system={.id="gba",.shell=&shell};
const bb_system_t *const bb_systems[]={&gba_system};
const unsigned bb_system_count=1;
static void bb_index_service(void) {}
''' + dispatch
main=r'''
int main(void) {
 available=100;
 assert(bb_index_poll(false)); assert(count==32 && was_busy);
 bb_catalog_item item;assert(gba_catalog(0,&item));assert(count==32 && copied==32);
 bb_index_poll(false);bb_index_poll(false);bb_index_poll(false); assert(count==100);
 // settled: A to Z (gba_catalog's rows, the shelf's order source)
 busy=false;bb_index_poll(false);
 for(int i=1;i<count;i++)assert(strcasecmp(entries[i-1].title,entries[i].title)<0);
 assert(gba_catalog(0,&item) && !strcmp(item.title,"Game 1901") && !strcmp(item.path,"roms/Game 1901.gba"));
 assert(!gba_catalog(count,&item) && !gba_catalog(-1,&item));
 // more records later: appended, sorted again once settled; nothing copied twice
 available=120;busy=true;bb_index_poll(false);busy=false;bb_index_poll(false);
 assert(count==120 && copied==120 && gba_catalog(0,&item) && !strcmp(item.title,"Game 1881"));
 // A new root/explicit refresh resets once, not on each appended record.
 generation++;available=0;busy=true;bb_index_poll(false);assert(!count && !copied);
 available=1400;
 for(int i=0;i<100;i++) bb_index_poll(false);
 assert(count==GBA_MAX && copied==GBA_MAX);
 busy=false;bb_index_poll(false);assert(count==GBA_MAX);
 // a partial scan (limited) still settles the list it has
 was_busy=true;scan_status=BB_INDEX_LIMITED;bb_index_poll(false);assert(count==GBA_MAX);scan_status=BB_INDEX_COMPLETE;
 char remote_path[PATH_LEN];
 assert(gba_remote_resolve("Game 2000",remote_path,sizeof(remote_path)));
 assert(!strcmp(remote_path,"roms/Game 2000.gba"));
 assert(!gba_remote_resolve("Game",remote_path,sizeof(remote_path)));
 assert(!gba_remote_resolve("Missing",remote_path,sizeof(remote_path)));
 // A game: the list stays (small, its strings on the heap); the index goes and comes back
 // with the same generation (bb_index content()): nothing is copied again, nothing drained.
 int kept=count;
 gba_library_release();assert(entries && count==kept);
 available=0;busy=true;assert(!bb_index_poll(false));assert(count==kept);
 available=1400;busy=false;assert(!bb_index_poll(false));assert(count==kept && copied==kept);
 generation++;available=0;busy=false;bb_index_poll(false);assert(count==0 && copied==0);
 puts("GBA incremental list, A to Z once settled, refresh, limit, remote lookup, kept through a game: PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'check.c').write_text(prefix+s+main)
 subprocess.run(['cc','-I'+str(root/'app/ui'),'-I'+str(root/'include/breadbox'),'-I'+str(root/'platform/3ds'),'-I'+str(root/'systems/gba'),'-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined',str(p/'check.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
with tempfile.TemporaryDirectory() as d:
 p=Path(d)
 subprocess.run(['cc','-I'+str(root/'app/ui'),'-I'+str(root/'include/breadbox'),'-I'+str(root/'platform/3ds'),'-I'+str(root/'systems/gba'),'-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'app/ui'),'-I'+str(root/'include/breadbox'),'-I'+str(root/'platform/3ds'),'-I'+str(root/'systems/gba'),str(root/'tests/gba_library/settings.c'),str(root/'systems/gba/gba_settings.c'),str(root/'systems/gba/gba_saveid.c'),str(root/'systems/gba/gba_cart.c'),'-lminizip','-lz',str(root/'app/ui/safefile.c'),'-o',str(p/'settings')],check=True)
 subprocess.run([str(p/'settings')],check=True)
with tempfile.TemporaryDirectory() as d:   # the same storage with the GBA on-screen buttons built in (gba_touch.h)
 p=Path(d)
 subprocess.run(['cc','-DGBA_TOUCH_BUTTONS=1','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'app/ui'),'-I'+str(root/'include/breadbox'),'-I'+str(root/'platform/3ds'),'-I'+str(root/'systems/gba'),str(root/'tests/gba_library/settings.c'),str(root/'systems/gba/gba_settings.c'),str(root/'systems/gba/gba_saveid.c'),str(root/'systems/gba/gba_cart.c'),'-lminizip','-lz',str(root/'app/ui/safefile.c'),'-o',str(p/'settings')],check=True)
 subprocess.run([str(p/'settings')],check=True)

with tempfile.TemporaryDirectory() as d:
 p=Path(d)
 flags=subprocess.check_output(['pkg-config','--cflags','--libs','freetype2'],text=True).split()
 subprocess.run(['cc','-I'+str(root/'app/ui'),'-I'+str(root/'include/breadbox'),'-I'+str(root/'platform/3ds'),'-I'+str(root/'systems/gba'),'-std=gnu11','-g','-Wall','-Wno-unused-function','-Wno-format-truncation','-Wno-sign-compare','-fsanitize=address,undefined','-ffunction-sections','-Wl,--gc-sections','-I'+str(root/'tests/app/host'),'-I'+str(root/'app/ui'),'-I'+str(root/'include/breadbox'),'-I'+str(root/'platform/3ds'),'-I'+str(root/'systems/gba'),'-I'+str(root/'core/deps/libretro-common/include'),'-I'+str(root/'systems/ps1'),'-DPSX3D_TEST_PRESETS="'+str(root/'assets/romfs/presets')+'"','-DPSX3D_PS1_OVERCLOCK_PRESETS',str(root/'tests/gba_library/scope.c'),str(root/'systems/ps1/clip.c'),str(root/'app/ui/safefile.c'),str(root/'tools/input-replay/mock-core.c'),*flags,'-lz','-lm','-o',str(p/'scope')],check=True)
 subprocess.run([str(p/'scope')],cwd=p,check=True)
