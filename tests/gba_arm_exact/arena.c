/* Linux ARM harness counterpart of breadbox_arena.c; no kernel SVC emulation. */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include "bb_arena.h"
extern unsigned char __arena_start[], __arena_statics_end[], __arena_end[];
static bb_arena_t loan;
extern unsigned char ewram[],iwram[],reg[];
bool bb_gba_jit(void **rom,void **ram,size_t rom_size,size_t ram_size)
{
 assert((uintptr_t)ewram>=(uintptr_t)__arena_start && (uintptr_t)ewram<(uintptr_t)__arena_statics_end);
 assert((uintptr_t)iwram>=(uintptr_t)__arena_start && (uintptr_t)iwram<(uintptr_t)__arena_statics_end);
 assert((uintptr_t)reg>=(uintptr_t)__arena_start && (uintptr_t)reg<(uintptr_t)__arena_statics_end);
 assert(bb_arena_claim(&loan,(uintptr_t)__arena_start,
     __arena_end-__arena_start,(uintptr_t)__arena_statics_end));
 *rom=bb_arena_take(&loan,rom_size,4096);
 *ram=bb_arena_take(&loan,ram_size,4096);
 assert(*rom && *ram);
 assert(!mprotect(*rom,rom_size+ram_size,PROT_READ|PROT_WRITE|PROT_EXEC));
 return true;
}

/* Simulate the intervening PS1 owner dirtying every byte, including JIT
 * pages. Claim must clear only adapter statics, not rely on a zero arena. */
void arena_reuse_after_ps1(void)
{
 bb_arena_drop(&loan);
 memset(__arena_start,0xff,__arena_end-__arena_start);
 __builtin___clear_cache((char *)__arena_start,(char *)__arena_end);
 memset(__arena_start,0,__arena_statics_end-__arena_start);
 puts("arena_reuse=PS1-poisoned statics=cleared caches=dirty");
}
