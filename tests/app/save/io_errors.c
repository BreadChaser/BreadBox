// Host test for safefile.c when the SD card errs while a copy is asked about
// The real safefile.c, linked with
// -Wl,--wrap=stat,--wrap=fopen and its SAFEFILE_TEST hooks: EIO or EACCES on
// the metadata (stat), the open, the read or the close of <name>, <name>.bak or
// <name>.tmp, alone and followed by a full card, a failing close or failing
// renames. Only "no such file" is absence: an unreadable copy is never an
// empty save, never truncated, never moved aside by a write, and every
// original byte stays on the card.
#define _GNU_SOURCE
#include "safefile.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../../common/test_tmpdir.h"

#define SIZE (4096 + 3)
static unsigned char A[SIZE], B[SIZE], C[SIZE], N[SIZE];
static int failures, checks;
static char ctx[200];
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #c, ctx); } } while (0)

static bool head_ok(const void *head) { const unsigned char *h = head; return h[0] == 'M' && h[1] == 'C'; }
// deep: the last byte is the XOR of the others (a "weak" copy fails it)
static unsigned char xor_of(const unsigned char *p) { unsigned char x = 0; for (size_t i = 0; i + 1 < SIZE; i++) x ^= p[i]; return x; }
static bool deep_ok(const void *p, size_t n) { return n == SIZE && ((const unsigned char *)p)[SIZE - 1] == xor_of(p); }
// the backup two saves old goes to its own file (as history.c keeps it)
static void keep_retired(const char *bak, void *unused)
{
	(void)unused;
	char name[64];
	for (int i = 1; i < 100; i++) {
		snprintf(name, sizeof(name), "card.retired%d", i);
		if (access(name, F_OK) != 0) { rename(bak, name); return; }
	}
}
static const safe_format_t FMT = { .size = SIZE, .head = 2, .valid = head_ok, .retire = keep_retired };
static const safe_format_t DEEP = { .size = SIZE, .head = 2, .valid = head_ok, .deep = deep_ok };
// no history hook (battery, clock, final files): the old backup is deleted by safefile itself
static const safe_format_t PLAIN = { .size = SIZE, .head = 2, .valid = head_ok };

// ---------------------------------------------------------------- faults
// ON_TAIL: the whole payload reads, then the next read fails (stdio's error
// flag, EOF alone does not tell)
enum { ON_STAT = 1, ON_OPEN = 2, ON_READ = 4, ON_CLOSE = 8, ON_TAIL = 16 };
enum { LATER_NONE, LATER_FULL, LATER_CLOSE, LATER_RENAME, LATER_N };
static const char *const later_name[] = { "-", "SD full", "close fails", "renames fail" };
static struct {
	bool armed;
	const char *name;    // the copy the card errs on
	int error;           // EIO / EACCES
	unsigned on;         // ON_*
	int later;           // LATER_*
	int renamed_onto;    // renames onto that name while its stat errs
} fault;

static bool is(const char *path, const char *name) { return fault.name && !strcmp(path, name); }
static bool fd_is(FILE *f, const char *name)   // the file f was opened as name (in this folder)
{
	char link[64], got[600];
	snprintf(link, sizeof(link), "/proc/self/fd/%d", fileno(f));
	ssize_t n = readlink(link, got, sizeof(got) - 1);
	if (n <= 0) return false;
	got[n] = 0;
	size_t k = strlen(name);
	return (size_t)n > k && got[n - k - 1] == '/' && !strcmp(got + n - k, name);
}
static bool tmp_file(FILE *f) { return fd_is(f, "card.tmp") || fd_is(f, "card.rep"); }

int __real_stat(const char *path, struct stat *st);
int __wrap_stat(const char *path, struct stat *st)
{
	if (fault.armed && (fault.on & ON_STAT) && is(path, fault.name)) { errno = fault.error; return -1; }
	return __real_stat(path, st);
}
FILE *__real_fopen(const char *path, const char *mode);
typedef struct { unsigned char *data; size_t len, pos; int error; } tail_t;
static ssize_t tail_read(void *c, char *p, size_t n)
{
	tail_t *t = c;
	if (t->pos == t->len) { errno = t->error; return -1; }
	if (n > t->len - t->pos) n = t->len - t->pos;
	memcpy(p, t->data + t->pos, n);
	t->pos += n;
	return (ssize_t)n;
}
static int tail_close(void *c) { tail_t *t = c; free(t->data); free(t); return 0; }
FILE *__wrap_fopen(const char *path, const char *mode)
{
	if (fault.armed && (fault.on & ON_OPEN) && mode[0] == 'r' && is(path, fault.name)) { errno = fault.error; return NULL; }
	if (fault.armed && (fault.on & ON_TAIL) && mode[0] == 'r' && is(path, fault.name)) {
		FILE *f = __real_fopen(path, "rb");
		if (!f) return NULL;
		tail_t *t = calloc(1, sizeof(*t));
		t->data = malloc(2 * SIZE);
		t->len = fread(t->data, 1, 2 * SIZE, f);
		t->error = fault.error;
		fclose(f);
		return fopencookie(t, "r", (cookie_io_functions_t){ .read = tail_read, .close = tail_close });
	}
	return __real_fopen(path, mode);
}
size_t safe_test_fread(void *p, size_t size, size_t n, FILE *f)
{
	if (fault.armed && (fault.on & ON_READ) && fd_is(f, fault.name)) { errno = fault.error; return 0; }
	return fread(p, size, n, f);
}
size_t safe_test_fwrite(const void *p, size_t size, size_t n, FILE *f)
{
	if (fault.armed && fault.later == LATER_FULL) { errno = ENOSPC; return 0; }
	return fwrite(p, size, n, f);
}
int safe_test_fclose(FILE *f)
{
	bool fail = fault.armed && ((fault.later == LATER_CLOSE && tmp_file(f)) || ((fault.on & ON_CLOSE) && fd_is(f, fault.name)));
	int r = fclose(f);
	if (fail) { errno = EIO; return EOF; }
	return r;
}
int safe_test_rename(const char *from, const char *to)
{
	if (fault.armed && (fault.on & ON_STAT) && is(to, fault.name)) fault.renamed_onto++;
	if (fault.armed && fault.later == LATER_RENAME) { errno = EIO; return -1; }
	if (access(to, F_OK) == 0) return -1;   // FAT: never over a file
	return rename(from, to);
}

// ---------------------------------------------------------------- helpers
static void put(const char *path, const void *d, size_t n)
{
	FILE *f = __real_fopen(path, "wb");
	fwrite(d, 1, n, f);
	fclose(f);
}
static int content(const char *path)   // which payload a file holds exactly
{
	static unsigned char buf[SIZE + 1];
	FILE *f = __real_fopen(path, "rb");
	if (!f) return 0;
	size_t n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	if (n != SIZE) return '?';
	return !memcmp(buf, A, SIZE) ? 'A' : !memcmp(buf, B, SIZE) ? 'B' : !memcmp(buf, C, SIZE) ? 'C' : !memcmp(buf, N, SIZE) ? 'N' : '?';
}
static long size_of_bad(void)
{
	struct stat st;
	return __real_stat("card.bad", &st) ? -1 : (long)st.st_size;
}
static bool kept(int w)   // some file in the folder holds payload w byte for byte
{
	DIR *d = opendir(".");
	struct dirent *e;
	bool found = false;
	while (d && !found && (e = readdir(d)))
		found = e->d_name[0] != '.' && content(e->d_name) == w;
	if (d) closedir(d);
	return found;
}
static void clean(void)
{
	memset(&fault, 0, sizeof(fault));
	DIR *d = opendir(".");
	struct dirent *e;
	while (d && (e = readdir(d)))
		if (e->d_name[0] != '.') remove(e->d_name);
	if (d) closedir(d);
}
static void arm(const char *name, int error, unsigned on, int later)
{
	fault = (typeof(fault)){ .armed = true, .name = name, .error = error, .on = on, .later = later };
}
static const char *kind(unsigned on)
{
	return on == ON_STAT ? "stat" : on == ON_OPEN ? "open" : on == (ON_STAT | ON_OPEN) ? "stat+open" : on == ON_READ ? "read" :
		on == ON_CLOSE ? "close" : "error after the payload";
}
static int load_as(const safe_format_t *fmt, safe_info_t *info)
{
	unsigned char *d = safe_load("card", fmt, 0, info);
	if (!d) return 0;
	int w = !memcmp(d, A, SIZE) ? 'A' : !memcmp(d, B, SIZE) ? 'B' : !memcmp(d, C, SIZE) ? 'C' : !memcmp(d, N, SIZE) ? 'N' : '?';
	free(d);
	return w;
}

static const char *const names[] = { "card", "card.bak", "card.tmp" };
static const int errors[] = { EIO, EACCES };
static const unsigned kinds[] = { ON_STAT, ON_OPEN, ON_STAT | ON_OPEN, ON_READ, ON_CLOSE, ON_TAIL };
#define KINDS (sizeof(kinds) / sizeof(kinds[0]))

// ---------------------------------------------------------------- tests
// A valid sole recovery .tmp, EIO while it is
// probed, then a full card at the next write. It must stay, byte for byte.
static void sole_tmp_probe(void)
{
	clean();
	snprintf(ctx, sizeof(ctx), "review probe: sole tmp EIO then ENOSPC");
	put("card.tmp", C, SIZE);
	safe_info_t info;
	CHECK(load_as(&FMT, &info) == 'C' && info.from == SAFE_TEMP);
	arm("card.tmp", EIO, ON_STAT | ON_OPEN, LATER_FULL);
	bool ok = safe_write("card", &FMT, N, true);
	fault.armed = false;
	CHECK(!ok && content("card.tmp") == 'C');
	CHECK(load_as(&FMT, &info) == 'C' && info.from == SAFE_TEMP);
}

// Loading: an unreadable sole copy is SAFE_UNREADABLE (never ABSENT, never
// DAMAGED); a good copy after it is still used (the fallback policy); a weak
// copy is never chosen over one that could not be read.
static void loads(void)
{
	safe_info_t info;
	clean();
	snprintf(ctx, sizeof(ctx), "nothing there");
	CHECK(load_as(&FMT, &info) == 0 && info.outcome == SAFE_ABSENT && !info.rejected && !info.failed);
	put("card", "MCx", 3);
	snprintf(ctx, sizeof(ctx), "damaged sole copy");
	CHECK(load_as(&FMT, &info) == 0 && info.outcome == SAFE_DAMAGED && info.rejected && !info.failed);
	for (size_t t = 0; t < 3; t++)
		for (size_t e = 0; e < 2; e++)
			for (size_t k = 0; k < KINDS; k++) {
				clean();
				snprintf(ctx, sizeof(ctx), "load: sole %s, %s on %s", names[t], strerror(errors[e]), kind(kinds[k]));
				put(names[t], A, SIZE);
				arm(names[t], errors[e], kinds[k], LATER_NONE);
				CHECK(load_as(&FMT, &info) == 0 && info.outcome == SAFE_UNREADABLE && info.failed && info.rejected);
				fault.armed = false;
				CHECK(content(names[t]) == 'A' && load_as(&FMT, &info) == 'A');
			}
	// <name> unreadable, a good backup: the backup (as for a damaged <name>)
	for (size_t k = 0; k < KINDS; k++) {
		clean();
		snprintf(ctx, sizeof(ctx), "load: card %s error, good backup", kind(kinds[k]));
		put("card", A, SIZE);
		put("card.bak", B, SIZE);
		arm("card", EIO, kinds[k], LATER_NONE);
		CHECK(load_as(&FMT, &info) == 'B' && info.outcome == SAFE_LOADED && info.from == SAFE_BACKUP && info.rejected);
		fault.armed = false;
		CHECK(content("card") == 'A');
	}
	// a weak <name> and an unreadable backup or .tmp: nothing (it may be the good one)
	unsigned char weak[SIZE];
	memcpy(weak, A, SIZE);
	weak[SIZE - 1] = (unsigned char)~xor_of(weak);
	for (size_t t = 1; t < 3; t++)
		for (size_t k = 0; k < KINDS; k++) {
			clean();
			snprintf(ctx, sizeof(ctx), "load: weak card, %s %s error", names[t], kind(kinds[k]));
			unsigned char good[SIZE];
			memcpy(good, B, SIZE);
			good[SIZE - 1] = xor_of(good);
			put("card", weak, SIZE);
			put(names[t], good, SIZE);
			arm(names[t], EIO, kinds[k], LATER_NONE);
			unsigned char *d = safe_load("card", &DEEP, 0, &info);
			CHECK(!d && info.outcome == SAFE_UNREADABLE);
			free(d);
			fault.armed = false;
			d = safe_load("card", &DEEP, 0, &info);
			CHECK(d && !memcmp(d, good, SIZE) && !info.weak);
			free(d);
		}
}

// Writing over copies the card errs on, then a full card, a failed close or
// failed renames: every original byte stays; an unreadable <name> is never
// moved aside or renamed onto; an unreadable sole .tmp is never truncated.
static void writes(void)
{
	for (int hook = 0; hook < 2; hook++)
	for (size_t t = 0; t < 3; t++)
		for (size_t e = 0; e < 2; e++)
			for (size_t k = 0; k < KINDS; k++)
				for (int later = 0; later < LATER_N; later++) {
					const safe_format_t *fmt = hook ? &FMT : &PLAIN;
					clean();
					snprintf(ctx, sizeof(ctx), "write (%s): %s %s on %s, then %s", hook ? "history hook" : "no hook",
						names[t], strerror(errors[e]), kind(kinds[k]), later_name[later]);
					bool sole_tmp = t == 2;
					if (sole_tmp)
						put("card.tmp", C, SIZE);
					else {
						put("card", A, SIZE);
						put("card.bak", B, SIZE);
					}
					arm(names[t], errors[e], kinds[k], later);
					bool ok = safe_write("card", fmt, N, true);
					int renamed_onto = fault.renamed_onto;
					fault.armed = false;
					CHECK(renamed_onto == 0);
					CHECK(!ok || content("card") == 'N');
					// an error only past the header is not seen by a write's header probe:
					// the copy may move by rename (never rewritten), its bytes stay
					bool past_head = kinds[k] == ON_TAIL;
					if (sole_tmp) {
						if (past_head)
							CHECK(kept('C') && (!ok || content("card.bak") == 'C'));
						else
							CHECK(!ok && content("card.tmp") == 'C');
						continue;
					}
					// B is the copy two saves old: without a history hook a write deletes it by
					// policy, once it reads back whole; only an error on B itself must keep it
					CHECK(kept('A'));
					if (hook || t == 1)
						CHECK(kept('B'));
					if (t == 0)   // the newest copy is never buried as .bad by a write
						CHECK(past_head ? (ok ? content("card.bak") == 'A' : content("card") == 'A') : !ok && content("card") == 'A');
					if (t == 1 && !hook)   // nothing keeps a backup the card cannot read: it stays
						CHECK(!ok && content("card.bak") == 'B' && content("card") == 'A');
					safe_info_t info;
					CHECK(load_as(fmt, &info) == (ok ? 'N' : 'A'));
				}
	// a weak <name> beside a good backup the card cannot read: the backup
	// is not pushed out (no history hook here, as for the battery)
	unsigned char weak[SIZE], good[SIZE];
	memcpy(weak, A, SIZE);
	weak[SIZE - 1] = (unsigned char)~xor_of(weak);
	memcpy(good, B, SIZE);
	good[SIZE - 1] = xor_of(good);
	safe_format_t deep_hook = DEEP;
	deep_hook.retire = keep_retired;   // a history hook would keep it, but a weak card must not push it there
	for (int hook = 0; hook < 2; hook++)
	for (size_t k = 0; k < KINDS; k++) {
		clean();
		snprintf(ctx, sizeof(ctx), "write: weak card, backup %s error%s", kind(kinds[k]), hook ? ", history hook" : "");
		put("card", weak, SIZE);
		put("card.bak", good, SIZE);
		arm("card.bak", EIO, kinds[k], LATER_NONE);
		bool ok = safe_write("card", hook ? &deep_hook : &DEEP, N, true);
		fault.armed = false;
		CHECK(!ok);
		FILE *f = __real_fopen("card.bak", "rb");
		unsigned char got[SIZE];
		CHECK(f && fread(got, 1, SIZE, f) == SIZE && !memcmp(got, good, SIZE));
		if (f) fclose(f);
	}
}

// A good backup or .tmp loads after a <name> the card
// cannot read, but repair (and a write's rescue of the .tmp) never sets that
// <name> aside: it may be the newest copy. It stays at its name, byte for byte.
static void repairs(void)
{
	for (int from_tmp = 0; from_tmp < 2; from_tmp++)
		for (size_t k = 0; k < KINDS; k++) {
			clean();
			snprintf(ctx, sizeof(ctx), "repair from %s: card %s error", from_tmp ? "tmp" : "backup", kind(kinds[k]));
			put("card", A, SIZE);
			put(from_tmp ? "card.tmp" : "card.bak", from_tmp ? C : B, SIZE);
			arm("card", EIO, kinds[k], LATER_NONE);
			safe_info_t info;
			unsigned char *d = safe_load("card", &PLAIN, 0, &info);
			CHECK(d && info.outcome == SAFE_LOADED && info.from == (from_tmp ? SAFE_TEMP : SAFE_BACKUP));
			bool repaired = d && safe_repair("card", &PLAIN, d, info.from);
			free(d);
			fault.armed = false;
			CHECK(!repaired && content("card") == 'A' && size_of_bad() < 0);
			CHECK(content(from_tmp ? "card.tmp" : "card.bak") == (from_tmp ? 'C' : 'B'));
			// a write while it still errs: its .tmp is not put over <name> either
			if (from_tmp && kinds[k] != ON_TAIL) {   // (past the header: a write's probe does not see it)
				arm("card", EIO, kinds[k], LATER_NONE);
				bool ok = safe_write("card", &PLAIN, N, true);
				fault.armed = false;
				CHECK(!ok && content("card") == 'A' && content("card.tmp") == 'C' && size_of_bad() < 0);
			}
		}
	// a readable damaged <name> is still set aside by repair (the policy stays)
	clean();
	snprintf(ctx, sizeof(ctx), "repair: readable damaged card");
	put("card", "MC", 2);
	put("card.bak", B, SIZE);
	safe_info_t info;
	unsigned char *d = safe_load("card", &PLAIN, 0, &info);
	CHECK(d && safe_repair("card", &PLAIN, d, info.from) && content("card") == 'B' && size_of_bad() == 2);
	free(d);
}

// An error right after the exact payload (EOF alone does
// not tell) is not a good copy, and a restore's read-back fails on it.
static void tail_errors(void)
{
	clean();
	snprintf(ctx, sizeof(ctx), "error after payload: load");
	put("card", A, SIZE);
	arm("card", EIO, ON_TAIL, LATER_NONE);
	safe_info_t info;
	CHECK(load_as(&PLAIN, &info) == 0 && info.outcome == SAFE_UNREADABLE);
	fault.armed = false;
	clean();
	snprintf(ctx, sizeof(ctx), "error after payload: restore read-back");
	put("card", "MC", 2);   // damaged: the player restores a good copy
	arm("card.tmp", EIO, ON_TAIL, LATER_NONE);
	bool ok = safe_restore("card", &PLAIN, B, NULL, NULL);
	fault.armed = false;
	CHECK(!ok && size_of_bad() < 0);
	FILE *f = __real_fopen("card", "rb");
	char got[3] = "";
	CHECK(f && fread(got, 1, 3, f) == 2 && !memcmp(got, "MC", 2));
	if (f) fclose(f);
}

int main(void)
{
	char dir[4096];
	if (!test_mkdtemp(dir, sizeof(dir), "bb-safefile-io") || chdir(dir)) return 2;
	for (int i = 0; i < SIZE; i++) {
		A[i] = (unsigned char)(i * 7 + 1);
		B[i] = (unsigned char)(i * 13 + 5);
		C[i] = (unsigned char)(i * 3 + 9);
		N[i] = (unsigned char)(i * 11 + 2);
	}
	A[0] = B[0] = C[0] = N[0] = 'M';
	A[1] = B[1] = C[1] = N[1] = 'C';
	sole_tmp_probe();
	loads();
	writes();
	repairs();
	tail_errors();
	clean();
	rmdir(dir);
	printf("SD errors (stat/open/read/close/after-payload x EIO/EACCES x full/close/rename, repair): %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
