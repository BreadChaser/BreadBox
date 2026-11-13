// Crash-safe file replacement for player data (see safefile.h).
//
// Replace steps; a power cut between any two leaves a good copy:
//   1. write <name>.tmp completely and close it (the 3DS FS flushes on close)
//   2. remove <name>.bak                    (<name> still the old good file)
//      (or hand it to fmt->retire, which renames it into the history)
//   3. rename <name> -> <name>.bak          (old good file now the backup)
//      or, if <name> is damaged, or weak while the backup is good: rename it
//      -> <name>.bad, .bad2 ... (the backup stays; safe_park)
//   4. rename <name>.tmp -> <name>          (new file in place)
//   5. text files only: remove <name>.bak
// FAT on the 3DS SD card cannot rename over an existing file, hence 2 and 3.
#include "safefile.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
/* A writer on another OS thread must never enter the importer's private
 * stack. libctru exposes a stable TLS address even for the main thread. */
#ifdef __3DS__
#include <3ds.h>
static void (*io_observer)(const char *,const char *,bool);
static void *checkpoint_thread;
static bool (*writer_checkpoint)(void *);
static void *writer_context;
void safe_set_checkpoint(bool (*fn)(void *),void *ctx)
{
 __atomic_store_n(&checkpoint_thread,NULL,__ATOMIC_RELEASE);
 writer_checkpoint=fn;writer_context=ctx;
 if(fn)__atomic_store_n(&checkpoint_thread,getThreadLocalStorage(),__ATOMIC_RELEASE);
}
bool safe_checkpoint(void)
{return __atomic_load_n(&checkpoint_thread,__ATOMIC_ACQUIRE)!=getThreadLocalStorage() || !writer_checkpoint || writer_checkpoint(writer_context);}
#else
static _Thread_local void (*io_observer)(const char *,const char *,bool);
static _Thread_local bool (*writer_checkpoint)(void *);
static _Thread_local void *writer_context;
void safe_set_checkpoint(bool (*fn)(void *),void *ctx){writer_checkpoint=fn;writer_context=ctx;}
bool safe_checkpoint(void){return !writer_checkpoint || writer_checkpoint(writer_context);}
#endif

void safe_set_io_observer(void (*fn)(const char *,const char *,bool)){io_observer=fn;}
void safe_observe_io(const char *op,const char *path,bool metadata)
{
#ifdef __3DS__
 if(__atomic_load_n(&checkpoint_thread,__ATOMIC_ACQUIRE)!=getThreadLocalStorage())return;
#endif
 if(writer_checkpoint && io_observer)io_observer(op,path,metadata);
}

#define CHUNK (64 * 1024)   // bounded FS calls, so a writer thread yields often
#define HEAD_MAX 256        // largest safe_format_t.head

#ifdef SAFEFILE_TEST
int safe_test_crash_after = -1;   // -2: the power went off, nothing more runs
static bool crash(void)
{
	if (safe_test_crash_after >= 0 && safe_test_crash_after-- == 0)
		safe_test_crash_after = -2;
	return safe_test_crash_after == -2;
}
#define STEP() do { if (crash()) return false; } while (0)
// host tests: FAT renames (never over an existing file) and injected faults
int safe_test_rename(const char *from, const char *to);
size_t safe_test_fwrite(const void *p, size_t size, size_t n, FILE *f);
size_t safe_test_fread(void *p, size_t size, size_t n, FILE *f);
int safe_test_fclose(FILE *f);
#define rename safe_test_rename
#define fwrite safe_test_fwrite
#define fread safe_test_fread
#define fclose safe_test_fclose
#else
#define STEP() do { } while (0)
#endif

/* Rollback/cleanup must run even when cancelled. Take a yielding permit,
 * but ignore its cancellation result so the previous primary is restored. */
static int cleanup_remove(const char *p){(void)safe_checkpoint();safe_observe_io("cleanup:remove",p,true);return remove(p);}
static int rollback_rename(const char *a,const char *b){(void)safe_checkpoint();safe_observe_io("rollback:rename",a,true);return rename(a,b);}
static bool cleanup_present(const char *p){struct stat st;(void)safe_checkpoint();safe_observe_io("cleanup:stat",p,true);return stat(p,&st)==0;}

#ifdef __3DS__
#include <3ds.h>
extern void log_printf(const char *,...);
#define SCHEDULED_IO(call,p) ({safe_observe_io(#call,(p),true);u64 began=svcGetSystemTick();__auto_type rc=(call);u64 elapsed=svcGetSystemTick()-began;int error=errno;if(elapsed>SYSCLOCK_ARM11/20)log_printf("import: slow safefile op=%s path=%s us=%lu errno=%d\n",#call,(p),(unsigned long)(elapsed/(SYSCLOCK_ARM11/1000000)),error);errno=error;rc;})
#else
#define SCHEDULED_IO(call,p) (safe_observe_io(#call,(p),true),(call))
#endif
static int scheduled_stat(const char *p,struct stat *st){if(!safe_checkpoint()){errno=ECANCELED;return -1;}return SCHEDULED_IO(stat(p,st),p);}
static int scheduled_remove(const char *p){if(!safe_checkpoint()){errno=ECANCELED;return -1;}return SCHEDULED_IO(remove(p),p);}
static int scheduled_rename(const char *a,const char *b){if(!safe_checkpoint()){errno=ECANCELED;return -1;}return SCHEDULED_IO(rename(a,b),a);}
static int scheduled_close(FILE *f,const char *path){(void)safe_checkpoint();return SCHEDULED_IO(fclose(f),path);}
static FILE *scheduled_open(const char *p,const char *mode){if(!safe_checkpoint()){errno=ECANCELED;return NULL;}return SCHEDULED_IO(fopen(p,mode),p);}
#undef rename
#undef fclose
#define stat(...) scheduled_stat(__VA_ARGS__)
#define remove scheduled_remove
#define rename scheduled_rename
#define fclose_at(f,p) scheduled_close(f,p)
#define fopen scheduled_open

static void name_with(char *out, size_t len, const char *path, const char *ext)
{
	snprintf(out, len, "%s%s", path, ext);
}

static long file_size(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

#define GOOD 2
#define WEAK 1

// GOOD: path is a good copy (read into data if given), WEAK: it passes the
// header check but not fmt->deep, 0: no such file, -1: there but damaged or
// unreadable
static int read_good(const char *path, const safe_format_t *fmt, void *data)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return file_size(path) < 0 ? 0 : -1;
	unsigned char head[HEAD_MAX];
	void *whole = data;
	if (!whole && fmt->deep)   // the deep check needs everything
		whole = malloc(fmt->size);
	size_t want = whole ? fmt->size : (fmt->head < fmt->size ? fmt->head : fmt->size);
	bool ok = fmt->head <= HEAD_MAX && file_size(path) == (long)fmt->size;
	if (ok && whole)
		ok = fread(whole, 1, want, f) == want && fgetc(f) == EOF;
	else if (ok)
		ok = fread(head, 1, want, f) == want;
	fclose_at(f,path);
	if (ok && fmt->valid && fmt->head)
		ok = fmt->valid(whole ? whole : head);
	int r = ok ? GOOD : -1;
	// no memory for the deep check: can't call it good
	if (ok && fmt->deep && !(whole && fmt->deep(whole, fmt->size)))
		r = WEAK;
	if (whole != data)
		free(whole);
	return r;
}

static bool write_tmp(const char *tmp, const void *data, size_t size)
{
	FILE *f = fopen(tmp, "wb");
	if (!f)
		return false;
	bool ok = true;
	for (size_t at = 0; ok && at < size; at += CHUNK) {
		size_t n = size - at < CHUNK ? size - at : CHUNK;
		ok = fwrite((const char *)data + at, 1, n, f) == n;
	}
	if (fclose_at(f,tmp))
		ok = false;
	return ok && file_size(tmp) == (long)size;
}

// true: nothing is at path (the SD card answered "not there"); false with
// errno: there, or the card could not tell (callers then stop)
static bool absent(const char *path)
{
	struct stat st;
	errno = 0;
	if (!stat(path, &st))
		return false;
	return errno == ENOENT;
}

// A damaged file makes room: renamed to the first free <file>.bad, .bad2 ...
// .bad99. An existing .bad is never removed or written over; a name the SD
// card cannot answer for stops it (the file stays where it is).
bool safe_park(const char *path)
{
	char bad[544];
	if (absent(path))
		return true;
	if (errno)   // there must be no doubt that it is there
		return false;
	for (unsigned n = 1; n < 100; n++) {
		if (n == 1)
			name_with(bad, sizeof(bad), path, ".bad");
		else
			snprintf(bad, sizeof(bad), "%s.bad%u", path, n);
		if (!absent(bad)) {
			if (errno)
				return false;
			continue;
		}
		STEP();   // the name is free: a cut here leaves the file in place
		return rename(path, bad) == 0;
	}
	return false;
}

// the backup is about to be replaced: the history may take it first
static void retire(const char *bak, const safe_format_t *fmt)
{
	if (fmt && fmt->retire && file_size(bak) >= 0)
		fmt->retire(bak, fmt->ctx);
	remove(bak);
}

// steps 2-5: <name>.tmp is complete. fmt NULL: a text file (no backup kept).
static bool put_in_place(const char *path, const char *tmp, const char *bak, const safe_format_t *fmt)
{
	bool moved = false;
	if (file_size(path) >= 0) {
		int cur = fmt ? read_good(path, fmt, NULL) : GOOD;
		// a weak <name> never pushes a good backup out
		if (cur == GOOD || (cur == WEAK && read_good(bak, fmt, NULL) != GOOD)) {
			retire(bak, fmt);
			STEP();
			if (rename(path, bak))
				return false;
			moved = true;
		} else if (!safe_park(path))   // must not replace a good backup
			return false;
		STEP();
	}
	if (rename(tmp, path)) {
		if (moved)
			rollback_rename(bak, path);
		return false;
	}
	if (!fmt) {
		STEP();
		remove(bak);
	}
	return true;
}

// <name>.tmp is the only good copy left (a cut-off first save, a failed
// repair): put it in place before anything writes a new .tmp over it
// A damaged leftover .tmp beside a damaged <name> is kept (.tmp.bad), not
// written over.
static bool rescue_tmp(const char *path, const char *tmp, const char *bak, const safe_format_t *fmt)
{
	if (read_good(path, fmt, NULL) > 0 || read_good(bak, fmt, NULL) > 0)
		return true;
	int t = read_good(tmp, fmt, NULL);
	if (t <= 0)
		return t == 0 || safe_park(tmp);
	return safe_park(path) && rename(tmp, path) == 0;
}

// After a failed put_in_place: the complete .tmp goes only while <name> is
// still there. With <name> already set aside, the .tmp is the newest good
// copy and the next load takes it.
static void drop_tmp(const char *path, const char *tmp)
{
	if (cleanup_present(path))   // also after a cancel (a rollback put <name> back)
		cleanup_remove(tmp);
}

bool safe_write(const char *path, const safe_format_t *fmt, const void *data, bool keep_backup)
{
	char tmp[528], bak[528];
	name_with(tmp, sizeof(tmp), path, ".tmp");
	name_with(bak, sizeof(bak), path, ".bak");
	if (keep_backup && !rescue_tmp(path, tmp, bak, fmt))
		return false;
	if (!write_tmp(tmp, data, fmt->size)) {
		cleanup_remove(tmp);
		return false;
	}
	STEP();
	if (!put_in_place(path, tmp, bak, keep_backup ? fmt : NULL)) {
#ifdef SAFEFILE_TEST
		if (safe_test_crash_after != -2)
#endif
			drop_tmp(path, tmp);
		return false;
	}
	return true;
}

// the .tmp holds exactly data (read back from the SD card)
static bool reads_back(const char *tmp, const void *data, size_t size)
{
	unsigned char buf[4096];
	FILE *f = fopen(tmp, "rb");
	bool ok = f != NULL;
	for (size_t at = 0; ok && at < size; at += sizeof(buf)) {
		size_t n = size - at < sizeof(buf) ? size - at : sizeof(buf);
		ok = fread(buf, 1, n, f) == n && !memcmp(buf, (const char *)data + at, n);
	}
	if (f) {
		ok = ok && fgetc(f) == EOF;
		if (fclose_at(f, tmp))
			ok = false;
	}
	return ok;
}

bool safe_restore(const char *path, const safe_format_t *fmt, const void *data,
	bool (*aside)(const char *path, void *ctx), void *ctx)
{
	char tmp[528], bak[528];
	name_with(tmp, sizeof(tmp), path, ".tmp");
	name_with(bak, sizeof(bak), path, ".bak");
	// no good copy is left beside <name> (else the load had taken it): set
	// the leftovers aside before the copy is written where they are
	if (!safe_park(tmp))
		return false;
	STEP();
	if (read_good(bak, fmt, NULL) != GOOD && !safe_park(bak))
		return false;
	STEP();
	if (!write_tmp(tmp, data, fmt->size) || !reads_back(tmp, data, fmt->size)) {
		cleanup_remove(tmp);   // only the copy; <name> is untouched
		return false;
	}
	STEP();
	if (aside && file_size(path) >= 0) {   // the caller keeps <name> somewhere else
		if (!aside(path, ctx)) {
			drop_tmp(path, tmp);
			return false;
		}
		STEP();
	}
	if (!put_in_place(path, tmp, bak, fmt)) {
#ifdef SAFEFILE_TEST
		if (safe_test_crash_after != -2)
#endif
			drop_tmp(path, tmp);
		return false;
	}
	return true;
}

bool safe_set_aside(const char *path)
{
	static const char *const ext[] = { ".tmp", ".bak", "" };   // <name> last: until then it is asked again
	char name[528];
	for (unsigned i = 0; i < 3; i++) {
		if (i)
			STEP();
		name_with(name, sizeof(name), path, ext[i]);
		if (!safe_park(name))
			return false;
	}
	return true;
}

FILE *safe_begin(const char *path)
{
	char tmp[528];
	name_with(tmp, sizeof(tmp), path, ".tmp");
	return fopen(tmp, "w");
}

bool safe_finish(FILE *f, const char *path, bool ok)
{
	char tmp[528], bak[528];
	name_with(tmp, sizeof(tmp), path, ".tmp");
	name_with(bak, sizeof(bak), path, ".bak");
	if (ferror(f))
		ok = false;
	if (fclose_at(f,path))
		ok = false;
	STEP();
	if (!ok) {
		cleanup_remove(tmp);
		return false;
	}
	if (!put_in_place(path, tmp, bak, NULL)) {
		drop_tmp(path, tmp);
		return false;
	}
	return true;
}

void safe_recover(const char *path)
{
	char tmp[528], bak[528];
	name_with(tmp, sizeof(tmp), path, ".tmp");
	name_with(bak, sizeof(bak), path, ".bak");
	// A text .tmp is never recovery input. With a primary present, leave any
	// abandoned tmp for safe_begin to truncate: probing/removing an absent tmp
	// can take >150 ms on SD, and cover readers call recovery repeatedly.
	if (file_size(path) >= 0)
		return;
	if (errno != ENOENT && errno != ENOTDIR) {
		int error = errno;
#ifdef __3DS__
		log_printf("safe_recover: stopped path=%s errno=%d\n", path, error);
#else
		fprintf(stderr, "safe_recover: stopped path=%s errno=%d\n", path, error);
#endif
		errno = error;
		return;
	}
	if (file_size(bak) < 0 || rename(bak, path))
		return;
	remove(tmp);
}

#ifdef BB_IMPORT_RECOVERY_TEST
/* Explicit diagnostic fixture, excluded from release builds. Never uses an
 * owner filename or the legacy application tree. Refuse preexisting files. */
#ifndef SAFE_RECOVERY_PROBE_PATH
#include "app_policy.h"
#define SAFE_RECOVERY_PROBE_PATH APP_SD_ROOT "/rc5-recovery-probe"
#endif
bool safe_recovery_probe(void)
{
 const char *path=SAFE_RECOVERY_PROBE_PATH;
 char bak[528],tmp[528];struct stat st;
 name_with(bak,sizeof(bak),path,".bak");name_with(tmp,sizeof(tmp),path,".tmp");
 const char *names[]={path,bak,tmp};
 for(unsigned i=0;i<3;i++){errno=0;if(stat(names[i],&st)!=-1 || errno!=ENOENT)return false;}
 FILE *f=fopen(bak,"w");if(!f)return false;
 if(fputs("rc5-recovery",f)<0){fclose_at(f,bak);return false;}
 if(fclose_at(f,bak))return false;
 f=fopen(tmp,"w");if(!f)return false;
 if(fputs("partial",f)<0){fclose_at(f,tmp);return false;}
 if(fclose_at(f,tmp))return false;
 errno=0;int rc=stat(path,&st),missing=errno;
 safe_recover(path);
 f=fopen(path,"r");char data[32]={0};bool ok=f && fgets(data,sizeof(data),f) && !strcmp(data,"rc5-recovery");
 if(f && fclose_at(f,path))ok=false;
 ok=ok && rc==-1 && missing==ENOENT && stat(bak,&st)==-1 && errno==ENOENT && stat(tmp,&st)==-1 && errno==ENOENT;
#ifdef __3DS__
 log_printf("safe_recover: probe missing_rc=%d errno=%d restored=%d path=%s\n",rc,missing,ok,path);
#endif
 if(ok)ok=remove(path)==0;
 return ok;
}
#endif

void *safe_load(const char *path, const safe_format_t *fmt, unsigned skip, safe_info_t *info)
{
	static const char *const ext[] = { [SAFE_CURRENT] = "", [SAFE_BACKUP] = ".bak", [SAFE_TEMP] = ".tmp" };
	safe_info_t dummy;
	if (!info)
		info = &dummy;
	info->from = SAFE_NONE;
	info->rejected = info->failed = info->weak = false;
	void *buf = fmt->size ? malloc(fmt->size) : NULL;
	if (!buf) {
		info->failed = true;
		return NULL;
	}
	int weak = SAFE_NONE;   // the first weak copy: used if none is good
	for (int s = SAFE_CURRENT; s <= SAFE_TEMP; s++) {
		char name[528];
		if (skip & (1u << s))
			continue;
		name_with(name, sizeof(name), path, ext[s]);
		int r = read_good(name, fmt, buf);   // only a good copy is returned
		if (r == GOOD) {
			info->from = (safe_source_t)s;
			info->rejected |= weak != SAFE_NONE;
			return buf;
		}
		if (r == WEAK && weak == SAFE_NONE)
			weak = s;
		else if (r != 0)
			info->rejected = true;
	}
	if (weak != SAFE_NONE) {
		char name[528];
		name_with(name, sizeof(name), path, ext[weak]);
		if (read_good(name, fmt, buf) == WEAK) {
			info->from = (safe_source_t)weak;
			info->weak = true;
			return buf;
		}
		info->rejected = true;
	}
	free(buf);
	return NULL;
}

bool safe_repair(const char *path, const safe_format_t *fmt, const void *data, safe_source_t from)
{
	char src[528];
	if (from == SAFE_TEMP) {
		name_with(src, sizeof(src), path, ".tmp");
		if (!safe_park(path))
			return false;
		STEP();
		return rename(src, path) == 0;
	}
	if (from != SAFE_BACKUP)
		return false;
	// the .bak stays as it is; the copy is staged under its own name
	name_with(src, sizeof(src), path, ".rep");
	if (!write_tmp(src, data, fmt->size) || !safe_park(path)) {
		remove(src);
		return false;
	}
	STEP();
	if (rename(src, path)) {
		remove(src);
		return false;
	}
	return true;
}

bool safe_stat(const char *path, const safe_format_t *fmt, time_t *mtime)
{
	char other[528];
	struct stat st;
	bool found = stat(path, &st) == 0;
	if (!found) {
		name_with(other, sizeof(other), path, ".bak");
		found = stat(other, &st) == 0;
	}
	if (!found) {
		name_with(other, sizeof(other), path, ".tmp");
		found = read_good(other, fmt, NULL) > 0 && stat(other, &st) == 0;
	}
	if (found && mtime)
		*mtime = st.st_mtime;
	return found;
}

void safe_remove(const char *path)
{
	static const char *const ext[] = { "", ".bak", ".tmp", ".rep" };   // a .bad stays
	char other[528];
	for (size_t i = 0; i < sizeof(ext) / sizeof(ext[0]); i++) {
		name_with(other, sizeof(other), path, ext[i]);
		remove(other);
	}
}

/* Shared writer reservations, including callers which do not use safefile. */
#include "../../include/breadbox/bb_writers.h"

/* High bit is the exclusive installer; low bits count reserved writers. */
static unsigned writers;
static unsigned writer_faults;
#define EXCLUSIVE (1u<<31)
bool bb_writer_begin(void)
{
 unsigned old=__atomic_load_n(&writers,__ATOMIC_ACQUIRE);
 do {if(old&EXCLUSIVE)return false;if(old>=EXCLUSIVE-1){__atomic_fetch_add(&writer_faults,1,__ATOMIC_RELAXED);return false;}}
 while(!__atomic_compare_exchange_n(&writers,&old,old+1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));
 return true;
}
void bb_writer_end(void)
{unsigned old=__atomic_load_n(&writers,__ATOMIC_ACQUIRE);do {if(!old || (old&EXCLUSIVE)){__atomic_fetch_add(&writer_faults,1,__ATOMIC_RELAXED);return;}}while(!__atomic_compare_exchange_n(&writers,&old,old-1,false,__ATOMIC_RELEASE,__ATOMIC_ACQUIRE));}
bool bb_writers_busy(void){return __atomic_load_n(&writers,__ATOMIC_ACQUIRE)!=0 || __atomic_load_n(&writer_faults,__ATOMIC_RELAXED)!=0;}
bool bb_writers_try_exclusive(void)
{if(__atomic_load_n(&writer_faults,__ATOMIC_RELAXED))return false;unsigned old=0;return __atomic_compare_exchange_n(&writers,&old,EXCLUSIVE,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);}
void bb_writers_release_exclusive(void)
{unsigned old=EXCLUSIVE;if(!__atomic_compare_exchange_n(&writers,&old,0,false,__ATOMIC_RELEASE,__ATOMIC_ACQUIRE))__atomic_fetch_add(&writer_faults,1,__ATOMIC_RELAXED);}
bool bb_writers_exclusive(void){return (__atomic_load_n(&writers,__ATOMIC_ACQUIRE)&EXCLUSIVE)!=0;}
