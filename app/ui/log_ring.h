/* BreadBox's log lines on their way to the SD card (log.c): one ring, filled by any thread,
 * emptied by the log's writer thread into the open file. Without a lock: on the Old 3DS a
 * line logged by the menu thread waited 8-100 ms for the writer, a low thread on the
 * time-limited system core, to let go of the ring (every slow frame logs, so the next one
 * was slow too). A line that does not fit is dropped and counted, never written over older
 * ones and never waited for. While the file is closed (HOME, sleep: log.c's APT hook)
 * nothing is taken out: lines wait. GPL-2.0-or-later. */
#ifndef LOG_RING_H
#define LOG_RING_H
#include <stdbool.h>
#define LOG_RING_BYTES (64 * 1024)   // a power of two
#define LOG_RING_LINE 1024           // longer lines are cut
void log_ring_init(void);
/* true queued; false the ring is full: dropped and counted */
bool log_ring_put(const char *line, unsigned n);
/* closed: the file is shut (APT handoff), nothing is drained until it is open again */
void log_ring_close(bool closed);
bool log_ring_closed(void);
/* hands what it holds to write() line by line, in order, while open; only what write() took
 * leaves the ring. One drainer at a time (log.c holds its file lock). Returns the bytes written. */
unsigned log_ring_drain(bool (*write)(const char *p, unsigned n, void *ctx), void *ctx);
/* lines dropped (full ring) since the last call */
unsigned log_ring_take_dropped(void);
#endif
