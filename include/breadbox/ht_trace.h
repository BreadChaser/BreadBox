// Optional cold-room trace. Disabled in normal/release builds.
#ifndef HT_TRACE_H
#define HT_TRACE_H
#ifdef HT_TRACE
#include <stddef.h>
#include <stdint.h>
void ht_init(void);
void ht_mark(const char *name, uint32_t value);
uint64_t ht_begin(void);
void ht_slow(uint64_t start, const char *name, size_t size);
void ht_dump(void);
void ht_freeze(void);
#define HT(name) ht_mark(name,0)
#else
#define ht_init() ((void)0)
#define HT(name) ((void)0)
#define ht_mark(name,value) ((void)0)
#define ht_begin() 0
#define ht_slow(start,name,size) ((void)0)
#define ht_dump() ((void)0)
#define ht_freeze() ((void)0)
#endif
#endif
