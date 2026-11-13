#include <assert.h>
#include <stdio.h>
#include "bb_arena.h"
int main(void) {
 assert(bb_heap_bytes(100,1000,3000)==2100);
 assert(bb_heap_bytes(100,3000,3000)==100);
 assert(bb_heap_bytes(100,4000,3000)==100);
 assert(bb_heap_bytes(100,UINTPTR_MAX,3000)==100);
 assert(bb_heap_bytes(SIZE_MAX,1000,3000)==SIZE_MAX);
 bb_arena_t a={0};
 assert(!bb_arena_take(&a,1,1));
 for(unsigned i=0;i<100;i++) {
  assert(bb_arena_claim(&a,0x100004,0x820000,0x290123));
  assert(!bb_arena_claim(&a,0x100004,0x820000,0x290123));
  assert(!bb_arena_take(&a,4,3));
  void *rom=bb_arena_take(&a,0x200000,4096);
  void *ram=bb_arena_take(&a,0x60000,4096);
  assert((uintptr_t)rom==0x291000 && (uintptr_t)ram==0x491000);
  uintptr_t before=a.next;
  assert(!bb_arena_take(&a,SIZE_MAX,4096) && a.next==before);
  assert(!bb_arena_take(&a,0x820000,4096));
  bb_arena_drop(&a);
 }
 assert(!bb_arena_claim(&a,UINTPTR_MAX-1024,4096,UINTPTR_MAX));
 assert(!bb_arena_claim(&a,4096,4096,9000));
 puts("arena ownership, alignment, bounds, overflow and 100 handoffs PASS");
}
