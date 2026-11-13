#!/usr/bin/env python3
"""Execute CTR JIT ownership and fallback code with fault-injected kernel calls.

Fixed aliases are deliberately inaccessible on the host. This exercises the
production setup/stop helpers, option gate and init_main emitter gate; it is
not a test of hardware SVC implementation or CPU instruction generation.
"""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[2]
s = (root/'gba-core/libretro/libretro.c').read_text()
a = s.index('/* CTR executable aliases')
helper = s[a:s.index('\n#endif', a)]
a = s.index('   /* One setup attempt per process')
init = s[a:s.index('#endif\n  #elif defined(VITA)', a)]
a = s.index('   var.key = "gpsp_drc";')
gate = s[a:s.index('   var.key = "gpsp_rtc_time_source";', a)]
t = (root/'gba-core/cpu_threaded.c').read_text()
a = t.index('void flush_dynarec_caches(void)')
flush = t[a:t.index('\n}', a)+2]
s = (root/'gba-core/main.c').read_text()
a = s.index('#ifdef HAVE_DYNAREC', s.index('void init_main'))
reset = s[a:s.index('\n}', a)]
s = (root/'gba-core/libretro/libretro.c').read_text()
a = s.index('void retro_deinit')
deinit = s[a:s.index('static retro_time_t',a)]
assert 'ctr_jit_stop();' in deinit
assert 'svcControlProcessMemory' not in deinit
assert 'MEMOP_UNMAP' not in s and 'MEMOP_PROT' not in s
front = (root/'systems/gba/gba.c').read_text()
a = front.index('void userAppExit(void)')
exit_hook = front[a:front.index('\n}', a)+2]
assert 'userAppExit' not in (root/'app/ui/main.c').read_text()
code = r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
typedef unsigned char u8;
typedef unsigned u32;
static u32 __ctru_heap_size;
static void loader_return(void) {}
static void (*__system_retAddr)(void);
#define _3DS 1
#define HAVE_DYNAREC 1
#define ROM_TRANSLATION_CACHE_SIZE (2*1024*1024)
#define RAM_TRANSLATION_CACHE_SIZE (384*1024)
#define MEMOP_MAP 4
#define RETRO_LOG_INFO 0
#define RETRO_LOG_WARN 1
#define RETRO_LOG_ERROR 2
#define RETRO_ENVIRONMENT_GET_VARIABLE 15
struct retro_variable {const char *key,*value;};
static bool __ctr_svchax=true;
static int translation_caches_inited,dynarec_enable;
static unsigned maps,flushes,allocs,fail_alloc,fail_map;
static unsigned live_maps,live_handles,emits;
static bool fail_handle;
static const char *option="enabled";
static u8 *rom_translation_cache=(void*)0x1e00000,*ram_translation_cache=(void*)0x1da0000;
static u8 *rom_translation_cache_ptr,*ram_translation_cache_ptr;
static u8 *rom_translation_ptr,*ram_translation_ptr,*last_rom_translation_ptr,*last_ram_translation_ptr;
static void logger(int level,const char*fmt,...) {}
static void (*log_cb)(int,const char*,...)=logger;
static void *allocated[2];
static void *memalign(unsigned align,unsigned size) {
 assert(align==4096 && allocs<2);allocs++;
 void *p=NULL;if(allocs!=fail_alloc)assert(!posix_memalign(&p,align,size));
 allocated[allocs-1]=p;return p;
}
static void tracked_free(void *p) {
 if(!p)return;
 for(unsigned i=0;i<2;i++)if(allocated[i]==p) {
  assert(!(live_maps&(1u<<i)));allocated[i]=NULL;free(p);return;
 }
 assert(!"unowned allocation");
}
#define free tracked_free
static int svcDuplicateHandle(uint32_t *h,uint32_t orig) {
 assert(orig==0xffff8001);if(fail_handle)return -1;*h=42;live_handles++;return 0;
}
static int svcControlProcessMemory(uint32_t h,void *dest,void *src,unsigned n,unsigned op,unsigned perm) {
 assert(h==42 && live_handles==1 && src);
 unsigned bit=dest==rom_translation_cache?1:2;
 assert((bit==1 && n==ROM_TRANSLATION_CACHE_SIZE)||(bit==2 && dest==ram_translation_cache && n==RAM_TRANSLATION_CACHE_SIZE));
 assert(src==allocated[bit==1?0:1] && !((uintptr_t)src&4095) && !(n&4095));
 // Any UNMAP or PROT is forbidden, even for failed-content cleanup/app close.
 assert(op==MEMOP_MAP && perm==7);maps++;
 if(maps==fail_map)return (int)0xe0a01bf5;
 assert(!(live_maps&bit));live_maps|=bit;return 0;
}
static void ctr_flush_invalidate_cache(void) {assert(live_maps==3);flushes++;}
static unsigned ewram_code_min,ewram_code_max,iwram_code_min,iwram_code_max;
static void flush_translation_cache_rom(void) {assert(live_maps==3);}
static void flush_translation_cache_ram(void) {assert(live_maps==3);}
static bool environ_cb(unsigned cmd,void *p) {assert(cmd==15);((struct retro_variable*)p)->value=option;return true;}
static void init_dynarec_caches(void) {assert(live_maps==3);}
static bool gamepak_must_swap(void) {return false;}
static void init_emitter(bool swap) {assert(live_maps==3);emits++;}
''' + helper + '\n#define gpsp_ctr_jit_has_mappings ctr_jit_has_mappings\n' + exit_hook + '\n' + flush + '\nstatic void init(void) {\n' + init + '''
}
static void select_engine(void) {
 struct retro_variable var;
#if 1
''' + gate + '''
}
static void reset_cpu(void) {
''' + reset + r'''
}
static void process_exit(void) {
 // Execute production libctru user hook; bulk FREE must never cover aliases.
 __ctru_heap_size=32*1024*1024;__system_retAddr=loader_return;
 userAppExit();
 assert(__ctru_heap_size==32*1024*1024);
 assert(__system_retAddr==loader_return);
 // Model kernel process destruction, NOT production game/app teardown.
 ctr_jit_stop();
 live_maps=0;live_handles=0;
 tracked_free(allocated[0]);tracked_free(allocated[1]);
 rom_translation_cache_ptr=ram_translation_cache_ptr=NULL;
 jit_process_handle=0;jit_mapped=0;jit_attempted=false;
}
static void cold(void) {
 process_exit();maps=flushes=allocs=fail_alloc=fail_map=0;fail_handle=false;
 option="enabled";__ctr_svchax=true;
}
static void fallback(void) {
 unsigned before=emits;
 select_engine();assert(!dynarec_enable);reset_cpu();flush_dynarec_caches();assert(emits==before);
 assert(!rom_translation_ptr && !ram_translation_ptr);
 select_engine();reset_cpu();assert(emits==before);
}
static void failed_session(void) {
 unsigned m=maps,a=allocs;
 fail_alloc=fail_map=0;fail_handle=false;
 for(unsigned i=0;i<1000;i++) {
  ctr_jit_stop();init();fallback();
  assert(maps==m && allocs==a); // no retry or overwrite of partial maps
 }
}
int main(void) {
 cold();
 for(unsigned i=0;i<10000;i++) {
  init();select_engine();assert(dynarec_enable && translation_caches_inited);
  reset_cpu();assert(live_maps==3 && live_handles==1 && maps==2 && allocs==2);
  // No game-specific execution pointers survive quit/error/APT-close cleanup.
  ctr_jit_stop();assert(!translation_caches_inited && !dynarec_enable);
  assert(!rom_translation_ptr && !ram_translation_ptr && !last_rom_translation_ptr && !last_ram_translation_ptr);
  assert(live_maps==3 && rom_translation_cache_ptr==allocated[0] && ram_translation_cache_ptr==allocated[1]);
  ctr_jit_stop(); // idempotent, including app-close after game exit
 }
 __ctr_svchax=false;init();fallback();assert(live_maps==3 && maps==2);
 __ctr_svchax=true;init();select_engine();reset_cpu();assert(dynarec_enable && maps==2);
 option="disabled";select_engine();assert(!dynarec_enable);reset_cpu();
 option="enabled";select_engine();reset_cpu();assert(dynarec_enable);
 for(unsigned i=1;i<=2;i++) {
  cold();fail_alloc=i;init();fallback();failed_session();
  assert(!live_maps && !live_handles && !rom_translation_cache_ptr && !ram_translation_cache_ptr);
 }
 cold();fail_handle=true;init();fallback();failed_session();assert(!live_handles);
 for(unsigned i=1;i<=2;i++) {
  cold();fail_map=i;init();fallback();failed_session();
  assert(live_maps==(i==2?1u:0u) && live_handles==1);
  assert((rom_translation_cache_ptr!=NULL)==(i==2));assert(!ram_translation_cache_ptr);
 }
 cold();__ctr_svchax=false;init();fallback();__ctr_svchax=true;failed_session();assert(!maps && !allocs);
 process_exit();
 puts("CTR JIT: 10000 game exits/reloads, exactly 2 maps; 6000 latched failure reloads; aligned ownership, no unmap/protect/free of aliases; capability/options/reset guards: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='breadbox-jit-') as td:
    source=Path(td)/'jit.c';source.write_text(code)
    exe=Path(td)/'jit'
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-unused-variable','-fsanitize=address,undefined','-g',str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
