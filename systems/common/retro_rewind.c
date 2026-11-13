/* Rewind history for libretro-hosted systems (GBA). GPL-2.0-or-later.
 * Every BB_REWIND_INTERVAL frames the game state is captured; the difference
 * from the previous capture is compressed and kept in a history of about
 * `budget` bytes. Stepping back applies those differences in reverse. The
 * comparing and compressing run on a worker on a spare core, so the game only
 * pays for the capture itself. Mirrors systems/ps1/rewind.c (kept unchanged
 * for PSX3D's layout); the host test checks the same history invariants. */
#include <stdlib.h>
#include <string.h>
#include <3ds.h>
#include <stddef.h>
#include "app_log.h"
#include "retro_rewind.h"

#define MAX_STEPS 1024

static const bb_rewind_ops *ops;
static u32 *cur, *next;      // newest state, and the capture being added
static u32 *scratch;         // worst-case compressed delta
static size_t words, budget; // state size in words, history cap in bytes
static void *steps[MAX_STEPS];
static size_t step_bytes[MAX_STEPS];
static int first, count;     // ring of compressed deltas, oldest at first
static size_t total;
static bool have_cur, active;
// busy: the worker owns cur/next/the ring until it clears it (release), the
// game thread reads it with acquire; quit only stops the worker
static bool busy;
static volatile bool quit;
static Thread worker;
static LightEvent job;
static int frame_ctr;
static unsigned failed_steps;   // history dropped for lack of memory

// delta = a XOR b as runs: [zero words][literal words][literals...]
static size_t encode(const u32 *a, const u32 *b, u32 *out)
{
	size_t i = 0, o = 0;
	while (i < words) {
		u32 z = 0, l = 0;
		while (i + z < words && a[i + z] == b[i + z])
			z++;
		size_t s = i + z;
		while (s + l < words && a[s + l] != b[s + l])
			l++;
		out[o++] = z;
		out[o++] = l;
		for (u32 k = 0; k < l; k++)
			out[o++] = a[s + k] ^ b[s + k];
		i = s + l;
	}
	return o * 4;
}

static void apply(u32 *state, const u32 *d, size_t bytes)
{
	size_t n = bytes / 4, o = 0, i = 0;
	while (o < n) {
		u32 z = d[o++], l = d[o++];
		i += z;
		for (u32 k = 0; k < l; k++)
			state[i++] ^= d[o++];
	}
}

static void drop_oldest(void)
{
	free(steps[first]);
	total -= step_bytes[first];
	first = (first + 1) % MAX_STEPS;
	count--;
}

static void worker_main(void *arg)
{
	(void)arg;
	while (!quit) {
		LightEvent_WaitTimeout(&job, 100000000LL);
		if (!__atomic_load_n(&busy, __ATOMIC_ACQUIRE))
			continue;
		size_t bytes = encode(cur, next, scratch);
		void *d = malloc(bytes);
		if (d) {
			memcpy(d, scratch, bytes);
			while (count && (count == MAX_STEPS || total + bytes > budget))
				drop_oldest();
			int at = (first + count) % MAX_STEPS;
			steps[at] = d;
			step_bytes[at] = bytes;
			count++;
			total += bytes;
		} else {
			// The step to the capture can't be kept, and the older steps
			// lead to cur, not to it: drop them, the history starts here
			while (count)
				drop_oldest();
			failed_steps++;
		}
		u32 *t = cur; cur = next; next = t;   // the capture is the newest state now
		__atomic_store_n(&busy, false, __ATOMIC_RELEASE);
	}
}

static void free_all(void)
{
	while (count)
		drop_oldest();
	free(cur); free(next); free(scratch);
	cur = next = scratch = NULL;
}

bool bb_rewind_start(const bb_rewind_ops *o, size_t bytes, int core)
{
	bb_rewind_stop();
	size_t size = o && o->size ? o->size() : 0;
	if (!size || !bytes)
		return false;
	ops = o;
	budget = bytes;
	words = (size + 3) / 4;
	cur = calloc(words, 4);
	next = calloc(words, 4);
	scratch = malloc(words * 4 + words * 8 / 2 + 64);   // worst case: alternating runs
	if (!cur || !next || !scratch) {
		free_all();
		log_printf("rewind: not enough memory\n");
		return false;
	}
	LightEvent_Init(&job, RESET_ONESHOT);
	quit = busy = have_cur = false;
	failed_steps = 0;
	first = count = 0;
	total = 0;
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	worker = threadCreate(worker_main, NULL, 16 * 1024, prio + 1, core, false);
	if (!worker) {
		free_all();
		log_printf("rewind: no spare core\n");
		return false;
	}
	active = true;
	frame_ctr = 0;
	log_printf("rewind: on, state %u KB, history %u KB\n", (unsigned)(size / 1024), (unsigned)(bytes / 1024));
	return true;
}

void bb_rewind_stop(void)
{
	if (!active)
		return;
	// let a capture being compressed finish: the worker uses the buffers
	quit = true;
	LightEvent_Signal(&job);
	threadJoin(worker, U64_MAX);
	threadFree(worker);
	free_all();
	log_printf("rewind: off\n");
	active = false;
}

bool bb_rewind_active(void) { return active; }
size_t bb_rewind_bytes(void) { return active ? total : 0; }

void bb_rewind_capture(void)
{
	if (!active || ++frame_ctr < BB_REWIND_INTERVAL || __atomic_load_n(&busy, __ATOMIC_ACQUIRE))
		return;
	if (failed_steps) {
		log_printf("rewind: not enough memory, history dropped (%u)\n", failed_steps);
		failed_steps = 0;
	}
	frame_ctr = 0;
	if (!have_cur) {
		have_cur = ops->save(cur, words * 4);
		return;
	}
	if (ops->save(next, words * 4)) {
		__atomic_store_n(&busy, true, __ATOMIC_RELEASE);
		LightEvent_Signal(&job);
	}
}

int bb_rewind_step(bb_state_result *result)
{
	if (result)
		*result = BB_STATE_OK;
	if (!active || !have_cur)
		return 0;
	while (__atomic_load_n(&busy, __ATOMIC_ACQUIRE))
		svcSleepThread(1000000LL);
	if (!count)
		return 0;
	int last = (first + count - 1) % MAX_STEPS;
	apply(cur, steps[last], step_bytes[last]);
	free(steps[last]);
	total -= step_bytes[last];
	count--;
	frame_ctr = 0;
	bb_state_result r = ops->load(cur, words * 4);
	if (r == BB_STATE_OK)
		return 1;
	if (result)
		*result = r;
	log_printf("rewind: a step could not be loaded (%d), history dropped\n", (int)r);
	while (count)
		drop_oldest();
	have_cur = false;
	return -1;
}

void bb_rewind_reset(void)
{
	if (!active)
		return;
	while (__atomic_load_n(&busy, __ATOMIC_ACQUIRE))
		svcSleepThread(1000000LL);
	while (count)
		drop_oldest();
	have_cur = false;
}
