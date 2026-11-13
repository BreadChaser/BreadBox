#include "app_policy.h"
#include "log_ring.h"
#include <stdint.h>
#include <string.h>

// No lock anywhere (log_ring.h): a line takes its place in one atomic step and is marked
// written after its bytes; the log's thread takes lines in order up to the first unmarked one.
// A record: a 4-byte header, then the line, rounded up to 4 bytes. The header is 0 until the
// line is in: what the reader has taken is zeroed again before its place is given back.
#define DONE 0x10000u   // header: the line is in
#define PAD  0x20000u   // header: the rest of the ring up to its end is skipped
static uint32_t words[LOG_RING_BYTES / 4];
static char *const ring = (char *)words;
static unsigned head, tail, dropped;   // bytes given out / taken (unsigned, wrap around)
static bool closed;

static unsigned size_of(unsigned n) { return (4 + n + 3) & ~3u; }
static void mark(unsigned at, uint32_t h) { __atomic_store_n(&words[at / 4], h, __ATOMIC_RELEASE); }

void log_ring_init(void)
{
	memset(words, 0, sizeof(words));
	head = tail = dropped = 0;
	closed = false;
}

bool log_ring_put(const char *line, unsigned n)
{
	if (n > LOG_RING_LINE) n = LOG_RING_LINE;
	unsigned need = size_of(n), h, pad;
	for (;;) {
		h = __atomic_load_n(&head, __ATOMIC_RELAXED);
		unsigned t = __atomic_load_n(&tail, __ATOMIC_ACQUIRE), at = h % LOG_RING_BYTES;
		pad = LOG_RING_BYTES - at < need ? LOG_RING_BYTES - at : 0;   // a line never wraps
		if (h + pad + need - t > LOG_RING_BYTES) {
			__atomic_add_fetch(&dropped, 1, __ATOMIC_RELAXED);
			return false;
		}
		if (__atomic_compare_exchange_n(&head, &h, h + pad + need, true, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
			break;
	}
	if (pad) mark(h % LOG_RING_BYTES, DONE | PAD | pad);
	unsigned at = (h + pad) % LOG_RING_BYTES;
	memcpy(ring + at + 4, line, n);
	mark(at, DONE | n);
	return true;
}

void log_ring_close(bool c) { __atomic_store_n(&closed, c, __ATOMIC_RELEASE); }
bool log_ring_closed(void) { return __atomic_load_n(&closed, __ATOMIC_ACQUIRE); }

unsigned log_ring_drain(bool (*write)(const char *p, unsigned n, void *ctx), void *ctx)
{
	unsigned total = 0, t = __atomic_load_n(&tail, __ATOMIC_RELAXED);
	while (!log_ring_closed() && t != __atomic_load_n(&head, __ATOMIC_ACQUIRE)) {
		unsigned at = t % LOG_RING_BYTES;
		uint32_t h = __atomic_load_n(&words[at / 4], __ATOMIC_ACQUIRE);
		if (!(h & DONE)) break;   // given out, not written yet: the lines after it wait
		unsigned n = h & 0xffffu, size = h & PAD ? n : size_of(n);
		if (!(h & PAD)) {
			if (!write(ring + at + 4, n, ctx)) break;   // only what write() took leaves the ring
			total += n;
		}
		memset(ring + at, 0, size);
		__atomic_store_n(&tail, t += size, __ATOMIC_RELEASE);
	}
	return total;
}

unsigned log_ring_take_dropped(void) { return __atomic_exchange_n(&dropped, 0, __ATOMIC_RELAXED); }
