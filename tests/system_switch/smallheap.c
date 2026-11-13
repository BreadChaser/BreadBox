/* The production small-block pool (app/ui/bb_smallheap.c), linked with
 * the same --wrap flags as BreadBox: tiny blocks come from the pool, never the
 * heap; everything else is untouched; contents survive realloc; the pool
 * returning blocks and overflowing to the heap; and many threads at once.
 * GPL-2.0-or-later. */
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <reent.h>
#include "bb_smallheap.h"

/* newlib's malloc lock, here a recursive pthread mutex */
static pthread_mutex_t lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
void __malloc_lock(struct _reent *r) { (void)r; pthread_mutex_lock(&lock); }
void __malloc_unlock(struct _reent *r) { (void)r; pthread_mutex_unlock(&lock); }
void *__real_malloc(size_t);

static uintptr_t lo = UINTPTR_MAX, hi;
static bool pooled(const void *p) { return (uintptr_t)p >= lo && (uintptr_t)p < hi; }
static void learn(void)
{
	/* the pool's extent, from its first and last blocks of each class */
	void *a[1280];
	int n = 0;
	for (size_t size = 16; size <= 64; size *= 2)
		for (int i = 0; i < (size == 64 ? 256 : 512); i++) {
			void *p = malloc(size);
			if ((uintptr_t)p < lo) lo = (uintptr_t)p;
			if ((uintptr_t)p + size > hi) hi = (uintptr_t)p + size;
			a[n++] = p;
		}
	for (int i = 0; i < n; i++) free(a[i]);
	assert(hi - lo == 512 * 16 + 512 * 32 + 256 * 64);
}

static void *worker(void *arg)
{
	unsigned seed = (unsigned)(uintptr_t)arg;
	void *mine[64] = {0};
	for (int i = 0; i < 200000; i++) {
		int k = rand_r(&seed) % 64;
		if (mine[k]) {
			assert(*(unsigned char *)mine[k] == (unsigned char)k);
			free(mine[k]);
			mine[k] = NULL;
		} else {
			size_t n = 1 + rand_r(&seed) % 96;
			mine[k] = malloc(n);
			assert(mine[k]);
			memset(mine[k], k, n);
		}
	}
	for (int k = 0; k < 64; k++) free(mine[k]);
	return NULL;
}

int main(void)
{
	learn();
	bb_smallheap_stat s;
	bb_smallheap_stats(&s);
	assert(!s.in_use[0] && !s.in_use[1] && !s.in_use[2]);
	/* tiny blocks: pool; bigger: heap */
	void *t = malloc(24), *b = malloc(65), *z = malloc(0);
	assert(pooled(t) && !pooled(b) && pooled(z));
	free(z);
	/* calloc zeroes, overflow-checked */
	unsigned char *c = calloc(8, 8);
	assert(pooled(c));
	for (int i = 0; i < 64; i++) assert(!c[i]);
	volatile size_t huge = SIZE_MAX / 2;   /* not a constant: the compiler would refuse it */
	assert(!calloc(huge, 4));
	/* realloc: stays while it fits, moves to the heap with its contents */
	memset(t, 0x5a, 24);
	void *same = realloc(t, 32);
	assert(same == t);
	unsigned char *g = realloc(same, 200);
	assert(g && !pooled(g));
	for (int i = 0; i < 24; i++) assert(g[i] == 0x5a);
	free(g);
	assert(realloc(c, 0) == NULL);
	/* heap blocks stay heap blocks, even when shrunk */
	void *h = realloc(b, 8);
	assert(h && !pooled(h));
	free(h);
	free(NULL);
	/* a full class borrows the next one, then the heap takes over */
	void *a[1300];
	for (int i = 0; i < 1300; i++) a[i] = malloc(16);
	assert(pooled(a[0]) && pooled(a[1279]) && !pooled(a[1299]));
	bb_smallheap_stats(&s);
	assert(s.overflow >= 20);
	for (int i = 0; i < 1300; i++) free(a[i]);
	bb_smallheap_stats(&s);
	assert(!s.in_use[0] && !s.in_use[1] && !s.in_use[2]);
	/* threads */
	pthread_t th[6];
	for (uintptr_t i = 0; i < 6; i++) pthread_create(&th[i], NULL, worker, (void *)(i + 1));
	for (int i = 0; i < 6; i++) pthread_join(th[i], NULL);
	bb_smallheap_stats(&s);
	assert(!s.in_use[0] && !s.in_use[1] && !s.in_use[2]);
	puts("Small-block pool: tiny blocks never in the heap, realloc/calloc/overflow/heap pass-through, 6 threads PASS");
	return 0;
}
