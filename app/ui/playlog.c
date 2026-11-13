#include "bb_writers.h"
// Play history and favourites for the library (LIBRARY-DESIGN.md section 7 in
// the UI mock). Both are keyed by a game's save name, so a moved or renamed
// folder keeps its history and its star.
//   saves/playlog.ini     [<save name>] last=<unix time> seconds=<play time>
//                         day=<unix day> today=<play time that day> (the room's Quit card; older builds skip them)
//   saves/favourites.ini  one save name per line
// Written through safefile (replace, never half a file), the play log once at
// the end of each game.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <stddef.h>
#include <sys/types.h>
#include "safefile.h"
#include "app_paths.h"
#include "cold_code.h"
#include "app_log.h"
#include "playlog.h"
#include "safefile.h"
#include "bb_save.h"
#include "bb_index.h"

#define PLAYLOG_FILE PSX3D_SAVE_DIR "/playlog.ini"
#define FAVS_FILE PSX3D_SAVE_DIR "/favourites.ini"
#define MAX_LOG 1024
#define NAME_LEN 96

typedef struct { char name[NAME_LEN]; u32 last, seconds, day, today; } logent_t;

static logent_t *log_ents;
static int nlog;
static char (*favs)[NAME_LEN];
static int nfavs;
static bool loaded;
// The tables hold what the files hold plus room for new games (grown in steps if ever
// needed): at 1024 entries each they were 200 KB of heap kept through every game.
#define TABLE_SPARE 48
static void pl_lock_init(void);   // the writer's table lock (below)
static int cap_log, cap_favs;
static int lines_of(const char *path, bool sections)
{
	int n = 0;
	char line[160];
	FILE *f = fopen(path, "r");
	while (f && fgets(line, sizeof(line), f)) n += sections ? line[0] == '[' : line[0] > ' ';
	if (f) fclose(f);
	return n;
}
static bool grow(void *table, int *cap, size_t size)
{
	if (*cap >= MAX_LOG) return false;
	int want = *cap + TABLE_SPARE > MAX_LOG ? MAX_LOG : *cap + TABLE_SPARE;
	void *p = realloc(*(void **)table, (size_t)want * size);
	if (!p) return false;
	*(void **)table = p;
	*cap = want;
	return true;
}
#define GROW_LOG() grow(&log_ents, &cap_log, sizeof(*log_ents))
#define GROW_FAVS() grow(&favs, &cap_favs, sizeof(*favs))

static void trim(char *s)
{
	size_t n = strlen(s);
	while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' '))
		s[--n] = 0;
}

/* Existing PS1 clients use legacy save names. New adapters supply id:key.
 * Keep this boundary out of frame code and preserve PSX3D's file format. */
static const char *metadata_key(const char *key,char out[NAME_LEN])
{
 if(key && *key && !strchr(key,':')) {snprintf(out,NAME_LEN,"ps1:%.*s",NAME_LEN-5,key);return out;}
 return key?key:"";
}

static logent_t *find(const char *name, bool add)
{
	for (int i = 0; i < nlog; i++)
		if (!strcmp(log_ents[i].name, name))
			return &log_ents[i];
	if (!add || (nlog >= cap_log && !GROW_LOG()))
		return NULL;
	logent_t *e = &log_ents[nlog++];
	memset(e, 0, sizeof(*e));
	snprintf(e->name, sizeof(e->name), "%s", name);
	return e;
}

static void load(void)
{
	if (loaded)
		return;
	loaded = true;
	pl_lock_init();
	safe_recover(PLAYLOG_FILE);
	safe_recover(FAVS_FILE);
	cap_log = lines_of(PLAYLOG_FILE, true) + TABLE_SPARE;
	cap_favs = lines_of(FAVS_FILE, false) + TABLE_SPARE;
	if (cap_log > MAX_LOG) cap_log = MAX_LOG;
	if (cap_favs > MAX_LOG) cap_favs = MAX_LOG;
	log_ents = calloc(cap_log, sizeof(*log_ents));
	favs = calloc(cap_favs, sizeof(*favs));
	if (!log_ents || !favs)
		return;
	char line[160];
	FILE *f = fopen(PLAYLOG_FILE, "r");
	logent_t *cur = NULL;
	while (f && fgets(line, sizeof(line), f)) {
		trim(line);
		if (line[0] == '[') {
			char *end = strrchr(line, ']');
			if (end) {
				*end = 0;
				char scoped[NAME_LEN];cur = find(metadata_key(line+1,scoped), true);
			}
		} else if (cur && !strncmp(line, "last=", 5))
			cur->last = (u32)strtoul(line + 5, NULL, 10);
		else if (cur && !strncmp(line, "seconds=", 8))
			cur->seconds = (u32)strtoul(line + 8, NULL, 10);
		else if (cur && !strncmp(line, "day=", 4))
			cur->day = (u32)strtoul(line + 4, NULL, 10);
		else if (cur && !strncmp(line, "today=", 6))
			cur->today = (u32)strtoul(line + 6, NULL, 10);
	}
	if (f)
		fclose(f);
	f = fopen(FAVS_FILE, "r");
	while (f && (nfavs < cap_favs || GROW_FAVS()) && fgets(line, sizeof(line), f)) {
		trim(line);
		if (line[0])
			{char scoped[NAME_LEN];snprintf(favs[nfavs++], NAME_LEN, "%.*s", NAME_LEN-1, metadata_key(line,scoped));}
	}
	if (f)
		fclose(f);
}


// One ordered owner for the play log, the favourites and the migration markers: the later
// thread (bb_save.h) writes them, one job at a time, each taking its copy of the tables when it
// runs, so whatever is written last is the newest state; a migration's marker is written only
// after a log that holds the migrated entry, and every marker file is touched by that job only
// (the menu thread looks at a marker only for a game it has not queued one for, pl_migrated).
// Pending markers are a list that grows; a marker leaves it only once written, so one that
// could not be written stays for the next job. Asks that come while a job waits to start join
// it. The menu thread changes the tables under pl_lock, the job copies them under it; no file
// I/O under the lock.
#ifdef __3DS__
static LightLock pl_lock;
static void pl_take(void) { LightLock_Lock(&pl_lock); }
static void pl_give(void) { LightLock_Unlock(&pl_lock); }
static void pl_lock_init(void) { LightLock_Init(&pl_lock); }
#else
#include <pthread.h>
static pthread_mutex_t pl_lock = PTHREAD_MUTEX_INITIALIZER;
static void pl_take(void) { pthread_mutex_lock(&pl_lock); }
static void pl_give(void) { pthread_mutex_unlock(&pl_lock); }
static void pl_lock_init(void) {}
#endif
typedef char pl_path[200];
static int pl_queued;
static unsigned pl_want;             // 1 the log, 2 the favourites
static pl_path *pl_marks;            // markers to write after them (grows)
static int pl_nmarks, pl_capmarks;
static char (*pl_done)[NAME_LEN];    // games whose marker is queued or written this session (menu thread only)
static int pl_ndone, pl_capdone;
static bool pl_room(void)            // pl_lock held: space for one more pending marker
{
	if (pl_nmarks < pl_capmarks) return true;
	int cap = pl_capmarks ? pl_capmarks * 2 : 16;
	pl_path *m = realloc(pl_marks, sizeof(*m) * cap);
	if (!m) return false;
	pl_marks = m;
	pl_capmarks = cap;
	return true;
}
static bool pl_migrated(const char *next)
{
	for (int i = 0; i < pl_ndone; i++)
		if (!strcmp(pl_done[i], next)) return true;
	return false;
}
static bool pl_note_migrated(const char *next)
{
	if (pl_ndone == pl_capdone) {
		int cap = pl_capdone ? pl_capdone * 2 : 16;
		char (*d)[NAME_LEN] = realloc(pl_done, sizeof(*d) * cap);
		if (!d) return false;
		pl_done = d;
		pl_capdone = cap;
	}
	snprintf(pl_done[pl_ndone++], NAME_LEN, "%s", next);
	return true;
}
static char *pl_log_text(size_t *len)
{
	size_t cap = (size_t)nlog * (NAME_LEN + 96) + 1, n = 0;
	char *t = malloc(cap);
	for (int i = 0; t && i < nlog && n < cap; i++) {
		n += (size_t)snprintf(t + n, cap - n, "[%s]\nlast=%lu\nseconds=%lu\n", log_ents[i].name,
			(unsigned long)log_ents[i].last, (unsigned long)log_ents[i].seconds);
		if (log_ents[i].day && n < cap)
			n += (size_t)snprintf(t + n, cap - n, "day=%lu\ntoday=%lu\n", (unsigned long)log_ents[i].day, (unsigned long)log_ents[i].today);
	}
	*len = n;
	return t;
}
static char *pl_favs_text(size_t *len)
{
	size_t cap = (size_t)nfavs * (NAME_LEN + 2) + 1, n = 0;
	char *t = malloc(cap);
	for (int i = 0; t && i < nfavs && n < cap; i++)
		n += (size_t)snprintf(t + n, cap - n, "%s\n", favs[i]);
	*len = n;
	return t;
}
static bool pl_write_text(const char *path, const char *text, size_t len)
{
	if(!bb_writer_begin())return false;
	FILE *f = safe_begin(path);
	bool ok = f != NULL;
 for(size_t at=0;ok && at<len;) {
  size_t n=len-at>65536?65536:len-at;
  ok=safe_checkpoint() && (safe_observe_io("playlog:fwrite",path,false),fwrite(text+at,1,n,f)==n);at+=n;
 }
 if(f)ok=safe_finish(f,path,ok);
	bb_writer_end();
	if (!ok) log_printf("playlog: could not write %s\n", path);
	return ok;
}
static void pl_run(void *unused)
{
	(void)unused;
	pl_take();
	__atomic_store_n(&pl_queued, 0, __ATOMIC_RELEASE);   // asks from now on: another job
	unsigned want = pl_want;
	size_t log_len = 0, favs_len = 0;
	char *log_text = want & 1 ? pl_log_text(&log_len) : NULL, *favs_text = want & 2 ? pl_favs_text(&favs_len) : NULL;
	int nmarks = pl_nmarks;   // the markers asked for so far: after this log (later ones wait)
	bool copied = (!(want & 1) || log_text) && (!(want & 2) || favs_text);
	if (copied) pl_want = 0;
	pl_give();
	if (!copied) {   // no memory for a copy: everything stays asked for, the next ask tries again
		log_printf("playlog: no memory to write it now, later\n");
		free(log_text);
		free(favs_text);
		return;
	}
	bool ok = (!log_text || pl_write_text(PLAYLOG_FILE, log_text, log_len)) && (!favs_text || pl_write_text(FAVS_FILE, favs_text, favs_len));
	free(log_text);
	free(favs_text);
	// The markers stay in the list until written: a failure needs no memory to keep them. Only
	// this job takes them out (one job at a time) and asks only add at the end, so the first
	// nmarks are still the ones this job saw; each is copied out under the lock (the list may
	// grow and move meanwhile)
	int written = 0;
	for (; ok && written < nmarks; written++) {
		pl_path marker;
		pl_take();
		memcpy(marker, pl_marks[written], sizeof(marker));
		pl_give();
		safe_format_t fmt = { .size = 1 };
		safe_recover(marker);   // its own file: only this job touches it
		ok = safe_write(marker, &fmt, "1", true);
		if (!ok) break;
	}
	pl_take();
	if (!ok) pl_want |= want;   // what was not written stays asked for (the next ask writes it)
	if (written) {
		memmove(pl_marks, pl_marks + written, sizeof(*pl_marks) * (pl_nmarks - written));
		pl_nmarks -= written;
	}
	pl_give();
}
// false: no room to keep a marker (the migration is not done now: the next library open)
static bool pl_ask(unsigned what, const char *marker)
{
	pl_take();
	bool ok = !marker || pl_room();
	if (ok) {
		pl_want |= what;
		if (marker) snprintf(pl_marks[pl_nmarks++], sizeof(pl_path), "%s", marker);
	}
	pl_give();
	if (ok && !__atomic_exchange_n(&pl_queued, 1, __ATOMIC_ACQ_REL))
		bb_later(pl_run, NULL);
	return ok;
}
// what a failed job left asked for (log, favourites, markers): written now (exit, tests)
bool playlog_pending(void)
{pl_take();bool pending=pl_want || pl_nmarks || __atomic_load_n(&pl_queued,__ATOMIC_ACQUIRE);pl_give();return pending;}
void playlog_flush(void)
{
	load();
	pl_take();
	bool any = pl_want || pl_nmarks;
	pl_give();
	if (any) pl_ask(0, NULL);
}
static bool save_log(void) { return pl_ask(1, NULL); }
static bool save_favs(void) { return pl_ask(2, NULL); }
#define PL_TAKE() pl_take()
#define PL_GIVE() pl_give()

// Load the tables now, at start: allocated lazily during a game they would sit in
// the middle of the heap for good and split the space the next system needs.
void playlog_init(void)
{
	load();
}

u32 playlog_last(const char *name)
{
 char scoped[NAME_LEN];name=metadata_key(name,scoped);
	load();
	logent_t *e = log_ents ? find(name, false) : NULL;
	return e ? e->last : 0;
}

u32 playlog_seconds(const char *name)
{
 char scoped[NAME_LEN];name=metadata_key(name,scoped);
	load();
	logent_t *e = log_ents ? find(name, false) : NULL;
	return e ? e->seconds : 0;
}

// play time today over every game (now: unix time, the console's local clock)
u32 playlog_today(u32 now)
{
	load();
	u32 total = 0;
	for (int i = 0; log_ents && i < nlog; i++)
		if (log_ents[i].day == PLAYLOG_DAY(now))
			total += log_ents[i].today;
	return total;
}

int playlog_favourite_count(void)
{
	load();
	return nfavs;
}

bool playlog_favourite(const char *name)
{
 char scoped[NAME_LEN];name=metadata_key(name,scoped);
	load();
	for (int i = 0; i < nfavs; i++)
		if (!strcmp(favs[i], name))
			return true;
	return false;
}

bool playlog_toggle_favourite(const char *name)
{
 char scoped[NAME_LEN];name=metadata_key(name,scoped);
	load();
	if (!favs)
		return false;
	PL_TAKE();
	for (int i = 0; i < nfavs; i++)
		if (!strcmp(favs[i], name)) {
			memmove(favs[i], favs[i + 1], (size_t)(nfavs - i - 1) * NAME_LEN);
			nfavs--;
			PL_GIVE();
			save_favs();
			return false;
		}
	if (nfavs < cap_favs || GROW_FAVS())
		snprintf(favs[nfavs++], NAME_LEN, "%s", name);
	PL_GIVE();
	save_favs();
	return true;
}

// ---------------------------------------------------------------- a game's time
// Only time the game runs in the foreground counts: the pause menu and the
// HOME Menu / sleep stop the clock.

static char cur_name[NAME_LEN];
static u64 run_since;     // tick the clock last started, 0 while stopped
static u64 run_ticks;
static int stops;         // reasons the clock is stopped (menu, system)

void playlog_begin(const char *name)
{
 char scoped[NAME_LEN];name=metadata_key(name,scoped);
	load();
	snprintf(cur_name, sizeof(cur_name), "%s", name ? name : "");
	run_ticks = 0;
	stops = 0;
	run_since = svcGetSystemTick();
}

void playlog_pause(bool paused)
{
	if (!cur_name[0])
		return;
	if (paused) {
		if (stops++ == 0 && run_since) {
			run_ticks += svcGetSystemTick() - run_since;
			run_since = 0;
		}
	} else if (stops > 0 && --stops == 0)
		run_since = svcGetSystemTick();
}

// the running game's play time so far, this session included (save postcards)
u32 playlog_played(void)
{
	if (!cur_name[0] || !log_ents)
		return 0;
	logent_t *e = find(cur_name, false);
	u64 t = run_ticks + (run_since ? svcGetSystemTick() - run_since : 0);
	return (e ? e->seconds : 0) + (u32)(t / SYSCLOCK_ARM11);
}

void playlog_end(void)
{
	if (!cur_name[0] || !log_ents)
		return;
	if (run_since)
		run_ticks += svcGetSystemTick() - run_since;
	run_since = 0;
	PL_TAKE();
	logent_t *e = find(cur_name, true);
	if (e) {
		u32 now = (u32)time(NULL), played = (u32)(run_ticks / SYSCLOCK_ARM11);
		e->last = now;
		e->seconds += played;
		if (e->day != PLAYLOG_DAY(now)) { e->day = PLAYLOG_DAY(now); e->today = 0; }   // a session counts on the day it ends
		e->today += played;
	}
	PL_GIVE();
	if (e)
		save_log();   // after the lock: the writer job takes it too
	cur_name[0] = 0;
}

void playlog_drop(void)
{
	cur_name[0] = 0;
	run_since = run_ticks = 0;
}

/* One-time GBA identity migration. Keep the legacy entries for recovery;
 * the durable marker also remembers an explicitly unstarred new identity. */
static void adopt_key(const char *old, const char *next, bool checked)
{
    if(!old || !*old || !next || !*next)return;
    char next_scoped[NAME_LEN];next=metadata_key(next,next_scoped);
    // nothing to adopt (most games): no SD access. The library calls this twice
    // per game on every open; the marker check cost two SD lookups each.
    load();if(!log_ents || !favs)return;
    {char scoped[NAME_LEN];const char *o=metadata_key(old,scoped);if(!find(o,false) && !playlog_favourite(o))return;}
    char marker[192];struct stat st;
    snprintf(marker,sizeof(marker),PSX3D_SAVE_DIR "/%s.playlog-id",next);
    for(char *p=marker+strlen(PSX3D_SAVE_DIR)+1;*p;p++)if(*p==':')*p='_';
    if(pl_migrated(next))return;   /* queued or written this session: its marker is the writer's */
    if(!checked) {safe_recover(marker); if(!stat(marker,&st))return;}
    load();if(!log_ents || !favs)return;
    char scoped[NAME_LEN];old=metadata_key(old,scoped);
    logent_t *src=find(old,false);
    bool favourite=playlog_favourite(old);
    if(!src && !favourite)return;
    PL_TAKE();
    logent_t *dst=src?find(next,true):NULL;
    if(src && dst && !dst->last && !dst->seconds) {
        dst->last=src->last;dst->seconds=src->seconds;
    }
    PL_GIVE();
    if(src && !dst)return;
    if(favourite && !playlog_favourite(next))playlog_toggle_favourite(next);
    if(favourite && !playlog_favourite(next))return;
    /* remembered first (from now on the marker is the writer's), then asked for: the marker
     * after a log holding the entry; no memory for either: not now, the next library open */
    if(!pl_note_migrated(next))return;
    if(!pl_ask((src?1:0)|(favourite?2:0),marker))pl_ndone--;
    return;
    if(src && (!dst || !save_log()))return;
    if(favourite && !playlog_favourite(next)) {
        playlog_toggle_favourite(next);
    }
    if(favourite && (!playlog_favourite(next) || !save_favs()))return;
    safe_format_t fmt={.size=1};safe_write(marker,&fmt,"1",true);
}

void playlog_adopt_key(const char *old, const char *next) { adopt_key(old,next,false); }

/* The list must never recover/stat one marker per game on the menu thread.
 * Only marker I/O runs on the index worker; table changes remain on the UI
 * thread. Pending nodes are protected by pl_lock; done is the handoff. */
typedef struct adoption {
 struct adoption *next;
 char old[NAME_LEN], key[NAME_LEN];
 int state; /* 0 queued, 1 reading, 2 absent, 3 already migrated */
} adoption;
static adoption *adoptions;
static bool adoption_queued;
static void adoption_read(void *unused)
{
 (void)unused;
 pl_take();adoption *a=adoptions;
 while(a && a->state)a=a->next;
 if(!a || bb_index_cancelled()){adoption_queued=false;pl_give();return;}
 a->state=1;pl_give();
 char marker[192];struct stat st;
 snprintf(marker,sizeof(marker),PSX3D_SAVE_DIR "/%s.playlog-id",a->key);
 for(char *p=marker+strlen(PSX3D_SAVE_DIR)+1;*p;p++)if(*p==':')*p='_';
 u64 start=svcGetSystemTick();
 safe_recover(marker);u64 recovered=svcGetSystemTick();
 bool exists=false;
 if(!bb_index_cancelled())exists=!stat(marker,&st);
 u64 end=svcGetSystemTick();
 log_printf("playlog: marker-check recover_ms=%llu stat_ms=%llu present=%d\n",
  (unsigned long long)((recovered-start)/(SYSCLOCK_ARM11/1000)),
  (unsigned long long)((end-recovered)/(SYSCLOCK_ARM11/1000)),exists);
 pl_take();a->state=bb_index_cancelled()?0:exists?3:2;adoption_queued=false;pl_give();
}

void playlog_adopt_key_later(const char *old,const char *next)
{
 if(!old || !*old || !next || !*next)return;
 load();if(!log_ents || !favs || pl_migrated(next))return;
 char scoped[NAME_LEN];const char *key=metadata_key(old,scoped);
 if(!find(key,false) && !playlog_favourite(key))return;
 if(find(next,false) || playlog_favourite(next))return;
 pl_take();
 for(adoption *a=adoptions;a;a=a->next)
  if(!strcmp(a->key,next)){pl_give();return;}
 adoption *a=calloc(1,sizeof(*a));
 if(!a){pl_give();return;}
 snprintf(a->old,sizeof(a->old),"%s",old);snprintf(a->key,sizeof(a->key),"%s",next);
 a->next=adoptions;adoptions=a;
 bool ask=!adoption_queued;adoption_queued=true;pl_give();
 if(ask && !bb_index_defer_low(adoption_read,NULL)) {
  pl_take();adoption_queued=false;pl_give();
 }
}
bool playlog_poll_adoptions(void)
{
 bool changed=false;
 pl_take();bool retry=false;
 if(!adoption_queued)for(adoption *a=adoptions;a;a=a->next)if(!a->state){retry=true;break;}
 if(retry)adoption_queued=true;
 pl_give();
 if(retry && !bb_index_defer_low(adoption_read,NULL)) {
  pl_take();adoption_queued=false;pl_give();
 }
 for(int budget=0;budget<1;budget++) {
  pl_take();adoption **at=&adoptions;
  while(*at && (*at)->state<2)at=&(*at)->next;
  adoption *a=*at;
  if(a)*at=a->next;
  pl_give();if(!a)break;
  if(a->state==2){adopt_key(a->old,a->key,true);changed=true;}
  else pl_note_migrated(a->key);
  free(a);
 }
 return changed;
}

/* Idle import only: caller has settled the later writer. Never add seconds on
 * a second import; merge each field by max so neither app's history shrinks. */
PSX3D_COLD u32 playlog_latest(void)
{
 load();u32 last=0;
 for(int i=0;log_ents && i<nlog;i++)if(log_ents[i].last>last)last=log_ents[i].last;
 return last;
}
/* Background import reads into an owned buffer; only the menu mutates tables.
 * Persistence uses the existing later writer, so the first room never waits. */
PSX3D_COLD char *playlog_import_psx3d_read(const char *path)
{
 if(!safe_checkpoint())return NULL;
 safe_observe_io("playlog:fopen",path,true);FILE *f=fopen(path,"rb");
 if(!f){char bak[PATH_LEN];if(snprintf(bak,sizeof(bak),"%s.bak",path)>=(int)sizeof(bak) || !safe_checkpoint())return NULL;safe_observe_io("playlog:fopen",bak,true);f=fopen(bak,"rb");}
 if(!f)return calloc(1,1);
 char *text=NULL;long n=-1;
 if(!safe_checkpoint())goto done;
 safe_observe_io("playlog:fseek-end",path,true);
 if(fseek(f,0,SEEK_END))goto done;
 if(!safe_checkpoint())goto done;
 safe_observe_io("playlog:ftell",path,true);n=ftell(f);
 if(n<0 || n>1024*1024 || !safe_checkpoint())goto done;
 safe_observe_io("playlog:fseek-start",path,true);
 if(fseek(f,0,SEEK_SET))goto done;
 text=malloc((size_t)n+1);if(!text)goto done;
 for(size_t at=0;at<(size_t)n;) {
  size_t k=(size_t)n-at>65536?65536:(size_t)n-at;
  if(!safe_checkpoint() || (safe_observe_io("playlog:fread",path,false),fread(text+at,1,k,f)!=k)){free(text);text=NULL;goto done;}at+=k;
 }
 text[n]=0;
done:
 (void)safe_checkpoint();safe_observe_io("playlog:fclose",path,true);if(fclose(f)){free(text);text=NULL;}return text;
}
PSX3D_COLD bool playlog_import_psx3d_apply(const char *text)
{
 load();if(!log_ents || !text)return false;
 char line[160],key[NAME_LEN]="";bool ok=true;
 pl_take();
 while(*text) {
  size_t n=strcspn(text,"\n");size_t copy=n<sizeof(line)-1?n:sizeof(line)-1;
  memcpy(line,text,copy);line[copy]=0;text+=n;if(*text)text++;
  trim(line);
  if(line[0]=='[') {
   key[0]=0;char *end=strchr(line,']');
   if(end && !end[1] && (size_t)(end-line-1)<NAME_LEN-5) {
    *end=0;
    if(!strchr(line+1,':') && !strchr(line+1,'/') && !strchr(line+1,'\\'))
     snprintf(key,sizeof(key),"ps1:%.91s",line+1);
   }
  } else if(key[0] && (!strncmp(line,"last=",5) || !strncmp(line,"seconds=",8))) {
   bool last=line[0]=='l';char *value=strchr(line,'=')+1,*end;unsigned long n=strtoul(value,&end,10);
   if(end==value || *end || n>UINT32_MAX)continue;
   logent_t *e=find(key,true);if(!e){ok=false;break;}
   u32 *field=last?&e->last:&e->seconds;if(*field<n)*field=(u32)n;
  }
 }
 pl_give();return ok;
}
PSX3D_COLD bool playlog_import_psx3d_commit(void)
{
 pl_take();size_t len=0;char *out=pl_log_text(&len);pl_give();
 bool ok=out && pl_write_text(PLAYLOG_FILE,out,len);free(out);return ok;
}
PSX3D_COLD bool playlog_import_psx3d(const char *path)
{
 char *text=playlog_import_psx3d_read(path);
 bool ok=playlog_import_psx3d_apply(text);free(text);
 /* Legacy synchronous callers require the same durable completion contract. */
 if(ok)ok=playlog_import_psx3d_commit();
 return ok;
}
