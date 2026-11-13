/* BreadBox's log ring (app/ui/log_ring.c): lines written while the file is shut (HOME,
 * sleep) wait and are written after it reopens, in order; a full ring drops new lines and
 * counts them, never older ones, and never waits; a failed write keeps what it could not
 * write. Then four threads log at once while one drains: every line whole, each thread's
 * lines in its order, every line written or counted as dropped. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log_ring.h"
static char out[2 * LOG_RING_BYTES];
static unsigned outn;
static bool refuse;
static bool to_out(const char *p, unsigned n, void *ctx)
{
	(void)ctx;
	if (refuse) return false;
	memcpy(out + outn, p, n);
	outn += n;
	return true;
}

#define THREADS 4
#define LINES 20000
static volatile bool stop;
static unsigned long got, bad, next_seq[THREADS];
static bool check_line(const char *p, unsigned n, void *ctx)
{
	(void)ctx;
	unsigned t, seq, len;
	// "t<thread> s<seq> <filler of seq % 200 dots> end\n"
	if (sscanf(p, "t%u s%u %n", &t, &seq, &len) != 2 || t >= THREADS || seq < next_seq[t]) { bad++; return true; }
	unsigned dots = 0;
	while (len + dots < n && p[len + dots] == '.') dots++;
	if (dots != seq % 200 || n - len - dots != 4 || memcmp(p + len + dots, "end\n", 4)) bad++;
	next_seq[t] = seq + 1;
	got++;
	return true;
}
static void *producer(void *arg)
{
	unsigned t = (unsigned)(size_t)arg;
	char line[400];
	for (unsigned seq = 0; seq < LINES; seq++) {
		int n = snprintf(line, sizeof(line), "t%u s%u ", t, seq);
		memset(line + n, '.', seq % 200);
		n += (int)(seq % 200);
		memcpy(line + n, "end\n", 4);
		log_ring_put(line, (unsigned)n + 4);
	}
	return NULL;
}
static void *consumer(void *arg)
{
	(void)arg;
	while (!__atomic_load_n(&stop, __ATOMIC_ACQUIRE)) log_ring_drain(check_line, NULL);
	log_ring_drain(check_line, NULL);
	return NULL;
}

int main(void)
{
	log_ring_init();
	char line[64];
	assert(log_ring_put("a\n", 2) && log_ring_drain(to_out, NULL) == 2 && outn == 2);
	/* shut: nothing leaves the ring, new lines wait */
	log_ring_close(true);
	unsigned kept = 0;
	for (int i = 0; i < 100; i++) {
		int n = snprintf(line, sizeof(line), "line %03d while shut\n", i);
		assert(log_ring_put(line, (unsigned)n));
		kept += (unsigned)n;
	}
	assert(log_ring_drain(to_out, NULL) == 0 && outn == 2);
	/* fill it while shut: the overflow is dropped and counted, the first lines stay */
	unsigned dropped = 0;
	for (int i = 0; i < 8000; i++) {
		int n = snprintf(line, sizeof(line), "filler %04d ......................\n", i);
		if (log_ring_put(line, (unsigned)n)) kept += (unsigned)n; else dropped++;
	}
	assert(dropped > 0 && log_ring_take_dropped() == dropped && log_ring_take_dropped() == 0);
	/* open again: everything kept comes out, in order */
	log_ring_close(false);
	refuse = true;   // the card refuses: nothing is lost
	assert(log_ring_drain(to_out, NULL) == 0);
	refuse = false;
	assert(log_ring_drain(to_out, NULL) == kept && outn == 2 + kept);
	assert(!strncmp(out + 2, "line 000 while shut\n", 20) && strstr(out, "line 099 while shut\n"));
	/* full while open: dropped and counted too (the caller never waits) */
	dropped = 0;
	for (int i = 0; i < 8000; i++) {
		int n = snprintf(line, sizeof(line), "open %04d .......................\n", i);
		dropped += !log_ring_put(line, (unsigned)n);
	}
	assert(dropped > 0 && log_ring_take_dropped() == dropped);
	outn = 0;
	log_ring_drain(to_out, NULL);
	assert(!strncmp(out, "open 0000 ", 10));

	/* four threads at once, one reader */
	log_ring_init();
	pthread_t p[THREADS], c;
	assert(!pthread_create(&c, NULL, consumer, NULL));
	for (size_t t = 0; t < THREADS; t++) assert(!pthread_create(&p[t], NULL, producer, (void *)t));
	for (int t = 0; t < THREADS; t++) pthread_join(p[t], NULL);
	__atomic_store_n(&stop, true, __ATOMIC_RELEASE);
	pthread_join(c, NULL);
	unsigned long lost = log_ring_take_dropped();
	printf("log ring threads: %lu lines written, %lu dropped (full), %lu bad\n", got, lost, bad);
	assert(!bad && got + lost == (unsigned long)THREADS * LINES && got > 0);
	puts("Log ring: lines kept while shut, in order after, full ring drops and counts without waiting, failed writes keep, 4 writers at once whole and in order: PASS");
	return 0;
}
