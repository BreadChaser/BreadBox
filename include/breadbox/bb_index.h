#ifndef BB_INDEX_H
#define BB_INDEX_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bb_system.h"
int bb_index_count(const bb_system_t *system);
/* One value for all of bb_index_count's results: it changes when any system's count (or their visibility) changes.
 * Keep the last value and recount only when it differs. */
uint32_t bb_index_counts_key(void);
bool bb_index_get(const bb_system_t *system,int index,char *path,size_t cap,bb_entry *entry);
/* Main/UI thread owns start/shutdown. Getters copy under a short lock;
 * no filesystem access is performed by start or the snapshot getters. */
void bb_index_start(const char *root, bool refresh);
/* UI thread only: always advance every adapter, regardless of screen population.
 * refresh is retained for caller compatibility; it cannot suppress publication. */
bool bb_index_poll(bool refresh);
/* Menu-frame service: advance publication without consuming its change notice.
 * The next screen-level poll consumes the latched notice exactly once. */
/* UI thread only; never publish during updater install/commit. */
void bb_index_publish(void);
/* Reap cancelled work and start a queued request without polling adapters. */
void bb_index_service(void);
/* Main thread: run deferred services even before a games folder is chosen. No scan. */
bool bb_index_background_start(void);
/* Nonblocking enqueue on the index worker (Old: core 0 priority 0x31; New: core 2).
 * False leaves ownership with the caller; retry later, never run I/O inline. */
bool bb_index_defer(void (*fn)(void *),void *arg);
/* Low lane for one bounded marker operation; callbacks must acknowledge cancellation. */
/* Allocation-free teardown slot. One importer may transfer its final reference
 * here even when the ordinary queue is full. Drained before worker shutdown. */
bool bb_index_defer_cleanup(void (*fn)(void *),void *arg);
bool bb_index_defer_low(void (*fn)(void *),void *arg);
/* UI-visible low-lane work precedes ordinary metadata; owned buffers only. */
bool bb_index_defer_visible(void (*fn)(void *),void *arg);
/* Worker callbacks only: cooperatively service visible jobs during long reads. */
bool bb_index_visible_checkpoint(void);
bool bb_index_cancelled(void);
int bb_index_gba_count(void);
bool bb_index_busy(void);
/* A published usable snapshot exists, even while validation is busy or limited.
 * Scan success/limits are reported separately by bb_index_status(). */
bool bb_index_complete(void);
typedef enum {
    BB_INDEX_IDLE, BB_INDEX_SCANNING, BB_INDEX_COMPLETE,
    BB_INDEX_LIMITED, BB_INDEX_ERROR
} bb_index_status_t;
/* LIMITED: a memory limit was reached (bb_index.c MAX_ITEMS/MAX_BYTES/MAX_PENDING; no time or file-count
 * limit); the games a previous complete check found stay. ERROR: an unreadable directory or worker failure.
 * Neither is persisted as a complete cache. */
bb_index_status_t bb_index_status(void);
/* Scans that ended with a status (bb_index_status): a new value is a new result to report. */
unsigned bb_index_scans(void);
/* Changes only when a snapshot is replaced, never for append/completion. */
unsigned bb_index_generation(void);
const char *bb_index_root(void);
void bb_index_shutdown(void);
/* Nonblocking completion/reap, after shutdown; caller owns index lifecycle. */
bool bb_index_shutdown_done(void);
/* Exit, after the last drain of importer and cover retirement: no worker starts again (an
 * importer's retry_cleanup, bb_index_start), so the final shutdown's worker is the last one. */
void bb_index_close(void);
/* PS1 candidate snapshot: existing library content checks/grouping still apply. */
int bb_index_ps1_count(void);
bool bb_index_ps1_get(int i, char *path, size_t cap);
int bb_index_folder_hint(const char *name); /* 1 registered, -1 foreign, 0 unknown */
#define BB_SYSTEM_UNKNOWN 0
/* Cheap bounded discovery, preserves any existing saved directory. */
bool bb_index_discover(const char *saved, char *out, size_t cap);
/* Map a selected system folder to its parent root; leaves other paths intact. */
bool bb_index_root_normalize(const char *path, char *out, size_t cap);
#endif
