/* BreadBox: blocks of 64 bytes or less live in a fixed pool, not in the heap.
 *
 * A system switch frees big blocks (gpSP's cartridge cache, PS1 buffers) and
 * the next system needs big blocks again (a PS1 state is one 4.4 MB block).
 * Tiny blocks made in the meantime - libctru's linear-allocator nodes, file
 * handles, other threads' bookkeeping - were carved from wherever the heap's
 * top happened to be and outlived the session, splitting the free heap in two
 * (measured: 12.4 MB contiguous at boot, 4-7 MB after a GBA session; PS1 state
 * load "Not enough memory"). Here they cannot pin the heap at all.
 *
 * Linked with -Wl,--wrap=malloc,--wrap=free,--wrap=calloc,--wrap=realloc
 * (BreadBox only). Larger requests, memalign and newlib's own internal
 * allocations go to the heap unchanged. Pool exhausted: heap as before.
 * GPL-2.0-or-later. */
#include <reent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "bb_smallheap.h"
#include "ht_trace.h"

void *__real_malloc(size_t);
void __real_free(void *);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __malloc_lock(struct _reent *);
void __malloc_unlock(struct _reent *);

/* Size classes and their counts: 512 x 16 + 512 x 32 + 256 x 64 = 40 KiB.
 * Measured peak over PS1/GBA cycles and menus: 12 / 49 / 12 blocks. */
#define CLASSES 3
static const size_t class_size[CLASSES] = { 16, 32, 64 };
#define N16 512
#define N32 512
#define N64 256
static uint64_t pool[(N16 * 16 + N32 * 32 + N64 * 64) / 8];   /* 8-byte aligned */
static void *free_list[CLASSES];
static size_t class_begin[CLASSES + 1];
static unsigned in_use[CLASSES], peak[CLASSES], overflow;
static bool ready;

static void setup(void)
{
	const unsigned count[CLASSES] = { N16, N32, N64 };
	size_t at = 0;
	for (int c = 0; c < CLASSES; c++) {
		class_begin[c] = at;
		for (unsigned i = 0; i < count[c]; i++) {
			void **block = (void **)((char *)pool + at + (size_t)i * class_size[c]);
			*block = i + 1 < count[c] ? (char *)block + class_size[c] : NULL;
		}
		free_list[c] = (char *)pool + at;
		at += count[c] * class_size[c];
	}
	class_begin[CLASSES] = at;
	ready = true;
}

static int class_of_size(size_t n)
{
	for (int c = 0; c < CLASSES; c++)
		if (n <= class_size[c]) return c;
	return -1;
}

static int class_of_block(const void *p)
{
	uintptr_t off = (uintptr_t)p - (uintptr_t)pool;
	if ((uintptr_t)p < (uintptr_t)pool || off >= class_begin[CLASSES]) return -1;
	for (int c = CLASSES - 1; c >= 0; c--)
		if (off >= class_begin[c]) return c;
	return -1;
}

static void *take(size_t n)
{
	int c = n ? class_of_size(n) : 0;
	if (c < 0) return NULL;
	void *p = NULL;
	__malloc_lock(_REENT);
	if (!ready) setup();
	for (; c < CLASSES && !p; c++)   /* a full class borrows from the next one */
		if (free_list[c]) {
			p = free_list[c];
			free_list[c] = *(void **)p;
			if (++in_use[c] > peak[c]) peak[c] = in_use[c];
		}
	if (!p) overflow++;
	__malloc_unlock(_REENT);
	return p;
}

static void give(void *p, int c)
{
	__malloc_lock(_REENT);
	*(void **)p = free_list[c];
	free_list[c] = p;
	in_use[c]--;
	__malloc_unlock(_REENT);
}

void *__wrap_malloc(size_t n)
{
#ifdef HT_TRACE
 uint64_t t=ht_begin();
#endif
	void *p = take(n);
 p=p?p:__real_malloc(n);
 ht_slow(t,"malloc",n);
 return p;
}

void __wrap_free(void *p)
{
	int c = p ? class_of_block(p) : -1;
	if (c >= 0) give(p, c);
	else __real_free(p);
}

void *__wrap_calloc(size_t a, size_t b)
{
#ifdef HT_TRACE
 uint64_t t=ht_begin();
#endif
	if (b && a > SIZE_MAX / b) return NULL;
	void *p = take(a * b);
	if (!p) {p=__real_calloc(a,b);ht_slow(t,"calloc",a*b);return p;}
	memset(p, 0, a * b);
 ht_slow(t,"calloc",a*b);
	return p;
}

void *__wrap_realloc(void *old, size_t n)
{
	int c = old ? class_of_block(old) : -1;
	if (c < 0) return __real_realloc(old, n);   /* heap blocks stay in the heap */
	if (n && n <= class_size[c]) return old;
	if (!n) { give(old, c); return NULL; }
	void *p = __real_malloc(n);
	if (p) {
		memcpy(p, old, class_size[c]);
		give(old, c);
	}
	return p;
}

void bb_smallheap_stats(bb_smallheap_stat *s)
{
	__malloc_lock(_REENT);
	for (int c = 0; c < CLASSES; c++) {
		s->in_use[c] = in_use[c];
		s->peak[c] = peak[c];
	}
	s->overflow = overflow;
	__malloc_unlock(_REENT);
}
