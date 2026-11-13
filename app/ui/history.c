// Save backups (see history.h). Files are only ever renamed into the
// backups or written through safefile.c, so a power cut leaves either the
// old or the new set of entries, never a cut-off one under a final name.
#include "history.h"
#include "fs_path.h"
#include "app_policy.h"
#include "safefile.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef HISTORY_ROOT
#define HISTORY_ROOT APP_SD_ROOT "/backups"
#endif

void log_printf(const char *fmt, ...);

#ifdef __3DS__
#include <3ds.h>
static void *checkpoint_thread;
static bool (*history_checkpoint)(void *);
static void *history_checkpoint_ctx;
void history_set_checkpoint(bool (*fn)(void *),void *ctx)
{
 __atomic_store_n(&checkpoint_thread,NULL,__ATOMIC_RELEASE);
 history_checkpoint=fn;history_checkpoint_ctx=ctx;
 if(fn)__atomic_store_n(&checkpoint_thread,getThreadLocalStorage(),__ATOMIC_RELEASE);
}
static bool checkpoint(void)
{return __atomic_load_n(&checkpoint_thread,__ATOMIC_ACQUIRE)!=getThreadLocalStorage() || !history_checkpoint || history_checkpoint(history_checkpoint_ctx);}
#else
static _Thread_local bool (*history_checkpoint)(void *);
static _Thread_local void *history_checkpoint_ctx;
void history_set_checkpoint(bool (*fn)(void *),void *ctx)
{history_checkpoint=fn;history_checkpoint_ctx=ctx;}
static bool checkpoint(void)
{return !history_checkpoint || history_checkpoint(history_checkpoint_ctx);}
#endif
/* An import checkpoint is a scheduler permit. Check every filesystem call,
 * including dot entries and missing thumbnails, rather than allowing a
 * directory entry to group several blocking SD operations into one permit. */
static int history_stat(const char *p,struct stat *s)
{if(!checkpoint()){errno=ECANCELED;return -1;}safe_observe_io("history:stat",p,true);return stat(p,s);}
static int history_mkdir(const char *p,mode_t m)
{if(!checkpoint()){errno=ECANCELED;return -1;}safe_observe_io("history:mkdir",p,true);return mkdir(p,m);}
static int history_remove(const char *p)
{if(!checkpoint()){errno=ECANCELED;return -1;}safe_observe_io("history:remove",p,true);return remove(p);}
static DIR *history_opendir(const char *p)
{if(!checkpoint()){errno=ECANCELED;return NULL;}safe_observe_io("history:opendir",p,true);return opendir(p);}
static struct dirent *history_readdir(DIR *d,const char *path)
{if(!checkpoint()){errno=ECANCELED;return NULL;}safe_observe_io("history:readdir",path,true);return readdir(d);}
static int history_closedir(DIR *d,const char *path)
{(void)checkpoint();safe_observe_io("history:closedir",path,true);return closedir(d);}
static int history_rename(const char *a,const char *b)
{if(!checkpoint()){errno=ECANCELED;return -1;}safe_observe_io("history:rename",a,true);return rename(a,b);}
#define closedir_at(d,p) history_closedir(d,p)
#define rename history_rename
#define stat(...) history_stat(__VA_ARGS__)
#define mkdir history_mkdir
#define remove history_remove
#define opendir history_opendir
#define readdir_at(d,p) history_readdir(d,p)
static char root[300] = HISTORY_ROOT;
time_t history_fake_now;

time_t history_now(void)
{
	return history_fake_now ? history_fake_now : time(NULL);
}
#define now history_now
static const char *const kind_name[HIST_KINDS] = {
	[HIST_TIMER] = "timer", [HIST_EXIT] = "exit", [HIST_LOAD] = "load", [HIST_SAVE] = "save",
	[HIST_RESET] = "reset", [HIST_START] = "start", [HIST_DELETE] = "delete", [HIST_RESTORE] = "restore",
	[HIST_CARD_SESSION] = "session", [HIST_CARD_DAY] = "day", [HIST_CARD_PIN] = "pin",
};

/* A conservative, session-local upper bound. While a game runs the frontend
 * owns the backup tree; remote file mutations are library-only. Count every
 * file (even cards/thumbnails), and never subtract trims. No on-disk index. */
static unsigned long long session_bound;
static bool session_bound_valid;
static bool scan_complete;
static bool quiet_writer;
static char session_group[300];
static hist_entry_t session_entries[HIST_RING];
static int session_count;
static bool session_group_valid;
static hist_entry_t *scan(const char *,int *);
void history_session_begin(bool enable,const char *group)
{
    session_bound_valid=false; session_bound=0;session_group_valid=false;
    if(!enable)return;
    DIR *d=opendir(root); if(!d)return;
    bool ok=true;struct dirent *de;
    while((errno=0,(de=readdir_at(d,root)))) {
        if(de->d_name[0]=='.')continue;
        char dir[600];struct stat st;
        if(!fs_path_join(dir,sizeof(dir),root,de->d_name) || stat(dir,&st)){ok=false;break;}
        if(!S_ISDIR(st.st_mode)){session_bound+=st.st_size;continue;}
        DIR *g=opendir(dir);if(!g){ok=false;break;}
        struct dirent *ge;
        while((errno=0,(ge=readdir_at(g,dir)))) {
            if(!strcmp(ge->d_name,".") || !strcmp(ge->d_name,".."))continue;
            char file[900];
            if(!fs_path_join(file,sizeof(file),dir,ge->d_name) || stat(file,&st) || !S_ISREG(st.st_mode)){ok=false;break;}
            session_bound+=(unsigned long long)st.st_size;
        }
        if(!ge && errno)ok=false;
        closedir_at(g,dir);if(!ok)break;
    }
    if(!de && errno)ok=false;
    closedir_at(d,root);session_bound_valid=ok && session_bound<=HIST_BUDGET;
    if(!group || strlen(group)>=sizeof(session_group))return;
    int n;hist_entry_t *entries=scan(group,&n);
    bool cache=scan_complete && n<=HIST_RING;
    for(int i=0;i<n;i++)if(hist_is_card(entries[i].kind))cache=false;
    if(cache) {
        strcpy(session_group,group);session_count=n;
        if(n)memcpy(session_entries,entries,n*sizeof(*entries));
        session_group_valid=true;
    }
    free(entries);
}

void history_set_root(const char *r)
{
	snprintf(root, sizeof(root), "%s", r);
	session_bound_valid=false;session_group_valid=false;
}

const char *history_kind_name(hist_kind_t kind)
{
	return kind < HIST_KINDS ? kind_name[kind] : "?";
}

// "<seq>-<kind>-<time><ext>" -> entry; false for anything else (thumbnails, temp files)
static bool parse(const char *name, hist_entry_t *e)
{
	char *end;
	unsigned long seq = strtoul(name, &end, 10);
	if (end != name + 8 || *end != '-')
		return false;
	end++;
	for (int k = 0; k < HIST_KINDS; k++) {
		size_t n = strlen(kind_name[k]);
		if (strncmp(end, kind_name[k], n) || end[n] != '-' || end[n + 1] < '0' || end[n + 1] > '9')
			continue;
		// the time: digits (no library parser: it would move library code)
		const char *ext = end + n + 1;
		long long when = 0;
		for (int d = 0; *ext >= '0' && *ext <= '9'; ext++)
			if (++d <= 12)
				when = when * 10 + (*ext - '0');
		size_t len = strlen(ext);
		if (ext[0] != '.' || len < 2 || len >= sizeof(e->ext) || strchr(ext + 1, '.'))   // .thm, .tmp, .bak...
			return false;
		e->seq = (unsigned)seq;
		e->kind = (hist_kind_t)k;
		e->when = (time_t)when;
		memcpy(e->ext, ext, len + 1);
		return true;
	}
	return false;
}

static int newest_first(const void *a, const void *b)
{
	(void)checkpoint();
	const hist_entry_t *x = a, *y = b;
	return x->seq < y->seq ? 1 : x->seq > y->seq ? -1 : 0;
}

// every entry of a group, newest first, in a new array (count in *n)
static hist_entry_t *scan(const char *group, int *n)
{
    scan_complete=false;
	char dir[600];
	*n = 0;
	if (!fs_path_join(dir, sizeof(dir), root, group))
		return NULL;
	DIR *d = opendir(dir);
	if (!d)
		return NULL;
    scan_complete=true;
    int cap = 0;
	hist_entry_t *all = NULL;
	struct dirent *de;
    while((errno=0,(de=readdir_at(d,dir)))) {
        if(!checkpoint()){scan_complete=false;break;}
		hist_entry_t e;
		struct stat st;
		if (!parse(de->d_name, &e))
			continue;
		if (!fs_path_join(e.path, sizeof(e.path), dir, de->d_name) || stat(e.path, &st)) {
            scan_complete=false;
            continue;
        }
		e.size = (long)st.st_size;
		char thm[620];
		snprintf(thm, sizeof(thm), "%s.thm", e.path);
		e.thumb = stat(thm, &st) == 0;
        if(!e.thumb && errno!=ENOENT)scan_complete=false;
		if (*n == cap) {
			hist_entry_t *more = realloc(all, (cap = cap ? cap * 2 : 16) * sizeof(*all));
			if (!more) {
                scan_complete=false;
                break;
            }
			all = more;
		}
		all[(*n)++] = e;
	}
    if(!de && errno)scan_complete=false;
	closedir_at(d,dir);
	if (*n)
		qsort(all, *n, sizeof(*all), newest_first);
	return all;
}

int history_list(const char *group, bool cards, hist_entry_t *out, int max)
{
	int n, k = 0;
	hist_entry_t *all = scan(group, &n);
	for (int i = 0; i < n && k < max; i++)
		if (hist_is_card(all[i].kind) == cards)
			out[k++] = all[i];
	free(all);
	return k;
}

static void drop(const hist_entry_t *e)
{
	char thm[620];
	snprintf(thm, sizeof(thm), "%s.thm", e->path);
	remove(thm);
	remove(e->path);
	if(!quiet_writer)
	log_printf("backups: dropped %s\n", e->path);
}

// the calendar day (the 3DS clock and file times are local time already;
// no time zone library code is linked for this)
static long day_of(time_t t)
{
	return (long)(t / 86400);
}

// The per-group limits, newest kept: HIST_RING ring entries; per card file
// one session copy, HIST_CARD_DAYS day copies (one per day), HIST_CARD_PINS pins.
static void trim(const char *group)
{
	int n, ring = 0;
	hist_entry_t *all = scan(group, &n);
	for (int i = 0; i < n; i++) {
        if(!checkpoint())break;
		hist_entry_t *e = &all[i];
		if (!hist_is_card(e->kind)) {
			if (++ring > HIST_RING)
				drop(e);
			continue;
		}
		int same = 0, limit = e->kind == HIST_CARD_SESSION ? 1 : e->kind == HIST_CARD_DAY ? HIST_CARD_DAYS : HIST_CARD_PINS;
		bool twin_day = false;   // a day keeps its first card: a later copy of that day goes
		for (int j = 0; j < n; j++) {
            if(!(j%32) && !checkpoint()){free(all);return;}
			if (j == i || !all[j].path[0] || all[j].kind != e->kind || strcmp(all[j].ext, e->ext))
				continue;
			same += j < i;
			twin_day |= e->kind == HIST_CARD_DAY && j > i && day_of(all[j].when) == day_of(e->when);
		}
		if (same >= limit || twin_day) {
			drop(e);
			e->path[0] = 0;
		}
	}
	free(all);
}

// All games' ring entries together stay under HIST_BUDGET (PSX3D never asks
// the SD card for its free space). First, games not played for
// HIST_IDLE_DAYS keep only their newest HIST_IDLE_KEEP; then the oldest
// entries of any game go. keep: the entry just added is never dropped.
typedef struct { hist_entry_t e; int group, rank; time_t group_newest; } budget_t;
static void enforce_budget(const char *keep)
{
	DIR *d = opendir(root);
	if (!d)
		return;
	budget_t *all = NULL;
	int n = 0, cap = 0, groups = 0;
	unsigned long long total = 0;
	time_t t = now();
	struct dirent *de;
	while ((de = readdir_at(d,root))) {
        if(!checkpoint()){closedir_at(d,root);free(all);return;}
		int m, rank = 0;
		if (de->d_name[0] == '.')
			continue;
		hist_entry_t *g = scan(de->d_name, &m);
		time_t newest = 0;
		for (int i = 0; i < m; i++)
			if (!hist_is_card(g[i].kind) && g[i].when > newest)
				newest = g[i].when;
		for (int i = 0; i < m; i++) {
            if(!checkpoint()){free(g);closedir_at(d,root);free(all);return;}
			if (hist_is_card(g[i].kind))
				continue;
			if (n == cap) {
				budget_t *more = realloc(all, (cap = cap ? cap * 2 : 32) * sizeof(*all));
				if (!more)
					break;
				all = more;
			}
			all[n++] = (budget_t){ g[i], groups, rank++, newest };
			total += (unsigned long long)g[i].size;
		}
		free(g);
		groups++;
	}
	closedir_at(d,root);
	for (int phase = 0; phase < 2 && total > HIST_BUDGET; phase++)
		while (total > HIST_BUDGET) {
            if(!checkpoint()){free(all);return;}
			int victim = -1;
			for (int i = 0; i < n; i++) {
                if(!(i%32) && !checkpoint()){free(all);return;}
				budget_t *b = &all[i];
				if (!b->e.path[0] || (keep && !strcmp(b->e.path, keep)))
					continue;
				if (phase == 0 && (b->rank < HIST_IDLE_KEEP || t - b->group_newest < HIST_IDLE_DAYS * 86400L))
					continue;
				if (victim < 0 || b->e.when < all[victim].e.when ||
				    (b->e.when == all[victim].e.when && b->group == all[victim].group && b->e.seq < all[victim].e.seq))
					victim = i;
			}
			if (victim < 0)
				break;
			drop(&all[victim].e);
			total -= (unsigned long long)all[victim].e.size;
			all[victim].e.path[0] = 0;
		}
	free(all);
}

bool history_next(const char *group, hist_kind_t kind, const char *ext, time_t when, char *out, size_t len)
{
	char dir[600];
	int n;
	if (!group || !*group || strchr(group, '/') || !ext || kind >= HIST_KINDS)
		return false;
    if(session_group_valid && !strcmp(group,session_group) && !hist_is_card(kind)) {
        unsigned seq=session_count?session_entries[0].seq+1:1;
        char name[160];int w=snprintf(name,sizeof(name),"%08u-%s-%lld%s",seq,kind_name[kind],(long long)(when?when:now()),ext);
        return w>0 && (size_t)w<sizeof(name) && fs_path_join(dir,sizeof(dir),root,group) && fs_path_join(out,len,dir,name);
    }
	mkdir(root, 0777);
	if (!fs_path_join(dir, sizeof(dir), root, group))
		return false;
	mkdir(dir, 0777);
	struct stat st;
	if (stat(dir, &st) || !S_ISDIR(st.st_mode))
		return false;
	hist_entry_t *all = scan(group, &n);
	unsigned seq = n ? all[0].seq + 1 : 1;
	free(all);
	char name[160];int w = snprintf(name, sizeof(name), "%08u-%s-%lld%s", seq, kind_name[kind], (long long)(when ? when : now()), ext);
	return w > 0 && (size_t)w < sizeof(name) && fs_path_join(out,len,dir,name);
}

void history_added(const char *group, const char *path)
{
    /* Account before trimming, including any thumbnail. Fail closed on I/O
     * errors or exhaustion: the original global enforcement takes over. */
    bool bounded=false;
    if(session_bound_valid) {
        struct stat st;char thm[620];
        if(stat(path,&st)==0 && st.st_size>=0) {
            session_bound+=(unsigned long long)st.st_size;
            snprintf(thm,sizeof(thm),"%s.thm",path);
            /* A missing thumbnail is normal; count a present one. */
            if(stat(thm,&st)==0)session_bound+=(unsigned long long)st.st_size;
            else if(errno!=ENOENT)session_bound=HIST_BUDGET+1ULL;
            bounded=session_bound<=HIST_BUDGET;
        }
        if(!bounded)session_bound_valid=false;
    }
    if(session_group_valid && !strcmp(group,session_group)) {
        const char *name=strrchr(path,'/');hist_entry_t entry={0};
        if(name && parse(name+1,&entry) && !hist_is_card(entry.kind) && strlen(path)<sizeof(entry.path)) {
            strcpy(entry.path,path);
            if(session_count==HIST_RING) {
                hist_entry_t *old=&session_entries[--session_count];drop(old);
                struct stat st;
                if(stat(old->path,&st)==0 || errno!=ENOENT)session_group_valid=false;
            }
            memmove(session_entries+1,session_entries,session_count*sizeof(entry));
            session_entries[0]=entry;session_count++;
        } else {session_group_valid=false;trim(group);}
    } else trim(group);
    if(bounded)return;
    session_group_valid=false;
    enforce_budget(path);
}

bool history_adopt(const char *group, hist_kind_t kind, const char *ext, const char *file, const char *thm,
	time_t when, hist_entry_t *out)
{
	char to[600], to_thm[620];
	struct stat st;
	if (!file || stat(file, &st))
		return false;
	// entries are named so that hist_entry_t holds their path
	if (!when)
		when = now();
	if (!history_next(group, kind, ext, when, to, sizeof(((hist_entry_t *)0)->path)) || rename(file, to)) {
		log_printf("backups: could not keep %s\n", file);
		return false;
	}
	if (thm && stat(thm, &st) == 0) {
		snprintf(to_thm, sizeof(to_thm), "%s.thm", to);
		rename(thm, to_thm);
	}
	log_printf("backups: kept %s as %s\n", file, to);
	if (out) {
		memset(out, 0, sizeof(*out));
		memcpy(out->path, to, strlen(to) + 1);   // fits: history_next checked it
		out->kind = kind;
		out->when = when;
		snprintf(out->ext, sizeof(out->ext), "%s", ext);
		if (stat(to, &st) == 0)
			out->size = (long)st.st_size;
	}
	history_added(group, to);
	return true;
}

bool history_add(const char *group, hist_kind_t kind, const char *ext, const void *data, size_t size)
{
	char to[600];
	safe_format_t fmt = { size, 0, NULL, NULL, NULL, NULL };
	if (!history_next(group, kind, ext, 0, to, sizeof(((hist_entry_t *)0)->path)) || !safe_write(to, &fmt, data, false))
		return false;
	history_added(group, to);
	return true;
}

bool history_take(const hist_entry_t *e, const char *file)
{
    session_bound_valid=false;session_group_valid=false;
	char from[620], to[620];
	struct stat st;
	if (stat(file, &st) == 0 || rename(e->path, file))   // never over a file
		return false;
	snprintf(from, sizeof(from), "%s.thm", e->path);
	snprintf(to, sizeof(to), "%s.thm", file);
	remove(to);
	rename(from, to);
	return true;
}

/* A caller exclusively owns history until it is drained. Avoid its trim
 * diagnostics touching the main thread's log stream across APT close. */
bool history_add_quiet(const char *group,hist_kind_t kind,const char *ext,const void *data,size_t size)
{
    quiet_writer=true;
    bool ok=history_add(group,kind,ext,data,size);
    quiet_writer=false;
    return ok;
}
