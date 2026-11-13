#ifndef BB_ARENA_H
#define BB_ARENA_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Newlib's fordblks excludes the heap above its current program break. */
static inline size_t bb_heap_bytes(size_t free_chunks, uintptr_t brk, uintptr_t end)
{
 size_t unused=brk!=UINTPTR_MAX && brk<end?end-brk:0;
 return unused>SIZE_MAX-free_chunks?SIZE_MAX:free_chunks+unused;
}
/* Pure geometry/ownership helpers are shared by the host tests. */
typedef struct { uintptr_t base, end, next; bool held; } bb_arena_t;
static inline bool bb_arena_claim(bb_arena_t *a, uintptr_t base, size_t size, uintptr_t used_end)
{
 if(a->held || used_end<base || used_end-base>size || base+size<base)return false;
 a->base=base;a->end=base+size;a->next=used_end;a->held=true;return true;
}
static inline void *bb_arena_take(bb_arena_t *a,size_t bytes,size_t align)
{
 if(!a->held || !align || (align&(align-1)) || a->next>UINTPTR_MAX-(align-1))return NULL;
 uintptr_t p=(a->next+align-1)&~(uintptr_t)(align-1);
 if(p>a->end || bytes>a->end-p)return NULL;
 a->next=p+bytes;return (void *)p;
}
static inline void bb_arena_drop(bb_arena_t *a) { *a=(bb_arena_t){0}; }
struct bb_system;
bool bb_exec_claim(const struct bb_system *system);
void bb_exec_release(void);
bool bb_exec_held(void);
void *bb_exec_buffer(size_t bytes);
bool bb_exec_ready(uintptr_t begin, size_t bytes);
#endif
