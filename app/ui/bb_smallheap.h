/* Small-block pool (bb_smallheap.c). GPL-2.0-or-later. */
#ifndef BB_SMALLHEAP_H
#define BB_SMALLHEAP_H
typedef struct { unsigned in_use[3], peak[3], overflow; } bb_smallheap_stat;   /* 16/32/64-byte classes */
void bb_smallheap_stats(bb_smallheap_stat *);
#endif
