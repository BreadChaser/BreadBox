/* Immutable final files; producer transfers ownership before destroying its core.
 * Main submits/retries/discards; the later worker writes submitted tickets. bb_final_write is
 * one write at a time whoever calls it, and per path the newest ticket wins (generations).
 * Validators must use only immutable bytes, never an active core. */
#ifndef BB_FINAL_H
#define BB_FINAL_H
#include "safefile.h"
#include <stdint.h>
#define BB_FINAL_SLOTS 8   /* tickets are 0..BB_FINAL_SLOTS-1: owners may keep fixed per-ticket context */
typedef enum { BB_FINAL_EMPTY, BB_FINAL_QUEUED, BB_FINAL_WRITTEN, BB_FINAL_FAILED } bb_final_outcome;
/* Owns data on success; fixed slots need no allocation at teardown. identity
 * optionally matches stable prefix bytes (<=fmt->head), excluding payload CRCs.
 * Zero keeps the codec's existing header compatibility rules. */
int bb_final_take(const char *path,const safe_format_t *,void *data,uint32_t saved,size_t identity);
/* Called exactly once when written or explicitly discarded; context must be owned. */
void bb_final_after(int ticket,void (*done)(void *,bool),void *ctx);
/* A queued ticket the caller claimed back before it was written (a newer one replaces it): let go. */
void bb_final_drop(int ticket);
void bb_final_defer_failed(int ticket); /* already failed producer write, retain without retry */
/* Skipped (true) when a newer ticket of its path is already written; a failed one is kept. */
bool bb_final_write(int ticket);
bool bb_final_failed(void);
/* The newest failed ticket of each path first; older ones go once a newer one is written. */
bool bb_final_retry(void);
void bb_final_discard(void);
/* A capture failure has no retryable bytes; requires explicit acknowledgment. */
void bb_final_capture_failed(void);
bool bb_final_uncaptured(void);
void bb_final_acknowledge(void);
bb_final_outcome bb_final_status(const char *path,uint32_t *saved);
#endif
