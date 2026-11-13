// The shared rewind engine (systems/common/retro_rewind.c, used by GBA) with
// its real worker thread on a host pthread: the PS1 rewind worker checks,
// run through the system-supplied state calls and a byte budget. Every step
// back must land exactly on an earlier captured state, never on a mix of two
// histories; a too-small budget keeps only the newest steps.
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <3ds.h>
#include <math.h>
#include <stddef.h>
#include <time.h>
#include <sys/types.h>
#include "app_log.h"
#include "retro_rewind.h"
typedef int s32;
typedef struct { pthread_mutex_t m; pthread_cond_t c; bool set; } LightEvent;
typedef pthread_t *Thread;
#define RESET_ONESHOT 0
#define CUR_THREAD_HANDLE 0
#define U64_MAX UINT64_MAX

#define WORDS 64
static u32 game[WORDS];            // the "emulated game": its whole state
static int fail_deltas;            // next N delta allocations fail
static bool counting;              // after rewind_start: allocations are deltas
static size_t state_size(void) { return sizeof(game); }
static bool state_save(void *p, size_t n) { assert(n >= sizeof(game)); memcpy(p, game, sizeof(game)); return true; }
static int fail_loads;             // next N loads fail (half-restored)
static bb_state_result state_load(const void *p, size_t n)
{
	assert(n >= sizeof(game));
	if (fail_loads > 0) {
		fail_loads--;
		game[0] ^= 0xdead;   // the game was partly overwritten
		return BB_STATE_PARTLY;
	}
	memcpy(game, p, sizeof(game));
	return BB_STATE_OK;
}
static const bb_rewind_ops test_ops = { .size = state_size, .save = state_save, .load = state_load };
static size_t budget = 1u << 30;
void log_printf(const char *fmt, ...) { (void)fmt; }

static void LightEvent_Init(LightEvent *e, int mode)
{
	(void)mode;
	pthread_mutex_init(&e->m, NULL);
	pthread_cond_init(&e->c, NULL);
	e->set = false;
}
static void LightEvent_Signal(LightEvent *e)
{
	pthread_mutex_lock(&e->m);
	e->set = true;
	pthread_cond_signal(&e->c);
	pthread_mutex_unlock(&e->m);
}
static void LightEvent_WaitTimeout(LightEvent *e, long long ns)
{
	struct timespec t;
	clock_gettime(CLOCK_REALTIME, &t);
	t.tv_nsec += ns % 1000000000LL;
	t.tv_sec += ns / 1000000000LL + t.tv_nsec / 1000000000LL;
	t.tv_nsec %= 1000000000LL;
	pthread_mutex_lock(&e->m);
	while (!e->set && pthread_cond_timedwait(&e->c, &e->m, &t) == 0)
		;
	e->set = false;   // one-shot
	pthread_mutex_unlock(&e->m);
}
static void svcGetThreadPriority(s32 *p, int handle) { (void)handle; *p = 0x30; }
static void svcSleepThread(long long ns) { usleep(ns / 1000); }
struct start { void (*fn)(void *); void *arg; };
static void *trampoline(void *p)
{
	struct start s = *(struct start *)p;
	free(p);
	s.fn(s.arg);
	return NULL;
}
static Thread threadCreate(void (*fn)(void *), void *arg, size_t stack, int prio, int cpu, bool detached)
{
	(void)stack; (void)prio; (void)cpu; (void)detached;
	Thread t = malloc(sizeof(*t));
	struct start *s = malloc(sizeof(*s));
	*s = (struct start){ fn, arg };
	if (pthread_create(t, NULL, trampoline, s)) { free(t); free(s); return NULL; }
	return t;
}
static void threadJoin(Thread t, u64 timeout) { (void)timeout; pthread_join(*t, NULL); }
static void threadFree(Thread t) { free(t); }
static void *test_malloc(size_t n)
{
	if (counting && fail_deltas > 0) {
		fail_deltas--;
		return NULL;
	}
	return malloc(n);
}
#define malloc test_malloc
#include "../../systems/common/retro_rewind.c"
#undef malloc

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void set_game(u32 v)
{
	for (int i = 0; i < WORDS; i++)
		game[i] = i % 3 ? v * 2654435761u + i : v;   // some words same, some different
}

// one capture: INTERVAL frames, then wait for the worker to finish
static void capture(void)
{
	for (int f = 0; f < BB_REWIND_INTERVAL; f++)
		bb_rewind_capture();
	while (__atomic_load_n(&busy, __ATOMIC_ACQUIRE))
		usleep(100);
}

static bool is_state(u32 v)
{
	u32 want[WORDS];
	memcpy(want, game, sizeof(want));
	set_game(v);
	bool same = !memcmp(want, game, sizeof(want));
	memcpy(game, want, sizeof(want));
	return same;
}

static void start(void)
{
	counting = false;
	CHECK(bb_rewind_start(&test_ops, budget, 2));
	counting = true;
}

int main(void)
{
	// The reviewer's case: 0 -> 1, the 1 -> 2 delta can't be allocated, then 3
	start();
	set_game(0); capture();               // base 0
	set_game(1); capture();               // step 0 -> 1
	fail_deltas = 1;
	set_game(2); capture();               // 1 -> 2 lost: history must restart at 2
	set_game(3); capture();               // step 2 -> 3
	CHECK(bb_rewind_step(NULL) && is_state(2));  // the fix: 2, not 3 ^ (0 ^ 1)
	CHECK(!bb_rewind_step(NULL) && is_state(2)); // nothing older is valid any more
	bb_rewind_stop();

	// Random histories with random failures, against a model of what is valid
	srand(1234);
	for (int round = 0; round < 200; round++) {
		start();
		u32 model[MAX_STEPS + 1];
		int n = 0;   // model[0..n-1]: states a step back may reach, newest last
		u32 v = round * 1000;
		set_game(v); capture();
		model[n++] = v;
		int steps = 1 + rand() % 40;
		for (int s = 0; s < steps; s++) {
			if (rand() % 6 == 0) {   // step back
				bool ok = bb_rewind_step(NULL);
				CHECK(ok == (n > 1));
				if (n > 1) {
					n--;
					CHECK(is_state(model[n - 1]));
					v = model[n - 1];
				}
				continue;
			}
			if (rand() % 5 == 0)
				fail_deltas = 1;
			bool fails = fail_deltas > 0;
			set_game(++v); capture();
			if (fails)
				n = 0;   // history starts over at the new capture
			model[n++] = v;
		}
		while (n > 1) {   // walk all the way back
			CHECK(bb_rewind_step(NULL));
			n--;
			CHECK(is_state(model[n - 1]));
		}
		CHECK(!bb_rewind_step(NULL));
		fail_deltas = 0;
		if (round % 3 == 0)
			bb_rewind_reset();   // a loaded state: the history no longer leads here
		if (round % 3 == 0)
			CHECK(!bb_rewind_step(NULL));
		set_game(++v); capture();   // stop while the worker may be busy
		bb_rewind_stop();
	}
	// A full ring: the oldest steps go, the rest still lead back exactly
	start();
	set_game(0); capture();
	for (u32 k = 1; k <= MAX_STEPS + 100; k++) {
		set_game(k); capture();
	}
	int back = 0;
	while (bb_rewind_step(NULL)) {
		back++;
		CHECK(is_state(MAX_STEPS + 100 - back));
	}
	CHECK(back == MAX_STEPS);
	bb_rewind_stop();
	// Review 3 F7c: a step that can't be loaded is reported (-1), the history
	// is dropped (nothing older leads anywhere valid), and the caller is told
	// the game was left half-restored
	start();
	set_game(0); capture();
	set_game(1); capture();
	set_game(2); capture();
	fail_loads = 1;
	bb_state_result r;
	CHECK(bb_rewind_step(&r) == -1 && r == BB_STATE_PARTLY);
	CHECK(bb_rewind_step(NULL) == 0);
	set_game(7); capture();
	set_game(8); capture();
	CHECK(bb_rewind_step(NULL) == 1 && is_state(7));   // a new history from the next capture
	bb_rewind_stop();
	// A small byte budget: the oldest steps go first, the newest still lead back
	budget = 4 * (WORDS * 4 + 16);
	start();
	set_game(0); capture();
	for (u32 k = 1; k <= 40; k++) {
		set_game(k); capture();
		CHECK(bb_rewind_bytes() <= budget);
	}
	back = 0;
	while (bb_rewind_step(NULL) == 1) {
		back++;
		CHECK(is_state(40 - back));
	}
	CHECK(back >= 3 && back < 40);
	bb_rewind_stop();
	CHECK(!bb_rewind_active() && !bb_rewind_bytes());
	if (failures) {
		fprintf(stderr, "FAIL: %d rewind worker checks\n", failures);
		return 1;
	}
	puts("PASS: shared rewind engine (real thread, encode/apply) never restores a state outside its history, incl. failed delta allocations, reset and stop");
	return 0;
}
