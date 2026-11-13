#include "bb_writers.h"
#include "ht_trace.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/socket.h>
#include <3ds.h>
#include <citro2d.h>
#include "app_paths.h"
#include "app_log.h"

#include <malloc.h>
#include <3ds/3dslink.h>
#include "app_policy.h"
#include "log_ring.h"

static FILE *log_fp;
static aptHookCookie log_cookie;
static bool log_held;
static unsigned log_quiesced;

// A buffer that holds a whole stats report, so log_hold() makes it one write.
static FILE *log_open(const char *mode)
{
	FILE *f = fopen(PSX3D_LOG, mode);
	if (f)
		setvbuf(f, NULL, _IOFBF, 4096);
	return f;
}

// Lines go to the SD card from a thread of their own (BreadBox): a flush on the menu thread
// waited for whatever else used the card (a 1.5 MB read, a 4.4 MB state, covers being
// saved): 100-600 ms frames on the consoles. A line never waits (log_ring.c): a full ring
// drops it and the log says how many. log_hold(false) still writes at once. While the APT
// hook has the file shut (HOME, sleep) the lines wait in the ring, written after it reopens.
static LightLock fp_lock;
static LightEvent ring_wake;
static Thread writer;
static volatile bool writer_stop;
static bool write_fp(const char *p, unsigned n, void *unused)
{
	(void)unused;
	return log_fp && fwrite(p, 1, n, log_fp) == n;
}
static void drain(void)   // fp_lock held
{
	if (!log_fp || log_ring_closed())
		return;
	unsigned lost = log_ring_take_dropped();
	if (lost)
		fprintf(log_fp, "log: %u lines lost (the log was full, or shut for HOME or sleep)\n", lost);
	log_ring_drain(write_fp, NULL);
	fflush(log_fp);
}
static void writer_main(void *unused)
{
	(void)unused;
	while (!writer_stop) {
		LightEvent_Wait(&ring_wake);
		LightLock_Lock(&fp_lock);
		HT("writer enter");drain();HT("writer leave");
		LightLock_Unlock(&fp_lock);
	}
}
static void log_now(void)   // everything so far on the card before returning
{
	LightLock_Lock(&fp_lock);
	drain();
	LightLock_Unlock(&fp_lock);
}

// Close the log while suspended (HOME, sleep): an SD file held open across
// the handoff left the HOME Menu waiting forever. Reopen on return.
static void log_apt(APT_HookType hook, void *param)
{
	(void)param;
 if(__atomic_load_n(&log_quiesced,__ATOMIC_ACQUIRE))return;
	LightLock_Lock(&fp_lock);
	if (hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONSLEEP || hook == APTHOOK_ONEXIT) {
		drain();
		log_ring_close(true);   // the writer keeps its hands off until the file is back
	}
	if ((hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONSLEEP || hook == APTHOOK_ONEXIT) && log_fp) {
		fclose(log_fp);
		log_fp = NULL;
	} else if ((hook == APTHOOK_ONRESTORE || hook == APTHOOK_ONWAKEUP) && !log_fp && !bb_writers_exclusive()) {
		log_fp = log_open("a");
	}
	if ((hook == APTHOOK_ONRESTORE || hook == APTHOOK_ONWAKEUP) && log_fp) {
		log_ring_close(false);
		LightEvent_Signal(&ring_wake);   // what waited while it was shut
	}
	LightLock_Unlock(&fp_lock);
}
// The HOME Menu's Close wakes us without the restore hook, so the log would
// stay shut for the exit. log_apt closes it again at exit.
void log_resume(void)
{
 __atomic_store_n(&log_quiesced,0,__ATOMIC_RELEASE);
	LightLock_Lock(&fp_lock);
	if (!log_fp)
		log_fp = log_open("a");
	if (log_fp && writer) {
		log_ring_close(false);
		LightEvent_Signal(&ring_wake);
	}
	LightLock_Unlock(&fp_lock);
}


// Netloaded with 3dslink: the log is also sent to the PC. Never block on
// it: if the PC stops reading, a blocking send froze the whole game.
static int link_fd = -1;
static u32 *soc_buf;

void log_init(void)
{
	// Keep the last five sessions (log.txt newest, log.4.txt oldest), so the log
	// of a crashed session survives the relaunch. Renames only: cheap at startup.
	// FAT rename does not replace a file, so drop the oldest first.
	remove(PSX3D_DIR "/log.4.txt");
	rename(PSX3D_DIR "/log.3.txt", PSX3D_DIR "/log.4.txt");
	rename(PSX3D_DIR "/log.2.txt", PSX3D_DIR "/log.3.txt");
	rename(PSX3D_DIR "/log.1.txt", PSX3D_DIR "/log.2.txt");
	rename(PSX3D_LOG, PSX3D_DIR "/log.1.txt");
	log_fp = log_open("w");
	log_ring_init();
	LightLock_Init(&fp_lock);
	LightEvent_Init(&ring_wake, RESET_ONESHOT);
	aptHook(&log_cookie, log_apt, NULL);

	// launched from a PC with 3dslink: mirror the log to its console (3dslink -s)
	if (__3dslink_host.s_addr) {
		soc_buf = memalign(0x1000, 0x100000);
		if (soc_buf && R_SUCCEEDED(socInit(soc_buf, 0x100000)))
			link_fd = link3dsConnectToHost(false, false);
	}
}

// The writer thread, once the console's model is known (main.c): below the menu and the games;
// Old 3DS core 0 (on the system core each write held up the GPU's events there: a missed frame
// at every few lines), New core 2. Until then lines are written at once.
void log_writer_start(int core)
{
	if (!writer) {writer_stop=false;writer = threadCreate(writer_main, NULL, 8 * 1024, 0x38, core, false);}
}

void log_printf(const char *fmt, ...)
{
	va_list ap;
	unsigned ms = (unsigned)(osGetTime() % 1000000);   // stamp: seconds.millis
	if (link_fd >= 0) {
		char line[512];
		int n = snprintf(line, sizeof(line), "%3u.%03u ", ms / 1000, ms % 1000);
		va_start(ap, fmt);
		vsnprintf(line + n, sizeof(line) - n, fmt, ap);
		va_end(ap);
		send(link_fd, line, strlen(line), MSG_DONTWAIT);   // dropped if the PC is behind
	}
	if (writer) {
		char line[512];
		int n = snprintf(line, sizeof(line), "%3u.%03u ", ms / 1000, ms % 1000);
		va_start(ap, fmt);
		int m = vsnprintf(line + n, sizeof(line) - n, fmt, ap);
		va_end(ap);
		n += m < 0 ? 0 : m >= (int)sizeof(line) - n ? (int)sizeof(line) - n - 1 : m;
		log_ring_put(line, (unsigned)n);   // never waits: full, it is dropped and counted
		if (!log_held)
			LightEvent_Signal(&ring_wake);
		return;
	}
	if (!log_fp)
		return;
	fprintf(log_fp, "%3u.%03u ", ms / 1000, ms % 1000);
	va_start(ap, fmt);
	vfprintf(log_fp, fmt, ap);
	va_end(ap);
	// flush every line so the log survives a crash
	if (!log_held)
		fflush(log_fp);
}

// The once-a-second stats report is ~8 lines. Flushing each one is a separate
// SD write on the main thread: together ~66 ms, which is more than the frame
// pacer catches up, so Silent Hill lost 4 frames every second (93%). Hold the
// flushes during the report and write it once.
void log_hold(bool hold)
{
	log_held = hold;
	if (writer) {
		if (!hold)
			log_now();
		return;
	}
	if (!hold && log_fp)
		fflush(log_fp);
}

// The lines so far on their way to the card now, without waiting for it (BreadBox: the
// writer thread; the arena's step-by-step trace flushed each line on the menu thread,
// ~100 ms apiece on the Old 3DS: 0.7 s of every quit)
void log_soon(void)
{
	if (writer) {
		LightEvent_Signal(&ring_wake);
		return;
	}
	if (log_fp)
		fflush(log_fp);
}

// true when the log also goes to a PC (3dslink): cheap to send often
bool log_linked(void) { return link_fd >= 0; }

// A writer that will not stop in time (stuck in an SD write) is left running with the
// log as it was: the caller abandons whatever needed the log closed.
bool log_close_timeout(uint64_t timeout)
{
 __atomic_store_n(&log_quiesced,1,__ATOMIC_RELEASE);
	if (writer) {
		writer_stop = true;
		LightEvent_Signal(&ring_wake);
		if(threadJoin(writer, timeout)!=0){
			writer_stop = false;
			__atomic_store_n(&log_quiesced,0,__ATOMIC_RELEASE);
			return false;
		}
		threadFree(writer);
		writer = NULL;
		log_now();
	}
	if (log_fp)
		fclose(log_fp);
	log_fp = NULL;
	if (link_fd >= 0) {
		closesocket(link_fd);
		socExit();
	}
	link_fd = -1;
 return true;
}
void log_close(void)
{(void)log_close_timeout(UINT64_MAX);}
