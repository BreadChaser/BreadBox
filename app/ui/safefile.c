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
#ifdef __3DS__
#include "cold_code.h"
#else
#include <time.h>
#ifndef PSX3D_COLD
#define PSX3D_COLD
#endif
#endif
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
typedef struct { const char *op, *role; int error; size_t want, got; unsigned *pauses; } refusal_t;
static PSX3D_COLD void note_failure(refusal_t *why,const char *op,const char *path,int error,size_t want,size_t got)
{
	if(!why)return;
	const char *ext=strrchr(path,'.');
	why->op=op;why->error=error;why->want=want;why->got=got;
	why->role=ext && !strcmp(ext,".bak") ? "backup" :
		ext && (!strcmp(ext,".tmp") || !strcmp(ext,".rep")) ? "temporary" : "primary";
}
static PSX3D_COLD bool refused(const refusal_t *why)
{
	int error=errno;
#ifdef __3DS__
	log_printf("safefile: refused step=%s role=%s errno=%d expected=%lu read=%lu\n",
#else
	fprintf(stderr,"safefile: refused step=%s role=%s errno=%d expected=%lu read=%lu\n",
#endif
		why->op,why->role,why->error,(unsigned long)why->want,(unsigned long)why->got);
	errno=error;return false;
}
static PSX3D_COLD bool retried(const refusal_t *why)
{
	if (!why->pauses || *why->pauses == 2)
		return true;
	int error = errno;
#ifdef __3DS__
	log_printf("safefile: retry-succeeded step=%s role=%s errno=%d pauses=%u\n",
#else
	fprintf(stderr,"safefile: retry-succeeded step=%s role=%s errno=%d pauses=%u\n",
#endif
		why->op, why->role, why->error, 2 - *why->pauses);
	errno = error;
	return true;
}
/* Only contradictory ENOENT probes retry. Each operation shares two 10 ms pauses;
 * checkpoints on both sides of each pause keep cancellation bounded. */
static PSX3D_COLD bool retry_pause(refusal_t *why)
{
	if(!safe_checkpoint()){errno=ECANCELED;return false;}
	if(!why || !why->pauses || !*why->pauses)return false;
	--*why->pauses;
#ifdef __3DS__
	svcSleepThread(10000000LL);
#else
	struct timespec pause={.tv_nsec=10000000};nanosleep(&pause,NULL);
#endif
	if(!safe_checkpoint()){errno=ECANCELED;return false;}
	return true;
}
static PSX3D_COLD int scheduled_stat(const char *p,struct stat *st){if(!safe_checkpoint()){errno=ECANCELED;return -1;}return SCHEDULED_IO(stat(p,st),p);}
static PSX3D_COLD int scheduled_remove(const char *p){if(!safe_checkpoint()){errno=ECANCELED;return -1;}return SCHEDULED_IO(remove(p),p);}
static PSX3D_COLD int scheduled_rename(const char *a,const char *b){if(!safe_checkpoint()){errno=ECANCELED;return -1;}return SCHEDULED_IO(rename(a,b),a);}
static PSX3D_COLD int scheduled_close(FILE *f,const char *path){(void)safe_checkpoint();return SCHEDULED_IO(fclose(f),path);}
static PSX3D_COLD FILE *scheduled_open(const char *p,const char *mode){if(!safe_checkpoint()){errno=ECANCELED;return NULL;}return SCHEDULED_IO(fopen(p,mode),p);}
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

safe_name_t safe_name(const char *path, long *size)
{
	struct stat st;
	errno = 0;
	if (stat(path, &st) == 0) {
		if (size)
			*size = (long)st.st_size;
		return SAFE_NAME_PRESENT;
	}
	return errno == ENOENT ? SAFE_NAME_ABSENT : SAFE_NAME_UNREADABLE;
}

// One copy: GOOD (read into data if given), WEAK (passes the header check but
// not fmt->deep), ABSENT (the SD card said "no such file"), DAMAGED (read, and
// not a copy: wrong size or content) or UNREADABLE (an SD error on the way).
typedef enum { ABSENT, GOOD, WEAK, DAMAGED, UNREADABLE } copy_t;
static bool usable(copy_t c) { return c == GOOD || c == WEAK; }

static PSX3D_COLD copy_t read_copy(const char *path, const safe_format_t *fmt, void *data, refusal_t *why, bool seen)
{
	unsigned retries=0;
	bool contradicted=false;
retry:
	;
	FILE *f=fopen(path,"rb");
	if(!f) {
		int error=errno;
		safe_name_t at=safe_name(path,NULL);
		if(at==SAFE_NAME_ABSENT && !seen)return ABSENT;
		if(error==ENOENT && (seen || at==SAFE_NAME_PRESENT)) {
			seen=true;
			contradicted=true;
			if(retries++<2 && retry_pause(why))goto retry;
			note_failure(why,"open",path,errno==ECANCELED ? ECANCELED : error,0,0);
			return UNREADABLE;
		}
		note_failure(why,"open",path,error,0,0);
		return UNREADABLE;
	}
	seen=true;
	unsigned char head[HEAD_MAX];
	void *whole=data;
	if(!whole && fmt->deep)whole=malloc(fmt->size);
	size_t want=whole ? fmt->size : (fmt->head<fmt->size ? fmt->head : fmt->size);
	long size=-1;
	copy_t r=safe_name(path,&size)!=SAFE_NAME_PRESENT ? UNREADABLE :
		fmt->head>HEAD_MAX || size!=(long)fmt->size ? DAMAGED : GOOD;
	int error=r==UNREADABLE ? errno : 0;
	if(r==UNREADABLE)note_failure(why,"stat",path,error,0,0);
	if(r==GOOD) {
		errno=0;
		size_t got=fread(whole ? whole : head,1,want,f);
		if(got!=want || (whole && fgetc(f)!=EOF) || ferror(f)) {
			error=errno;r=UNREADABLE;note_failure(why,got==want && whole ? "read-tail" : "read",path,error,want,got);
		}
	}
	bool closed=fclose_at(f,path)==0;
	if(!closed){note_failure(why,"close",path,errno,0,0);if(r==GOOD)r=UNREADABLE;}
	if(r==GOOD && fmt->valid && fmt->head && !fmt->valid(whole ? whole : head))r=DAMAGED;
	if(r==GOOD && fmt->deep && !(whole && fmt->deep(whole,fmt->size)))r=WEAK;
	if(whole!=data)free(whole);
	/* Reopen and revalidate from the start; never reuse partial bytes. A close
	 * failure is not retried, even if the earlier probe said not-found. */
	if(r==UNREADABLE && error==ENOENT && closed && retries++<2) {
		contradicted=true;
		if(retry_pause(why))goto retry;
		if(errno==ECANCELED)note_failure(why,"retry-cancel",path,ECANCELED,0,0);
	}
	if((contradicted || (why && why->pauses && *why->pauses<2)) && r==DAMAGED) {
		note_failure(why,"retry-damaged",path,ENOENT,fmt->size,size<0 ? 0 : (size_t)size);
		r=UNREADABLE;
	}
	return r;
}

// The whole file reads back without an SD error (its content is not judged):
// GOOD, ABSENT or UNREADABLE. Asked before a file whose bytes nobody else keeps
// is deleted or moved aside (retire, repair, rescue); never in the frame code.
static PSX3D_COLD copy_t read_through(const char *path, refusal_t *why, bool seen)
{
	unsigned retries=0;
retry:
	;
	FILE *f=fopen(path,"rb");
	if(!f) {
		int error=errno;
		safe_name_t at=safe_name(path,NULL);
		if(at==SAFE_NAME_ABSENT && !seen)return ABSENT;
		note_failure(why,"open",path,error,0,0);
		if(error==ENOENT && (seen || at==SAFE_NAME_PRESENT)) {
			seen=true;
			if(retries++<2 && retry_pause(why))goto retry;
			if(errno==ECANCELED)note_failure(why,"retry-cancel",path,ECANCELED,0,0);
			return UNREADABLE;
		}
		return UNREADABLE;
	}
	seen=true;
	unsigned char buf[4096];
	long size=-1,got=0;
	bool ok=safe_name(path,&size)==SAFE_NAME_PRESENT;
	int error=ok ? 0 : errno;
	if(!ok)note_failure(why,"stat",path,error,0,0);
	if(ok) {
		errno=0;
		for(size_t n;(n=fread(buf,1,sizeof(buf),f))>0;)got+=(long)n;
		ok=!ferror(f) && got==size;
		if(!ok){error=errno;note_failure(why,"read",path,error,(size_t)size,(size_t)got);}
	}
	bool closed=fclose_at(f,path)==0;
	if(!closed){note_failure(why,"close",path,errno,0,0);ok=false;}
	if(!ok && error==ENOENT && closed && retries++<2) {
		if(retry_pause(why))goto retry;
		if(errno==ECANCELED)note_failure(why,"retry-cancel",path,ECANCELED,0,0);
	}
	return ok ? GOOD : UNREADABLE;
}

static PSX3D_COLD bool write_tmp(const char *tmp, const void *data, size_t size, refusal_t *why)
{
	FILE *f = fopen(tmp, "wb");
	if (!f){note_failure(why,"open",tmp,errno,0,0);return false;}
	bool ok = true;
	for (size_t at = 0; ok && at < size; at += CHUNK) {
		size_t n = size - at < CHUNK ? size - at : CHUNK;
		errno=0;
		size_t got=fwrite((const char *)data + at, 1, n, f);
		ok=got==n;
		if(!ok)note_failure(why,"write",tmp,errno,n,got);
	}
	if (fclose_at(f,tmp)){note_failure(why,"close",tmp,errno,0,0);ok=false;}
	long written = -1;
	if(!ok)return false;
	if(safe_name(tmp,&written)!=SAFE_NAME_PRESENT){note_failure(why,"stat",tmp,errno,0,0);return false;}
	if(written!=(long)size){note_failure(why,"size",tmp,0,size,(size_t)written);return false;}
	return true;
}

// A damaged file makes room: renamed to the first free <file>.bad, .bad2 ...
// .bad99. An existing .bad is never removed or written over; a name the SD
// card cannot answer for stops it (the file stays where it is).
bool safe_park(const char *path)
{
	char bad[544];
	safe_name_t at = safe_name(path, NULL);
	if (at != SAFE_NAME_PRESENT)   // there must be no doubt that it is there
		return at == SAFE_NAME_ABSENT;
	for (unsigned n = 1; n < 100; n++) {
		if (n == 1)
			name_with(bad, sizeof(bad), path, ".bad");
		else
			snprintf(bad, sizeof(bad), "%s.bad%u", path, n);
		safe_name_t taken = safe_name(bad, NULL);
		if (taken == SAFE_NAME_UNREADABLE)
			return false;
		if (taken == SAFE_NAME_PRESENT)
			continue;
		STEP();   // the name is free: a cut here leaves the file in place
		return rename(path, bad) == 0;
	}
	errno = EEXIST;
	return false;
}

// <name> is set aside for a good copy only when it reads back whole: a copy the
// SD card cannot read may be the newest one and stays at its name.
static PSX3D_COLD bool park_readable(const char *path, refusal_t *why)
{
	if(read_through(path, why, false)==UNREADABLE)return false;
	if(safe_park(path))return true;
	note_failure(why,"park",path,errno,0,0);return false;
}

// the backup is about to be replaced: the history may take it first; what it
// leaves is deleted only once it has read back whole (an old copy that the SD
// card cannot read may be the last one of those bytes). false: it stays, and
// nothing goes on.
static PSX3D_COLD bool retire(const char *bak, const safe_format_t *fmt, refusal_t *why)
{
	safe_name_t at = safe_name(bak, NULL);
	if (at == SAFE_NAME_UNREADABLE){note_failure(why,"stat",bak,errno,0,0);return false;}
	if (at == SAFE_NAME_PRESENT && fmt && fmt->retire) {
		fmt->retire(bak, fmt->ctx);
		at = safe_name(bak, NULL);
		if (at == SAFE_NAME_UNREADABLE){note_failure(why,"stat",bak,errno,0,0);return false;}
	}
	if (at == SAFE_NAME_ABSENT)
		return true;
	if (read_through(bak, why, true) != GOOD)
		return false;
	if(remove(bak)){note_failure(why,"remove",bak,errno,0,0);return false;}
	return true;
}

// steps 2-5: <name>.tmp is complete. fmt NULL: a text file (no backup kept).
// A <name> or .bak the SD card cannot answer for stops it: nothing is moved.
static PSX3D_COLD bool put_in_place(const char *path, const char *tmp, const char *bak, const safe_format_t *fmt, refusal_t *why)
{
	bool moved = false;
	safe_name_t at = safe_name(path, NULL);
	if (at == SAFE_NAME_UNREADABLE){note_failure(why,"stat",path,errno,0,0);return false;}
	if (at == SAFE_NAME_PRESENT) {
		copy_t cur = fmt ? read_copy(path, fmt, NULL, why, true) : GOOD;
		copy_t old = cur == WEAK ? read_copy(bak, fmt, NULL, why, false) : ABSENT;
		if (cur == UNREADABLE || old == UNREADABLE)
			return false;
		// a weak <name> never pushes a good backup out
		if (cur == GOOD || (cur == WEAK && old != GOOD)) {
			if (!retire(bak, fmt, why))
				return false;
			STEP();
			if (rename(path, bak)){note_failure(why,"rename",path,errno,0,0);return false;}
			moved = true;
		} else if (!safe_park(path)){note_failure(why,"park",path,errno,0,0);return false;}
		STEP();
	}
	if (rename(tmp, path)) {
		note_failure(why,"rename",tmp,errno,0,0);
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
// A .tmp the SD card cannot read may be that copy: it stops the write
// (writing the new .tmp would truncate it).
// A <name> the SD card cannot read is never set aside for it either.
static PSX3D_COLD bool rescue_tmp(const char *path, const char *tmp, const char *bak, const safe_format_t *fmt, refusal_t *why)
{
	refusal_t current={.pauses=why ? why->pauses : NULL};
	copy_t cur = read_copy(path, fmt, NULL, &current, false);
	if(why && current.op)*why=current;
	if (usable(cur) || usable(read_copy(bak, fmt, NULL, why, false)))
		return true;
	copy_t t = read_copy(tmp, fmt, NULL, why, false);
	if (t == ABSENT || t == UNREADABLE)
		return t == ABSENT;
	if (t == DAMAGED){if(safe_park(tmp))return true;note_failure(why,"park",tmp,errno,0,0);return false;}
	if(cur==UNREADABLE){if(why)*why=current;return false;}
	if(!park_readable(path,why))return false;
	if(rename(tmp,path)){note_failure(why,"rename",tmp,errno,0,0);return false;}
	return true;
}

// After a failed put_in_place: the complete .tmp goes only while <name> is
// still there. With <name> already set aside, the .tmp is the newest good
// copy and the next load takes it.
static void drop_tmp(const char *path, const char *tmp)
{
	if (cleanup_present(path))   // also after a cancel (a rollback put <name> back)
		cleanup_remove(tmp);
}

PSX3D_COLD bool safe_write(const char *path, const safe_format_t *fmt, const void *data, bool keep_backup)
{
	unsigned pauses=2;
	refusal_t why={.op="write",.role="temporary",.pauses=&pauses};
	char tmp[528], bak[528];
	name_with(tmp, sizeof(tmp), path, ".tmp");
	name_with(bak, sizeof(bak), path, ".bak");
	if (keep_backup && !rescue_tmp(path, tmp, bak, fmt, &why))
		return refused(&why);
	if (!write_tmp(tmp, data, fmt->size, &why)) {
		refused(&why);
		cleanup_remove(tmp);
		return false;
	}
	STEP();
	if (!put_in_place(path, tmp, bak, keep_backup ? fmt : NULL, &why)) {
#ifdef SAFEFILE_TEST
		if (safe_test_crash_after != -2)
#endif
			drop_tmp(path, tmp);
		return refused(&why);
	}
	return retried(&why);
}

// the .tmp holds exactly data (read back from the SD card)
static PSX3D_COLD bool reads_back(const char *tmp, const void *data, size_t size, refusal_t *why)
{
	unsigned char buf[4096];
	FILE *f = fopen(tmp, "rb");
	bool ok = f != NULL;
	if(!ok)note_failure(why,"open",tmp,errno,0,0);
	for (size_t at = 0; ok && at < size; at += sizeof(buf)) {
		size_t n = size - at < sizeof(buf) ? size - at : sizeof(buf);
		errno=0;size_t got=fread(buf,1,n,f);
		ok=got==n && !memcmp(buf,(const char *)data+at,n);
		if(!ok)note_failure(why,got==n ? "compare" : "read",tmp,errno,n,got);
	}
	if (f) {
		if(ok){errno=0;ok=fgetc(f)==EOF && !ferror(f);
			if(!ok)note_failure(why,"read-tail",tmp,errno,size,size);}
		if (fclose_at(f, tmp)){note_failure(why,"close",tmp,errno,0,0);ok=false;}
	}
	return ok;
}

PSX3D_COLD bool safe_restore(const char *path, const safe_format_t *fmt, const void *data,
	bool (*aside)(const char *path, void *ctx), void *ctx)
{
	unsigned pauses=2;
	refusal_t why={.op="write",.role="temporary",.pauses=&pauses};
	char tmp[528], bak[528];
	name_with(tmp, sizeof(tmp), path, ".tmp");
	name_with(bak, sizeof(bak), path, ".bak");
	// no good copy is left beside <name> (else the load had taken it): set
	// the leftovers aside before the copy is written where they are
	if (!safe_park(tmp)){note_failure(&why,"park",tmp,errno,0,0);return refused(&why);}
	STEP();
	if (read_copy(bak, fmt, NULL, &why, false) != GOOD && !safe_park(bak)){note_failure(&why,"park",bak,errno,0,0);return refused(&why);}
	STEP();
	if (!write_tmp(tmp, data, fmt->size, &why) || !reads_back(tmp, data, fmt->size, &why)) {
		refused(&why);
		cleanup_remove(tmp);   // only the copy; <name> is untouched
		return false;
	}
	STEP();
	safe_name_t at = safe_name(path, NULL);
	if (at == SAFE_NAME_UNREADABLE) {
		note_failure(&why,"stat",path,errno,0,0);
		drop_tmp(path, tmp);
		return refused(&why);
	}
	if (aside && at == SAFE_NAME_PRESENT) {   // the caller keeps <name> somewhere else
		if (!aside(path, ctx)) {
			note_failure(&why,"aside",path,errno,0,0);
			drop_tmp(path, tmp);
			return refused(&why);
		}
		STEP();
	}
	if (!put_in_place(path, tmp, bak, fmt, &why)) {
#ifdef SAFEFILE_TEST
		if (safe_test_crash_after != -2)
#endif
			drop_tmp(path, tmp);
		return refused(&why);
	}
	return retried(&why);
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

PSX3D_COLD bool safe_finish(FILE *f, const char *path, bool ok)
{
	unsigned pauses=2;
	refusal_t why={.op="write",.role="temporary",.pauses=&pauses};
	char tmp[528], bak[528];
	name_with(tmp, sizeof(tmp), path, ".tmp");
	name_with(bak, sizeof(bak), path, ".bak");
	if (ferror(f)){note_failure(&why,"write",tmp,errno,0,0);ok=false;}
	if (fclose_at(f,path)){note_failure(&why,"close",tmp,errno,0,0);ok=false;}
	STEP();
	if (!ok) {
		refused(&why);
		cleanup_remove(tmp);
		return false;
	}
	if (!put_in_place(path, tmp, bak, NULL, &why)) {
		drop_tmp(path, tmp);
		return refused(&why);
	}
	return retried(&why);
}

void safe_recover(const char *path)
{
	char tmp[528], bak[528];
	name_with(tmp, sizeof(tmp), path, ".tmp");
	name_with(bak, sizeof(bak), path, ".bak");
	// A text .tmp is never recovery input. With a primary present, leave any
	// abandoned tmp for safe_begin to truncate: probing/removing an absent tmp
	// can take >150 ms on SD, and cover readers call recovery repeatedly.
	safe_name_t at = safe_name(path, NULL);
	if (at == SAFE_NAME_PRESENT)
		return;
	if (at == SAFE_NAME_UNREADABLE && errno != ENOTDIR) {
		int error = errno;
#ifdef __3DS__
		log_printf("safe_recover: stopped path=%s errno=%d\n", path, error);
#else
		fprintf(stderr, "safe_recover: stopped path=%s errno=%d\n", path, error);
#endif
		errno = error;
		return;
	}
	if (safe_name(bak, NULL) != SAFE_NAME_PRESENT || rename(bak, path))
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
	unsigned pauses=2;
	refusal_t why={.pauses=&pauses};
	static const char *const ext[] = { [SAFE_CURRENT] = "", [SAFE_BACKUP] = ".bak", [SAFE_TEMP] = ".tmp" };
	safe_info_t dummy;
	if (!info)
		info = &dummy;
	*info = (safe_info_t){ .outcome = SAFE_NO_MEMORY, .from = SAFE_NONE, .failed = true };
	void *buf = fmt->size ? malloc(fmt->size) : NULL;
	if (!buf)
		return NULL;
	int weak = SAFE_NONE;   // the first weak copy: used if none is good
	bool damaged = false, unreadable = false;
	for (int s = SAFE_CURRENT; s <= SAFE_TEMP; s++) {
		char name[528];
		if (skip & (1u << s))
			continue;
		name_with(name, sizeof(name), path, ext[s]);
		copy_t r = read_copy(name, fmt, buf, &why, false);   // only a good copy is returned
		if (r == GOOD) {
			*info = (safe_info_t){ .outcome = SAFE_LOADED, .from = (safe_source_t)s,
				.rejected = damaged || unreadable || weak != SAFE_NONE };
			return buf;
		}
		if (r == WEAK && weak == SAFE_NONE)
			weak = s;
		else if (r != ABSENT) {
			damaged |= r != UNREADABLE;
			unreadable |= r == UNREADABLE;
		}
	}
	// a copy that could not be read may be the good one: no weak copy over it
	if (weak != SAFE_NONE && !unreadable) {
		char name[528];
		name_with(name, sizeof(name), path, ext[weak]);
		copy_t r = read_copy(name, fmt, buf, &why, false);
		if (r == WEAK) {
			*info = (safe_info_t){ .outcome = SAFE_LOADED, .from = (safe_source_t)weak, .rejected = damaged, .weak = true };
			return buf;
		}
		unreadable |= r == UNREADABLE;
	}
	damaged |= weak != SAFE_NONE;
	*info = (safe_info_t){ .outcome = unreadable ? SAFE_UNREADABLE : damaged ? SAFE_DAMAGED : SAFE_ABSENT,
		.from = SAFE_NONE, .rejected = damaged || unreadable, .failed = unreadable };
	free(buf);
	return NULL;
}

bool safe_repair(const char *path, const safe_format_t *fmt, const void *data, safe_source_t from)
{
	unsigned pauses=2;
	refusal_t why={.pauses=&pauses};
	char src[528];
	if (from == SAFE_TEMP) {
		name_with(src, sizeof(src), path, ".tmp");
		if (!park_readable(path,&why))
			return false;
		STEP();
		return rename(src, path) == 0;
	}
	if (from != SAFE_BACKUP)
		return false;
	// the .bak stays as it is; the copy is staged under its own name
	name_with(src, sizeof(src), path, ".rep");
	if (!write_tmp(src, data, fmt->size, &why) || !park_readable(path,&why)) {
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
	unsigned pauses=2;
	refusal_t why={.pauses=&pauses};
	char other[528];
	struct stat st;
	bool unsure = false;
	errno = 0;
	bool found = stat(path, &st) == 0;
	unsure |= !found && errno != ENOENT;
	if (!found) {
		name_with(other, sizeof(other), path, ".bak");
		errno = 0;
		found = stat(other, &st) == 0;
		unsure |= !found && errno != ENOENT;
	}
	if (!found) {
		name_with(other, sizeof(other), path, ".tmp");
		copy_t t = read_copy(other, fmt, NULL, &why, false);
		found = usable(t) && stat(other, &st) == 0;
		unsure |= !found && t != ABSENT && t != DAMAGED;
	}
	if (mtime)
		*mtime = found ? st.st_mtime : 0;
	return found || unsure;
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
