#include "ht_trace.h"
#ifdef HT_TRACE
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "app_log.h"
#undef fopen
#undef fwrite
#undef stat
#undef linearAlloc
#undef vramAlloc
void *linearAlloc(size_t);
void *vramAlloc(size_t);
#define HT_COUNT 1024
typedef struct { uint32_t tick, thread, value, sequence; const char *name; } ht_event;
static ht_event ring[HT_COUNT];
static unsigned cursor;
static u32 main_id;
static bool enabled;
void ht_init(void) { svcGetThreadId(&main_id,CUR_THREAD_HANDLE); __atomic_store_n(&enabled,true,__ATOMIC_RELEASE); }
void ht_mark(const char *name, uint32_t value)
{
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return;
 u32 thread=0;svcGetThreadId(&thread,CUR_THREAD_HANDLE);
 unsigned n=__atomic_fetch_add(&cursor,1,__ATOMIC_RELAXED);
 unsigned i=n%HT_COUNT;
 __atomic_store_n(&ring[i].sequence,0,__ATOMIC_SEQ_CST);
 __atomic_store_n(&ring[i].tick,(u32)svcGetSystemTick(),__ATOMIC_RELAXED);
 __atomic_store_n(&ring[i].thread,thread,__ATOMIC_RELAXED);
 __atomic_store_n(&ring[i].name,name,__ATOMIC_RELAXED);
 __atomic_store_n(&ring[i].value,value,__ATOMIC_RELAXED);
 __atomic_store_n(&ring[i].sequence,n+1,__ATOMIC_RELEASE);
}
// A writer already in flight at freeze may finish later. Ticket validation and
// atomic fields let readers skip that slot without waiting on a worker.
static bool ht_read(unsigned n, ht_event *out)
{
 unsigned i=n%HT_COUNT;
 if(__atomic_load_n(&ring[i].sequence,__ATOMIC_ACQUIRE)!=n+1)return false;
 out->tick=__atomic_load_n(&ring[i].tick,__ATOMIC_RELAXED);
 out->thread=__atomic_load_n(&ring[i].thread,__ATOMIC_RELAXED);
 out->name=__atomic_load_n(&ring[i].name,__ATOMIC_RELAXED);
 out->value=__atomic_load_n(&ring[i].value,__ATOMIC_RELAXED);
 return __atomic_load_n(&ring[i].sequence,__ATOMIC_ACQUIRE)==n+1;
}
uint64_t ht_begin(void)
{
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return 0;
 u32 id=0;svcGetThreadId(&id,CUR_THREAD_HANDLE);
 return id==main_id?svcGetSystemTick():0;
}
void ht_slow(uint64_t start,const char *name,size_t size)
{
 if(start && svcGetSystemTick()-start>SYSCLOCK_ARM11/2000) {
  ht_mark(name,(u32)size);
  ht_mark("call ticks",(u32)(svcGetSystemTick()-start));
 }
}
void ht_freeze(void)
{
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return;
 __atomic_store_n(&enabled,false,__ATOMIC_RELEASE);
 unsigned end=__atomic_load_n(&cursor,__ATOMIC_ACQUIRE),start=end>HT_COUNT?end-HT_COUNT:0;
 struct {u32 ticks;const char *from,*to;} top[3]={{0}};
 u32 previous=0;const char *from=NULL;
 for(unsigned n=start;n<end;n++) {
  ht_event e;
  if(!ht_read(n,&e) || e.thread!=main_id)continue;
  u32 delta=e.tick-previous;
  if(from)for(unsigned k=0;k<3;k++)if(delta>top[k].ticks){
   for(unsigned j=2;j>k;j--)top[j]=top[j-1];
   top[k].ticks=delta;top[k].from=from;top[k].to=e.name;break;
  }
  previous=e.tick;from=e.name;
 }
 for(unsigned i=0;i<3;i++)if(top[i].from)log_printf("HT longest %s -> %s %.3f ms\n",top[i].from,top[i].to,(double)top[i].ticks*1000/SYSCLOCK_ARM11);
}
void ht_dump(void)
{
 // Pause writers while snapshotting; no per-frame formatting or SD writes.
 __atomic_store_n(&enabled,false,__ATOMIC_RELEASE);
 unsigned end=__atomic_load_n(&cursor,__ATOMIC_ACQUIRE),start=end>HT_COUNT?end-HT_COUNT:0;
 log_printf("HT begin %u..%u main %lu\n",start,end,(unsigned long)main_id);
 for(unsigned n=start;n<end;n++) {
  ht_event e;
  if(!ht_read(n,&e))continue;
  log_printf("HT %u %lu t%lu %s %lu\n",n,(unsigned long)e.tick,(unsigned long)e.thread,e.name,(unsigned long)e.value);
 }
 log_printf("HT end\n");__atomic_store_n(&enabled,true,__ATOMIC_RELEASE);
}
size_t __real_fread(void *,size_t,size_t,FILE *);
size_t ht_fread(void *p,size_t size,size_t n,FILE *f) {
 u64 t=ht_begin();size_t result=__real_fread(p,size,n,f);ht_slow(t,"fread",size*n);return result;
}
static u32 path_hash(const char *p) {u32 h=2166136261u;while(*p){h^=(u8)*p++;h*=16777619u;}return h;}
FILE *ht_fopen(const char *p,const char *m) {
 u64 t=ht_begin();FILE *f=fopen(p,m);ht_slow(t,"fopen",0);
 if(t && svcGetSystemTick()-t>SYSCLOCK_ARM11/2000)ht_mark("fopen path hash",path_hash(p));return f;
}
size_t ht_fwrite(const void *p,size_t s,size_t n,FILE *f) {
 u64 t=ht_begin();size_t r=fwrite(p,s,n,f);ht_slow(t,"fwrite",s*n);return r;
}
int ht_stat(const char *p,struct stat *s) {
 u64 t=ht_begin();int r=stat(p,s);ht_slow(t,"stat",0);if(t && svcGetSystemTick()-t>SYSCLOCK_ARM11/2000)ht_mark("stat path hash",path_hash(p));return r;
}
void *ht_linearAlloc(size_t n) {
 u64 t=ht_begin();void *p=linearAlloc(n);ht_slow(t,"linearAlloc",n);return p;
}
void *ht_vramAlloc(size_t n) {
 u64 t=ht_begin();void *p=vramAlloc(n);ht_slow(t,"vramAlloc",n);return p;
}
#endif
