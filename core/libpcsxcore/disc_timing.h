/* Optional disc profiling. No hooks or storage in ordinary builds. */
#ifndef DISC_TIMING_H
#define DISC_TIMING_H
#ifdef BB_DISC_TIMING
#include <3ds/svc.h>
/* Each bank has one writer. The main thread samples aligned 32-bit totals;
 * it never clears a running reader's counters. Tick deltas wrap modulo 2^32.
 * Reader selection is protected by the existing disc read lock. */
struct cdt_read_counts {
    unsigned reads, ticks, maximum, misses, wait_ticks, call_max; /* initial cache misses, including wait hits */
    unsigned hunks, hunk_ticks, hunk_max;
} __attribute__((aligned(64)));
extern int cdt_enabled;
extern unsigned cdt_reader;
extern volatile struct cdt_read_counts cdt_reads[2];
static inline unsigned cdt_begin(unsigned reader)
{
    if (!cdt_enabled) return 0;
    cdt_reader = reader;
    return (unsigned)svcGetSystemTick();
}
static inline void cdt_end(unsigned reader, unsigned start)
{
    if (!cdt_enabled) return;
    unsigned elapsed = (unsigned)svcGetSystemTick() - start;
    volatile struct cdt_read_counts *c = &cdt_reads[reader];
    c->ticks += elapsed;
    if (elapsed > c->maximum) c->maximum = elapsed;
    c->reads++;
}
#define CDT_HUNK(call) do { \
    if (cdt_enabled) { \
        unsigned start_ = (unsigned)svcGetSystemTick(); \
        call; \
        unsigned elapsed_ = (unsigned)svcGetSystemTick() - start_; \
        volatile struct cdt_read_counts *c_ = &cdt_reads[cdt_reader]; \
        c_->hunk_ticks += elapsed_; \
        if (elapsed_ > c_->hunk_max) c_->hunk_max = elapsed_; \
        c_->hunks++; \
    } else { call; } \
} while (0)
#else
#define CDT_HUNK(call) call
#endif
#endif
