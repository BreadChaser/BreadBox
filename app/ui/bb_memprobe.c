/* Memory at system switches: what one system leaves for the next.
 * Walks newlib's heap chunks (no allocation) and finds the largest linear
 * block, so a fragmented heap shows up in the log as a small largest block
 * next to a large free total. Diagnostic builds (EXTRA_DEFS=-DBB_MEM_TRACE)
 * also log a map of big regions and the small blocks between them.
 * GPL-2.0-or-later. */
#include <malloc.h>
#include <reent.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <3ds.h>
#include <stddef.h>
#include "app_log.h"
#include "bb_memprobe.h"
#include "bb_smallheap.h"

extern char *__malloc_sbrk_base;     /* newlib mallocr: first byte it got from sbrk */
extern void *__malloc_av_[];         /* bins; __malloc_av_[2] is the top chunk */
extern char *fake_heap_end;
void __malloc_lock(struct _reent *);
void __malloc_unlock(struct _reent *);

typedef struct { size_t prev, size; } chunk;
#define LIVE_MIN (64 * 1024)
#define LIVE_LOG 8

/* Under the malloc lock: other threads may allocate meanwhile. live (may be
 * NULL) lists live blocks of LIVE_MIN or more; map (trace builds) the regions. */
static void walk(bb_mem *m, char *live, size_t live_cap, char *map, size_t map_cap)
{
	int nlive = 0;
	bool after_big_free = false;
	__malloc_lock(_REENT);
	char *top = __malloc_av_[2], *brk = _sbrk_r(_REENT, 0);
	if (__malloc_sbrk_base != (char *)-1 && top) {
		char *p = (char *)(((uintptr_t)__malloc_sbrk_base + 7) & ~(uintptr_t)7);
		while (p < top) {
			size_t size = ((chunk *)p)->size & ~(size_t)3;
			if (size < 16 || (size & 7) || p + size > top) {
				m->broken = true;
				break;
			}
			bool inuse = ((chunk *)(p + size))->size & 1;
			if (map) {
				size_t ml = strlen(map);
				if (!inuse && size >= LIVE_MIN) {
					snprintf(map + ml, map_cap - ml, " F%lx:%uK", (unsigned long)(uintptr_t)p, (unsigned)(size / 1024));
					after_big_free = true;
				} else if (inuse && size >= LIVE_MIN) {
					snprintf(map + ml, map_cap - ml, " U%lx:%uK", (unsigned long)(uintptr_t)p, (unsigned)(size / 1024));
					after_big_free = false;
				} else if (inuse && after_big_free)
					snprintf(map + ml, map_cap - ml, " p%lx:%u", (unsigned long)(uintptr_t)p, (unsigned)size);
			}
			if (!inuse) {
				m->heap_free += size;
				if (size > m->heap_largest_hole) m->heap_largest_hole = size;
			} else {
				m->heap_used += size;
				if (live && size >= LIVE_MIN && nlive < LIVE_LOG) {
					size_t n = strlen(live);
					snprintf(live + n, live_cap - n, " %lx:%uK", (unsigned long)(uintptr_t)p, (unsigned)(size / 1024));
					nlive++;
				}
			}
			p += size;
		}
		m->heap_top = (((chunk *)top)->size & ~(size_t)3) + (size_t)(fake_heap_end - brk);
	} else
		m->heap_top = (size_t)(fake_heap_end - brk);
	__malloc_unlock(_REENT);
	m->heap_free += m->heap_top;
	m->heap_largest = m->heap_top > m->heap_largest_hole ? m->heap_top : m->heap_largest_hole;
}

size_t bb_mem_heap_largest(void)
{
	bb_mem m = {0};
	walk(&m, NULL, 0, NULL, 0);
	return m.heap_largest;
}

void bb_mem_probe(const char *where, bb_mem *out)
{
	bb_mem m = {0};
	char live[LIVE_LOG * 24] = "";
#ifdef BB_MEM_TRACE
	static char map[1600];
	map[0] = 0;
	walk(&m, live, sizeof(live), map, sizeof(map));
	log_printf("memmap: %s%s\n", where, map);
#else
	walk(&m, live, sizeof(live), NULL, 0);
#endif
	m.linear_free = linearSpaceFree();
	// the largest linear block: binary search over what the allocator gives (64 KiB steps)
	size_t lo = 0, hi = m.linear_free / 65536;
	while (lo < hi) {
		size_t mid = (lo + hi + 1) / 2;
		void *p = linearAlloc(mid * 65536);
		if (p) {
			linearFree(p);
			lo = mid;
		} else
			hi = mid - 1;
	}
	m.linear_largest = lo * 65536;
	bb_smallheap_stat sh;
	bb_smallheap_stats(&sh);
	log_printf("mem: %s heap used=%uK free=%uK largest=%uK (hole %uK, top %uK)%s linear free=%uK largest=%uK;"
		" small pool %u/%u/%u peak %u/%u/%u to heap %u; live>=64K:%s\n",
		where, (unsigned)(m.heap_used / 1024), (unsigned)(m.heap_free / 1024), (unsigned)(m.heap_largest / 1024),
		(unsigned)(m.heap_largest_hole / 1024), (unsigned)(m.heap_top / 1024), m.broken ? " WALK-STOPPED" : "",
		(unsigned)(m.linear_free / 1024), (unsigned)(m.linear_largest / 1024), sh.in_use[0], sh.in_use[1], sh.in_use[2],
		sh.peak[0], sh.peak[1], sh.peak[2], sh.overflow, live[0] ? live : " none");
	if (out)
		*out = m;
}
