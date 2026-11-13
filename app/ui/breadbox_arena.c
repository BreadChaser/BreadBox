/* Shell-owned executable pool. Physical storage retains its historic symbol
 * and address for the PS1 layout contract; no system owns its kernel state. */
#include "app_policy.h"
#include <string.h>
#include <3ds.h>
#include <stddef.h>
#include "app_log.h"
#include "app_main.h"
#include "bb_arena.h"
#include "bb_system.h"
#include "../../build/systems/arena-size.h"
extern unsigned char ndrc_bss[];
extern void gpsp_ctr_clear_cache(void); /* plain Luma cache SVCs */
static bb_arena_t arena;
static const bb_system_t *owner;
static bool poisoned,ready;
static uintptr_t pool_begin(void) {return (uintptr_t)ndrc_bss&~(uintptr_t)4095;}
static uintptr_t pool_end(void) {return ((uintptr_t)ndrc_bss+BB_EXEC_BYTES+4095)&~(uintptr_t)4095;}
static void trace_flush(void) {log_soon();}   /* written right away by the log's own thread */
/* Re-query after EVERY mutation. Equal permissions do not imply that the
 * kernel merged neighboring blocks (observed on hardware after PS1). */
static bool canonical(void)
{
 Handle process=0;
 Result rc=svcDuplicateHandle(&process,CUR_PROCESS_HANDLE);
 log_printf("arena: canonical duplicate rc=%08lx\n",(unsigned long)(u32)rc);trace_flush();
 if(R_FAILED(rc))return false;
 uintptr_t cursor=pool_begin(),end=pool_end();
 bool ok=true;
 while(cursor<end) {
  MemInfo info={0};PageInfo page={0};
  rc=svcQueryMemory(&info,&page,cursor);
  log_printf("arena: query at=%08lx rc=%08lx base=%08lx size=%lu perm=%lx\n",
   (unsigned long)cursor,(unsigned long)(u32)rc,
   (unsigned long)(R_FAILED(rc)?0:info.base_addr),(unsigned long)(R_FAILED(rc)?0:info.size),
   (unsigned long)(R_FAILED(rc)?0:info.perm));trace_flush();
  if(R_FAILED(rc) || !info.size || info.base_addr>cursor ||
     info.size>UINTPTR_MAX-info.base_addr || info.base_addr+info.size<=cursor) {ok=false;break;}
  uintptr_t stop=info.base_addr+info.size;
  if(stop>end)stop=end;
  if((cursor&4095) || (stop&4095)) {ok=false;break;}
  log_printf("arena: protect begin=%08lx bytes=%lu perm=7\n",(unsigned long)cursor,(unsigned long)(stop-cursor));trace_flush();
  rc=svcControlProcessMemory(process,cursor,0,stop-cursor,MEMOP_PROT,7);
  log_printf("arena: protect rc=%08lx\n",(unsigned long)(u32)rc);trace_flush();
  if(R_FAILED(rc)) {ok=false;break;}
  cursor=stop;
 }
 if(ok) {
  memset(ndrc_bss,0,BB_EXEC_BYTES);
  rc=svcFlushProcessDataCache(process,(uintptr_t)ndrc_bss,BB_EXEC_BYTES);
  log_printf("arena: data cache base=%08lx bytes=%u rc=%08lx\n",(unsigned long)(uintptr_t)ndrc_bss,BB_EXEC_BYTES,(unsigned long)(u32)rc);trace_flush();
  ok=!R_FAILED(rc);
  if(ok) {gpsp_ctr_clear_cache();log_printf("arena: instruction cache complete\n");trace_flush();}
 }
 svcCloseHandle(process);
 return ok;
}
bool bb_exec_claim(const bb_system_t *system)
{
 if(owner || poisoned || !system)return false;
 uintptr_t used=system->arena_end?(uintptr_t)system->arena_end:(uintptr_t)ndrc_bss;
 size_t budget=system->budget[g_new3ds?BB_NEW:BB_OLD].exec_kib;
 if(!budget || budget>BB_EXEC_BYTES/1024 ||
    !bb_arena_claim(&arena,(uintptr_t)ndrc_bss,budget*1024,used))return false;
 log_printf("arena: claim system=%s base=%08lx bytes=%u\n",system->id,(unsigned long)(uintptr_t)ndrc_bss,BB_EXEC_BYTES);trace_flush();
 /* No SVC access (Azahar) supports interpreter sessions only. A real failed
  * transition is latched: never launch another core in an unknown state. */
 ready=__ctr_svchax && canonical();
 if(__ctr_svchax && !ready) {poisoned=true;bb_arena_drop(&arena);return false;}
 if(!__ctr_svchax)memset(ndrc_bss,0,BB_EXEC_BYTES);
 owner=system;
 return true;
}
void bb_exec_release(void)
{
 if(!owner)return;
 ready=false;
 bool ok=!__ctr_svchax || canonical();
 poisoned=poisoned || !ok;
 log_printf("arena: release canonical=%d poisoned=%d\n",ok,poisoned);trace_flush();
 owner=NULL;bb_arena_drop(&arena);
}
bool bb_exec_held(void) {return owner && owner==bb_session_system() && arena.held;}
bool bb_exec_ready(uintptr_t begin,size_t bytes)
{
 return bb_exec_held() && ready && bytes && !(begin&4095) && !(bytes&4095) &&
  begin>=((arena.base+4095)&~(uintptr_t)4095) &&
  begin<(arena.end&~(uintptr_t)4095) && bytes<=(arena.end&~(uintptr_t)4095)-begin;
}
void *bb_exec_buffer(size_t bytes) {return bb_exec_held()?bb_arena_take(&arena,bytes,16):NULL;}
void *bb_exec_arena(size_t *bytes)
{
 if(bytes)*bytes=0;
 if(!bb_exec_held())return NULL;
 uintptr_t begin=(arena.next+4095)&~(uintptr_t)4095,end=arena.end&~(uintptr_t)4095;
 if(end<=begin || !bb_exec_ready(begin,end-begin))return NULL;
 void *p=bb_arena_take(&arena,end-begin,4096);
 if(p && bytes)*bytes=end-begin;
 return p;
}
