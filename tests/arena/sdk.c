/* Production arena + production PS1 mprotect. Per-page kernel state and
 * real Linux/ARM mprotect; no fake claims of emulating ARM11 cache hardware. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#define BB_SYSTEM_H
#define USE_CTRULIB_2
#include "bb_arena.h"
typedef uint32_t u32;
typedef int32_t Result;
typedef uint32_t Handle;
typedef struct { uintptr_t base_addr;size_t size;u32 perm,state; } MemInfo;
typedef struct { u32 flags; } PageInfo;
enum { BB_OLD,BB_NEW };
int g_new3ds;
typedef struct {unsigned exec_kib;} budget_t;
typedef struct bb_system { unsigned char *arena_start,*arena_end; const char *id;budget_t budget[2]; } bb_system_t;
#define CUR_PROCESS_HANDLE 0xffff8001u
#define MEMOP_PROT 6
#define MEMPERM_READ 1
#define MEMPERM_WRITE 2
#define MEMPERM_EXECUTE 4
#define R_FAILED(r) ((r)<0)
int __ctr_svchax=1;
unsigned char ndrc_bss[8519680] __attribute__((aligned(4096)));
static const bb_system_t *session;
static int duplicates,protects,closes,flushes,icaches,maps;
static Result duplicate_rc,protect_rc,query_rc,flush_rc;
static u32 perms[8519680/4096];
static bool boundary[8519680/4096+1];
static bool merge_blocks;
static unsigned cross_rejected;
static void reset_kernel(void) {
 for(size_t i=0;i<sizeof(perms)/sizeof(*perms);i++)perms[i]=3;
 memset(boundary,0,sizeof(boundary));boundary[0]=true;boundary[0x801000/4096]=true;
 boundary[sizeof(perms)/sizeof(*perms)]=true;
}
static const bb_system_t *bb_session_system(void){return session;}
static void log_printf(const char *fmt,...){(void)fmt;}
static void log_hold(bool hold){(void)hold;}
static void log_soon(void){}
void SysPrintf(const char *fmt,...){(void)fmt;}
static Result svcDuplicateHandle(Handle *h,Handle src){assert(src==CUR_PROCESS_HANDLE);duplicates++;*h=42;return duplicate_rc;}
static Result svcControlProcessMemory(Handle h,uintptr_t begin,uintptr_t alias,size_t bytes,u32 op,u32 perm){
 assert(h==42);
 if(op!=MEMOP_PROT){maps++;return -10;} /* reject overlapping/unknown aliases */
 assert(alias==0 && perm<=7 && !(begin&4095) && !(bytes&4095));
 assert(begin>=(uintptr_t)ndrc_bss && begin+bytes<=(uintptr_t)ndrc_bss+sizeof(ndrc_bss));
 protects++;if(protect_rc)return protect_rc;
 size_t first=(begin-(uintptr_t)ndrc_bss)/4096,last=first+bytes/4096;
 for(size_t i=first+1;i<last;i++)if(boundary[i]) {cross_rejected++;return (Result)0xd900060c;}
 assert(bytes);
 assert(!syscall(SYS_mprotect,(void *)begin,bytes,perm));
 boundary[first]=boundary[last]=true;
 for(size_t i=first;i<last;i++)perms[i]=perm;
 if(merge_blocks)for(size_t i=1;i<sizeof(perms)/sizeof(*perms);i++)
  if(perms[i-1]==perms[i])boundary[i]=false;
 return 0;
}
static Result svcQueryMemory(MemInfo *info,PageInfo *page,uintptr_t at){
 (void)page;if(query_rc)return query_rc;
 size_t i=(at-(uintptr_t)ndrc_bss)/4096,first=i,last=i+1;
 assert(i<sizeof(perms)/sizeof(*perms));
 while(first && !boundary[first])first--;
 while(last<sizeof(perms)/sizeof(*perms) && !boundary[last])last++;
 *info=(MemInfo){(uintptr_t)ndrc_bss+first*4096,(last-first)*4096,perms[i],3};return 0;
}
static Result svcFlushProcessDataCache(Handle h,uintptr_t at,size_t n){
 assert(h==42 && at==(uintptr_t)ndrc_bss && n==sizeof(ndrc_bss));
 for(size_t i=0;i<sizeof(ndrc_bss);i++)assert(ndrc_bss[i]==0);
 assert(flushes==icaches);flushes++;return flush_rc;
}
void gpsp_ctr_clear_cache(void){assert(flushes==icaches+1);icaches++;__builtin___clear_cache((char *)ndrc_bss,(char *)ndrc_bss+sizeof(ndrc_bss));}
static Result svcCloseHandle(Handle h){assert(h==42);closes++;return 0;}
/* No svcControlMemory fake: wrong protection API cannot link. */
#include BB_ARENA_FIXTURE_SOURCE
#ifndef LEGACY_HANDOFF
#include "../../systems/ps1/exec.c"
#define svcControlProcessMemory bb_ps1_exec_request
#endif
#define mprotect ps1_mprotect
#include "../../core/frontend/3ds/sys/mman.h"
#undef mprotect
#undef svcControlProcessMemory
static void execute(unsigned char *p,unsigned value){
#ifdef __arm__
 /* mov r0,#value; bx lr, executed out of the actual loan. */
 ((u32 *)p)[0]=0xe3a00000|value;((u32 *)p)[1]=0xe12fff1e;
 __builtin___clear_cache((char *)p,(char *)p+8);
 assert(((unsigned (*)(void))p)()==value);
#else
 (void)p;(void)value;
#endif
}
int main(void){
 reset_kernel();
 bb_system_t gba={.arena_start=ndrc_bss,.arena_end=ndrc_bss+4097,.id="gba",.budget={{8320},{8320}}};
 bb_system_t ps1={.id="ps1",.budget={{8320},{8320}}};
 session=&gba;size_t bytes=99;
 assert(!bb_exec_arena(&bytes) && !bytes);
#ifdef LEGACY_HANDOFF
 assert(bb_exec_claim(&gba));
 /* 1941's full-tail PROT crosses the equal-permission historical PS1 split. */
 assert(bb_exec_arena(&bytes));
#else
 /* The fake really splits, optionally merges, and rejects equal-permission
  * cross-block requests: exercise those rules before testing production. */
 assert(svcControlProcessMemory(42,(uintptr_t)ndrc_bss,0,sizeof(ndrc_bss),6,7)<0);
 assert(cross_rejected==1);
 assert(!svcControlProcessMemory(42,(uintptr_t)ndrc_bss+4096,0,4096,6,7));
 assert(boundary[1] && boundary[2] && perms[1]==7 && perms[0]==3 && perms[2]==3);
 assert(!svcControlProcessMemory(42,(uintptr_t)ndrc_bss+4096,0,4096,6,3));
 assert(boundary[1] && boundary[2]); /* equal permissions can stay split */
 merge_blocks=true;
 assert(!svcControlProcessMemory(42,(uintptr_t)ndrc_bss+4096,0,4096,6,3));
 assert(!boundary[1] && !boundary[2]); /* kernel variant merges */
 for(unsigned merging=0;merging<2;merging++) {
  merge_blocks=merging;reset_kernel();
  for(unsigned cycle=0;cycle<5;cycle++) {
   bool is_gba=!(cycle&1);session=is_gba?&gba:&ps1;
   assert(bb_exec_claim(session));
   for(size_t i=0;i<sizeof(perms)/sizeof(*perms);i++)assert(perms[i]==7);
   for(size_t i=0;i<sizeof(ndrc_bss);i++)assert(ndrc_bss[i]==0);
   assert(!bb_exec_claim(session));
   if(is_gba) {
    void *p=bb_exec_arena(&bytes);assert(p==ndrc_bss+8192 && bytes==sizeof(ndrc_bss)-8192);
    assert(!bb_exec_arena(&bytes));execute(p,37);
   } else {
    int calls=protects;
    assert(!ps1_mprotect(ndrc_bss,0x801000,7));
    assert(!ps1_mprotect(ndrc_bss,0x801000,7)); /* reset/reinit */
    assert(protects==calls); /* adapter never independently protects */
    assert(bb_ps1_exec_request(42,(uintptr_t)ndrc_bss,1,4096,6,7)<0);
    assert(bb_ps1_exec_request(42,(uintptr_t)ndrc_bss,0,4096,6,3)<0);
    execute(ndrc_bss,93);
   }
   memset(ndrc_bss,0xa5,sizeof(ndrc_bss));bb_exec_release();
   for(size_t i=0;i<sizeof(ndrc_bss);i++)assert(ndrc_bss[i]==0);
   for(size_t i=0;i<sizeof(perms)/sizeof(*perms);i++)assert(perms[i]==7);
   assert(!bb_exec_ready((uintptr_t)ndrc_bss,4096));
   /* Another kernel operation can retain an equal-permission boundary. */
   boundary[0x801000/4096]=true;
  }
 }
 assert(!maps && cross_rejected==1 && flushes==20 && icaches==20);
 session=&gba;__ctr_svchax=0;assert(bb_exec_claim(&gba));assert(!bb_exec_arena(&bytes));bb_exec_release();__ctr_svchax=1;
 /* Every failure must close the handle and prevent subsequent launches. */
 for(unsigned fault=0;fault<4;fault++) {
  poisoned=false;
  duplicate_rc=fault==0?-1:0;query_rc=fault==1?-1:0;
  protect_rc=fault==2?-1:0;flush_rc=fault==3?-1:0;
  assert(!bb_exec_claim(&gba) && poisoned && !owner);
  duplicate_rc=query_rc=protect_rc=flush_rc=0;
  assert(!bb_exec_claim(&ps1));
 }
 assert(closes==duplicates-1);
 printf("PASS block splitting/merging, GBA->PS1->GBA->PS1->GBA twice, exact SVC arguments, cold bytes/cache/ownership and failure latching; ARM=%d\n",
#ifdef __arm__
 1
#else
 0
#endif
 );
#endif
 return 0;
}
