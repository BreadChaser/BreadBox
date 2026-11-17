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
#include <time.h>
#include "../../common/test_tmpdir.h"

#define SIZE (4096 + 3)
static unsigned char A[SIZE], B[SIZE], C[SIZE], N[SIZE];
static int failures, checks;
static int retry_left, retry_hits, retry_opens, retry_mode;
static bool retry_ready;
static FILE *retry_stream;
static const char *retry_name="card";
static bool retry_both, retry_wrong_size, retry_vanish, retry_partial;
static unsigned pauses_seen, rename_attempts, opens_seen[3], retry_mask;
static bool retry_each;
static bool retry_target(const char *p)
{
	return !strcmp(p,retry_name) || (retry_both && !strcmp(p,"card.bak"));
}
int __wrap_nanosleep(const struct timespec *pause, struct timespec *remaining)
{
	(void)remaining;
	pauses_seen++;
	if(pause->tv_sec || pause->tv_nsec!=10000000)abort();
	return 0;
}
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
	if(retry_ready && retry_vanish && retry_hits && retry_target(path)){errno=ENOENT;return -1;}
	if(retry_left && retry_mode==ON_STAT && retry_stream && retry_target(path) &&
		(!retry_each || !(retry_mask & (!strcmp(path,"card") ? 1u : 2u)))){
		retry_mask |= !strcmp(path,"card") ? 1u : 2u;
		retry_hits++;if(retry_left>0)retry_left--;errno=ENOENT;return -1;
	}
	if (fault.armed && (fault.on & ON_STAT) && is(path, fault.name)) { errno = fault.error; return -1; }
	int rc=__real_stat(path,st);
	if(!rc && retry_ready && retry_wrong_size && retry_hits && retry_stream && retry_target(path))st->st_size--;
	return rc;
}
FILE *__real_fopen(const char *path, const char *mode);
typedef struct { unsigned char *data; size_t len, pos; int error; } tail_t;
static ssize_t tail_read(void *c, char *p, size_t n)
{
	tail_t *t = c;
	if (t->pos == t->len) {
		if(retry_mode==ON_TAIL){retry_hits++;retry_left=0;}
		errno = t->error; return -1;
	}
	if (n > t->len - t->pos) n = t->len - t->pos;
	memcpy(p, t->data + t->pos, n);
	t->pos += n;
	return (ssize_t)n;
}
static int tail_close(void *c) { tail_t *t = c; free(t->data); free(t); return 0; }
FILE *__wrap_fopen(const char *path, const char *mode)
{
	if(mode[0]=='w' && !strcmp(path,"card.tmp"))retry_ready=true;
 if(mode[0]=='r')for(int i=0;i<3;i++)if(!strcmp(path,(const char *[]){"card","card.bak","card.tmp"}[i]))opens_seen[i]++;
 if(retry_ready && retry_mode && mode[0]=='r' && retry_target(path)) {
  retry_opens++;
  if((retry_mode==ON_OPEN && retry_left) || (retry_vanish && retry_hits)){retry_hits++;if(retry_left>0)retry_left--;errno=ENOENT;return NULL;}
 }
	if (fault.armed && (fault.on & ON_OPEN) && mode[0] == 'r' && is(path, fault.name)) { errno = fault.error; return NULL; }
	if ((fault.armed && (fault.on & ON_TAIL) && mode[0] == 'r' && is(path, fault.name)) ||
		(retry_ready && retry_mode==ON_TAIL && retry_left && mode[0]=='r' && retry_target(path))) {
		FILE *f = __real_fopen(path, "rb");
		if (!f) return NULL;
		tail_t *t = calloc(1, sizeof(*t));
		t->data = malloc(2 * SIZE);
		t->len = fread(t->data, 1, 2 * SIZE, f);
		t->error = retry_mode==ON_TAIL ? ENOENT : fault.error;
		fclose(f);
		return fopencookie(t, "r", (cookie_io_functions_t){ .read = tail_read, .close = tail_close });
	}
	FILE *opened=__real_fopen(path,mode);
	if(opened && retry_ready && retry_mode && mode[0]=='r' && retry_target(path)){retry_stream=opened;}
	return opened;
}
size_t safe_test_fread(void *p, size_t size, size_t n, FILE *f)
{
	if(retry_left && retry_mode==ON_READ && f==retry_stream){
		retry_hits++;if(retry_left>0)retry_left--;
		size_t got=retry_partial ? fread(p,size,n/2,f) : 0;errno=ENOENT;return got;
	}
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
	if(f==retry_stream)retry_stream=NULL;
	bool fail = fault.armed && ((fault.later == LATER_CLOSE && tmp_file(f)) || ((fault.on & ON_CLOSE) && fd_is(f, fault.name)));
	int r = fclose(f);
	if (fail) { errno = EIO; return EOF; }
	return r;
}
int safe_test_rename(const char *from, const char *to)
{
	rename_attempts++;
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
	retry_left=retry_hits=retry_opens=retry_mode=0;
	retry_ready=retry_both=retry_wrong_size=retry_vanish=retry_partial=false;
	retry_stream=NULL;retry_name="card";pauses_seen=rename_attempts=retry_mask=0;retry_each=false;
	memset(opens_seen,0,sizeof(opens_seen));
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

static int cancel_checks;
static bool cancel_retry(void *unused)
{
 (void)unused;
 return !retry_hits || ++cancel_checks<3;
}
static void contradictory_probes(void)
{
 for(int mode=ON_STAT;mode<=ON_READ;mode*=2)
 for(int scenario=0;scenario<3;scenario++) {
  clean();put("card",A,SIZE);put("card.bak",B,SIZE);
  snprintf(ctx,sizeof(ctx),"contradictory %s scenario %d",kind(mode),scenario);
  retry_ready=false;retry_stream=NULL;
  retry_left=scenario==0 ? 1 : -1;retry_hits=retry_opens=0;retry_mode=mode;
  cancel_checks=0;
  if(scenario==2)safe_set_checkpoint(cancel_retry,NULL);
  FILE *log=__real_fopen("retry-log","w+b");CHECK(log!=NULL);
  int saved=dup(STDERR_FILENO);CHECK(saved>=0);
  fflush(stderr);CHECK(dup2(fileno(log),STDERR_FILENO)>=0);
  bool ok=safe_write("card",&PLAIN,N,true);
  fflush(stderr);CHECK(dup2(saved,STDERR_FILENO)>=0);close(saved);
  safe_set_checkpoint(NULL,NULL);
  retry_left=retry_mode=0;
  rewind(log);char line[512]={0};size_t n=fread(line,1,sizeof(line)-1,log);fclose(log);unlink("retry-log");
  if(scenario==0) {
   CHECK(ok && retry_hits==1 && content("card")=='N' && content("card.bak")=='A');
   CHECK(n>0 && strstr(line,"safefile: retry-succeeded") && pauses_seen==1);
   CHECK(strchr(line,'\n') && !strchr(strchr(line,'\n')+1,'\n'));
  } else {
   CHECK(!ok && content("card")=='A' && content("card.bak")=='B');
   CHECK(content("card.tmp")==0);
   CHECK(strstr(line,"safefile: refused step=") && strstr(line,"role=primary"));
   CHECK(strchr(line,'\n') && !strchr(strchr(line,'\n')+1,'\n'));
   if(scenario==1) {
    CHECK(retry_hits==3 && retry_opens==3 && pauses_seen==2);
    CHECK(strstr(line,"errno=2") && strstr(line,mode==ON_STAT ? "step=stat" : mode==ON_OPEN ? "step=open" : "step=read"));
    if(mode==ON_READ)CHECK(strstr(line,"expected=2 read=0"));
   } else {
    CHECK(retry_hits==1 && cancel_checks>=3 && pauses_seen==1);
    CHECK(strstr(line,mode==ON_OPEN ? "step=open" : "step=retry-cancel") && strstr(line,"errno=125"));
   }
  }
 }
}

static void retry_edges(void)
{
	clean();
	snprintf(ctx,sizeof(ctx),"empty slot stays fast");
	CHECK(safe_write("card",&PLAIN,N,true));
	CHECK(pauses_seen==0 && opens_seen[0]==1 && opens_seen[1]==1 && opens_seen[2]==1);
	clean();safe_info_t info;
	CHECK(load_as(&PLAIN,&info)==0 && info.outcome==SAFE_ABSENT);
	CHECK(pauses_seen==0 && opens_seen[0]==1 && opens_seen[1]==1 && opens_seen[2]==1);

	for(int rescue=0;rescue<3;rescue++) {
		clean();put("card",A,SIZE);put("card.bak",B,SIZE);
		snprintf(ctx,sizeof(ctx),"contradiction then wrong size, rescue=%d",rescue);
		if(rescue==1){unlink("card.bak");put("card.tmp",C,SIZE);}
		retry_ready=rescue!=0;
		retry_mode=ON_STAT;retry_left=1;retry_wrong_size=true;
		CHECK(!safe_write("card",&PLAIN,N,true));
		retry_mode=0;retry_wrong_size=false;
		CHECK(content("card")=='A' && content("card.bak")== (rescue==1 ? 0 : 'B'));
		CHECK(content("card.bad")==0 && content("card.tmp")== (rescue==1 ? 'C' : 0));
		CHECK(rename_attempts==0 && pauses_seen==1);
	}

	for(int rescue=0;rescue<2;rescue++) {
		clean();put("card.bak",B,SIZE);
		if(!rescue)put("card",A,SIZE);
		snprintf(ctx,sizeof(ctx),"backup contradiction, rescue=%d",rescue);
		retry_name="card.bak";retry_mode=ON_STAT;retry_left=1;retry_ready=rescue;
		CHECK(safe_write("card",&PLAIN,N,true));
		retry_mode=0;
		CHECK(content("card")=='N' && content("card.bak")== (rescue ? 'B' : 'A'));
		CHECK(pauses_seen==1);
	}

	clean();put("card",A,SIZE);put("card.bak",B,SIZE);
	snprintf(ctx,sizeof(ctx),"one success log for two recovered probes");
	retry_both=retry_each=true;retry_mode=ON_STAT;retry_left=-1;
	FILE *log=__real_fopen("retry-log","w+b");CHECK(log!=NULL);
	int saved=dup(STDERR_FILENO);CHECK(saved>=0);
	fflush(stderr);CHECK(dup2(fileno(log),STDERR_FILENO)>=0);
	CHECK(safe_write("card",&PLAIN,N,true));
	fflush(stderr);CHECK(dup2(saved,STDERR_FILENO)>=0);close(saved);
	rewind(log);char line[512]={0};CHECK(fread(line,1,sizeof(line)-1,log)>0);fclose(log);unlink("retry-log");
	CHECK(pauses_seen==2 && strstr(line,"retry-succeeded") && strstr(line,"pauses=2"));
	CHECK(strchr(line,'\n') && !strchr(strchr(line,'\n')+1,'\n'));

	for(int load=0;load<2;load++) {
		clean();put("card",A,SIZE);put("card.bak",B,SIZE);put("card.tmp",C,SIZE);
		snprintf(ctx,sizeof(ctx),"shared retry bound, load=%d",load);
		retry_both=true;retry_mode=ON_OPEN;retry_left=-1;retry_ready=true;
		if(load)CHECK(load_as(&PLAIN,&info)=='C');
		else CHECK(!safe_write("card",&PLAIN,N,true));
		CHECK(pauses_seen==2);
	}

	clean();put("card",A,SIZE);put("card.bak",B,SIZE);
	snprintf(ctx,sizeof(ctx),"ten-file listing shared bounds");
	retry_both=true;retry_mode=ON_OPEN;retry_left=-1;retry_ready=true;
	for(int i=0;i<10;i++)CHECK(load_as(&PLAIN,&info)==0 && info.outcome==SAFE_UNREADABLE);
	CHECK(pauses_seen==20);

	for(int error=EIO;error<=EACCES;error+=EACCES-EIO) {
		clean();put("card",A,SIZE);arm("card",error,ON_OPEN,LATER_NONE);
		snprintf(ctx,sizeof(ctx),"nontransient open error %d",error);
		CHECK(!safe_write("card",&PLAIN,N,true));
		CHECK(pauses_seen==0);
	}

	for(int load=0;load<2;load++)
	for(int tail=0;tail<2;tail++) {
		clean();unsigned char good[SIZE];memcpy(good,A,SIZE);good[SIZE-1]=xor_of(good);
		put("card",good,SIZE);
		snprintf(ctx,sizeof(ctx),"whole-buffer retry, load=%d tail=%d",load,tail);
		retry_mode=tail ? ON_TAIL : ON_READ;retry_left=1;retry_ready=load;retry_partial=true;
		if(load){void *copy=safe_load("card",&DEEP,0,&info);CHECK(copy && !memcmp(copy,good,SIZE));free(copy);}
		else CHECK(safe_write("card",&DEEP,good,true));
		CHECK(pauses_seen==1);
	}

	clean();put("card.bak",B,SIZE);
	snprintf(ctx,sizeof(ctx),"restore backup contradiction");
	retry_name="card.bak";retry_mode=ON_STAT;retry_left=1;retry_ready=true;
	CHECK(safe_restore("card",&PLAIN,N,NULL,NULL));
	retry_mode=0;CHECK(content("card")=='N' && content("card.bak")=='B' && pauses_seen==1);

	clean();put("card",A,SIZE);put("card.bak",B,SIZE);
	snprintf(ctx,sizeof(ctx),"backup retry then wrong size");
	retry_name="card.bak";retry_mode=ON_STAT;retry_left=1;retry_wrong_size=true;
	CHECK(!safe_write("card",&PLAIN,N,true));
	retry_mode=0;retry_wrong_size=false;
	CHECK(content("card")=='A' && content("card.bak")=='B' && content("card.bad")==0 && content("card.tmp")==0);

	clean();put("card",A,SIZE);put("card.tmp",C,SIZE);
	snprintf(ctx,sizeof(ctx),"seen copy vanishes during rescue");
	retry_mode=ON_STAT;retry_left=1;retry_vanish=true;retry_ready=true;
	CHECK(!safe_write("card",&PLAIN,N,true));
	retry_mode=0;retry_vanish=false;
	CHECK(content("card")=='A' && content("card.tmp")=='C' && rename_attempts==0);

	clean();put("card","bad",3);
	for(int i=1;i<100;i++){char name[64];snprintf(name,sizeof(name),i==1 ? "card.bad" : "card.bad%d",i);put(name,A,SIZE);}
	snprintf(ctx,sizeof(ctx),"full parking names");
	errno=0;CHECK(!safe_park("card") && errno==EEXIST);
	clean();
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
	contradictory_probes();
	retry_edges();
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
