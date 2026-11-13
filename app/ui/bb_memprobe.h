/* Memory left at system switches (bb_memprobe.c). GPL-2.0-or-later. */
#ifndef BB_MEMPROBE_H
#define BB_MEMPROBE_H
#include <stdbool.h>
#include <stddef.h>
typedef struct {
	size_t heap_used, heap_free, heap_largest, heap_largest_hole, heap_top;
	size_t linear_free, linear_largest;
	bool broken;   /* the heap walk met a chunk it does not understand */
} bb_mem;
/* Logs "mem: <where> ..."; out may be NULL. No heap allocation. */
void bb_mem_probe(const char *where, bb_mem *out);
/* The largest heap block malloc could return now (status; any thread). */
size_t bb_mem_heap_largest(void);
#endif
