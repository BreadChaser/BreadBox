#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#include "ht_trace.h"
#include "app_policy.h"
#include "bb_index.h"
#include "bb_registry.h"
#include "safefile.h"
#include "cold_code.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#ifdef BB_INDEX_TEST
#include <pthread.h>
#include <unistd.h>
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&mutex)
#define UNLOCK() pthread_mutex_unlock(&mutex)
#define LOG(...) fprintf(stderr, __VA_ARGS__)
static uint64_t millis(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static void pause_worker(void) { usleep(10000); }
static pthread_t worker;
#else
#include <3ds.h>
extern void log_printf(const char *, ...);
static LightLock mutex;
#define LOCK() LightLock_Lock(&mutex)
#define UNLOCK() LightLock_Unlock(&mutex)
#define LOG(...) log_printf(__VA_ARGS__)
static uint64_t millis(void) { return osGetTime(); }
static void pause_worker(void) { svcSleepThread(10000000); }
static Thread worker;
#endif
#ifndef BB_INDEX_CACHE
#define BB_INDEX_CACHE APP_SD_ROOT "/library-index"
#endif
#define PATH_CAP 512
/* No time, file or folder limit: a big library always finishes. The only limits are memory:
 * a list keeps at most MAX_ITEMS games in MAX_BYTES of titles, keys, covers and paths (about 250-300 bytes a game),
 * and a check holds two lists at once (the one shown and the fresh one), 6 MiB at most of the Old 3DS's ~11 MiB
 * free in the room. That is about 12,000 games, eight times a 1,469-game card. The subfolders still waiting to be
 * walked are packed names in at most MAX_PENDING bytes over the whole traversal, allocator overhead included; when
 * the open levels together would pass it, the outer levels' lists are freed and those directories read again later, so
 * only one directory's own list has to fit (as on trunk, with smaller entries). Reaching a limit never drops games a previous
 * complete check found: the cached list stays. */
#define MAX_ITEMS 12288
#define MAX_BYTES (3u * 1024u * 1024u)
#define MAX_PENDING (1024u * 1024u)
/* Names are normalized to ASCII letters/digits, with optional year prefixes.
 * Hints are advisory: never discard a supported file because of its folder. */
static const char *const foreign_folders[]={"gb","gbc","gameboy","1989","1998","gameboycolor","nintendogameboy","nintendogameboycolor","nes","snes","nds","3ds","n64","genesis","megadrive","saturn","dreamcast","psp","ps2","gamecube","wii",NULL};
int bb_index_folder_hint(const char *name)
{
 if(bb_system_folder(name))return 1;
 char n[128];unsigned k=0;
 for(;name && *name && k<127;name++)if(isalnum((unsigned char)*name))n[k++]=tolower((unsigned char)*name);
 n[k]=0;const char *p=n;
 if(k>4 && isdigit((unsigned char)n[0])&&isdigit((unsigned char)n[1])&&isdigit((unsigned char)n[2])&&isdigit((unsigned char)n[3]))p+=4;
 for(const char *const *f=foreign_folders;*f;f++)if(!strcmp(n,*f)||!strcmp(p,*f))return -1;
 return 0;
}
typedef struct { const bb_system_t *system; bb_entry entry; char path[]; } item;
typedef struct { item *items[MAX_ITEMS]; unsigned count,bytes,system_counts[BB_MAX_SYSTEMS]; } snapshot;
static snapshot *current;
static char requested[PATH_CAP];
static unsigned request_id, current_id, generation;
static bool initialized, have_worker, stopping, busy, force, complete, worker_done, cleanup_closed;
static unsigned handled_request;
static char restart_root[PATH_CAP];static bool restart_force;
static bb_index_status_t status;
static unsigned scans;   /* finished scans (bb_index_scans) */
/* Small nonblocking queue for index-related metadata reads. The UI never
 * falls back to doing SD work itself if no worker/memory is available. */
typedef struct deferred_job { struct deferred_job *next; void (*fn)(void *); void *arg; } deferred_job;
static deferred_job cleanup_job;
static deferred_job *jobs, **jobs_tail=&jobs;
static unsigned njobs;
static deferred_job *low_jobs, **low_tail=&low_jobs;
static deferred_job *visible_jobs, **visible_tail=&visible_jobs;
#ifdef BB_INDEX_TEST
static pthread_cond_t wake=PTHREAD_COND_INITIALIZER;
static void signal_worker(void){pthread_cond_signal(&wake);}
static void wait_worker(void){LOCK();if(!stopping && !cleanup_job.fn && !jobs && !visible_jobs && !low_jobs && request_id==handled_request)pthread_cond_wait(&wake,&mutex);UNLOCK();}
#else
extern bool g_new3ds;
static LightEvent wake;
static void signal_worker(void){LightEvent_Signal(&wake);}
static void wait_worker(void){LightEvent_Wait(&wake);}
#endif
bool bb_index_cancelled(void){if(!initialized)return false;LOCK();bool v=stopping;UNLOCK();return v;}

static bool enqueue(void (*fn)(void *),void *arg,int lane)
{
 if(!fn)return false;
 if(!initialized)return false;
 deferred_job *job=malloc(sizeof(*job));if(!job)return false;
 *job=(deferred_job){.fn=fn,.arg=arg};
 LOCK();
 if(stopping || !have_worker || njobs>=16){UNLOCK();free(job);return false;}
 if(lane==2){*visible_tail=job;visible_tail=&job->next;}
 else if(lane==1){*low_tail=job;low_tail=&job->next;}
 else{*jobs_tail=job;jobs_tail=&job->next;}
 njobs++;signal_worker();UNLOCK();return true;
}
bool bb_index_defer(void (*fn)(void *),void *arg){return enqueue(fn,arg,false);}
bool bb_index_defer_cleanup(void (*fn)(void *),void *arg)
{
 if(!initialized || !fn)return false;
 LOCK();
 if(!have_worker || cleanup_closed || cleanup_job.fn){UNLOCK();return false;}
 cleanup_job=(deferred_job){.fn=fn,.arg=arg};signal_worker();UNLOCK();return true;
}
bool bb_index_defer_low(void (*fn)(void *),void *arg){return enqueue(fn,arg,true);}
bool bb_index_defer_visible(void (*fn)(void *),void *arg){return enqueue(fn,arg,2);}
/* Worker-only checkpoint: a long Continue probe can yield to visible TV/box
 * reads without recursively dispatching another Continue probe or index scan. */
bool bb_index_visible_checkpoint(void)
{
 LOCK();deferred_job *job=visible_jobs;
 if(job){visible_jobs=job->next;if(!visible_jobs)visible_tail=&visible_jobs;njobs--;}
 UNLOCK();
 if(!job)return false;
 ht_mark("index visible enter",(uint32_t)(uintptr_t)job->fn);
 job->fn(job->arg);ht_mark("index visible leave",(uint32_t)(uintptr_t)job->fn);free(job);return true;
}
/* At most one job per scan checkpoint. Visible reads always precede marker I/O.
 * On stop, callbacks only cancel their handoff/free owned buffers. */
static bool run_deferred(void)
{
 LOCK();deferred_job cleanup=cleanup_job;bool reserved=cleanup.fn!=NULL;
 if(reserved)cleanup_job.fn=NULL;
 deferred_job *job=reserved?&cleanup:jobs;
 if(reserved){}
 else if(job){jobs=job->next;if(!jobs)jobs_tail=&jobs;}
 else if(visible_jobs){job=visible_jobs;visible_jobs=job->next;if(!visible_jobs)visible_tail=&visible_jobs;}
 else if(low_jobs){job=low_jobs;low_jobs=job->next;if(!low_jobs)low_tail=&low_jobs;}
 if(job && !reserved)njobs--;
 UNLOCK();
 if(!job)return false;
 ht_mark("index job enter",(uint32_t)(uintptr_t)job->fn);job->fn(job->arg);ht_mark("index job leave",(uint32_t)(uintptr_t)job->fn);if(!reserved)free(job);return true;
}
static void release(snapshot *s)
{
    if (!s) return;
    for(unsigned i=0;i<s->count;i++)free(s->items[i]);
    free(s);
}
static bool cancelled(unsigned id)
{
    LOCK(); bool c=stopping || request_id!=id; UNLOCK(); return c;
}
/* What the games are. The menu's index goes to every game (bb_index_shutdown) and is read
 * back from the cache after it: the same games keep their generation, so the libraries,
 * shelves and covers built from it stay as they are (no rebuild on the menu thread). */
static uint32_t content(const snapshot *s)
{
    uint32_t h=2166136261u^s->count;
    for(unsigned i=0;i<s->count;i++) {
        const item *p=s->items[i];
        const char *parts[]={p->system->id,p->path,p->entry.key,p->entry.title,p->entry.cover};
        for(unsigned k=0;k<sizeof(parts)/sizeof(parts[0]);k++)
            for(const char *c=parts[k];;c++) { h=(h^(unsigned char)*c)*16777619u; if(!*c) break; }
        h=(h^p->entry.group^((uint32_t)p->entry.part<<24)^((uint32_t)p->entry.flags<<16))*16777619u;
    }
    return h;
}
static uint32_t last_content;
static struct { const snapshot *s; const bb_system_t *system; int index; unsigned at; } walk_at;   /* bb_index_get */
static bool have_content;
static bool publish(snapshot *s, unsigned id, bool same)
{
    LOCK();
    if (stopping || request_id!=id) { UNLOCK(); return false; }
    snapshot *old=current; current=s; current_id=id; generation+=!same; walk_at.s=NULL;
    UNLOCK(); release(old); return true;
}
static bool append(snapshot *s,const bb_system_t *system,const char *path,const bb_entry *entry,bool live)
{
 size_t bytes=sizeof(item)+strlen(path)+1;
 if(s->count>=MAX_ITEMS || s->bytes+bytes>MAX_BYTES)return false;
 item *p=malloc(bytes);if(!p)return false;
 p->system=system;p->entry=*entry;strcpy(p->path,path);
 if(live)LOCK();
 s->items[s->count++]=p;s->bytes+=bytes;
 for(unsigned i=0;i<bb_system_count && i<BB_MAX_SYSTEMS;i++)if(bb_systems[i]==system){s->system_counts[i]++;break;}
 if(live)UNLOCK();
 return true;
}
static void cache_magic(unsigned char magic[8])
{
 memcpy(magic,"BBI6",4);uint32_t h=2166136261u;
 for(unsigned i=0;i<bb_system_count;i++)for(const char *p=bb_systems[i]->id;;p++) {
  h=(h^(unsigned char)*p)*16777619u;if(!*p)break;
 }
 for(unsigned i=0;i<4;i++)magic[4+i]=h>>(i*8);
}
/* Portable bounded binary cache. Every record has a kind, LE16 path length,
 * path bytes and (GBA only) 192 header bytes plus an optional legacy CRC (zero on header-only scans). FNV covers root and records.
 * No structs, pointers, counts or lengths from disk are trusted. */
static uint32_t hash_bytes(uint32_t h, const void *v, size_t n)
{
    const unsigned char *p=v; while(n--) h=(h^*p++)*16777619u; return h;
}
static bool io_bytes(FILE *f, void *v, size_t n, bool write_, uint32_t *hash)
{
    bool ok=write_ ? fwrite(v,1,n,f)==n : fread(v,1,n,f)==n;
    if (ok && hash) *hash=hash_bytes(*hash,v,n);
    return ok;
}
static bool io32(FILE *f, uint32_t *v, bool wr)
{
    unsigned char b[4];
    if (wr) for(int i=0;i<4;i++) b[i]=(unsigned char)(*v>>(i*8));
    if (!io_bytes(f,b,4,wr,NULL)) return false;
    if (!wr) { *v=0; for(int i=0;i<4;i++) *v|=(uint32_t)b[i]<<(i*8); }
    return true;
}
static bool safe_path(const char *root, const char *path)
{
    size_t n=strlen(root);
    if (strncmp(root,path,n) || path[n]!='/' || !path[n+1]) return false;
    for (const char *p=path+n+1;*p;) {
        size_t k=strcspn(p,"/");
        if (!k || (k==1 && p[0]=='.') || (k==2 && p[0]=='.' && p[1]=='.')) return false;
        for(size_t i=0;i<k;i++) if ((unsigned char)p[i]<32 || p[i]=='\\' || p[i]==':') return false;
        p+=k; if (*p) p++;
    }
    return true;
}
static snapshot *cache_read(const char *root, unsigned id)
{
    safe_recover(BB_INDEX_CACHE);
    FILE *f=fopen(BB_INDEX_CACHE,"rb"); if (!f) return NULL;
    /* 32 KB a request, not stdio's 1 KB: thousands of small reads took 2.7-4 s on the New 3DS */
    char *vbuf=malloc(32*1024); if(vbuf) setvbuf(f,vbuf,_IOFBF,32*1024);
    unsigned char magic[8],expected_magic[8];char saved[PATH_CAP];
    cache_magic(expected_magic); uint32_t count=0, rl=0, expected=0, hash=2166136261u;
    snapshot *s=NULL;
    if (!io_bytes(f,magic,8,false,NULL) || memcmp(magic,expected_magic,8) ||
        !io32(f,&count,false) || count>MAX_ITEMS || !io32(f,&rl,false) ||
        !rl || rl>=PATH_CAP || !io32(f,&expected,false)) goto done;
    if (!io_bytes(f,saved,rl,false,&hash)) goto done;
    saved[rl]=0;
    if (strlen(saved)!=rl || strcmp(saved,root)) goto done;
    s=calloc(1,sizeof(*s)); if(!s) goto done;
    for(uint32_t i=0;i<count;i++) {
        unsigned char rec[3];bb_entry e;char path[PATH_CAP];
        if (cancelled(id) || !io_bytes(f,rec,3,false,&hash)) goto bad;
        unsigned len=rec[1]|(unsigned)rec[2]<<8;
        if ((!rec[0] || rec[0]>bb_system_count) || !len || len>=PATH_CAP ||
            !io_bytes(f,path,len,false,&hash)) goto bad;
        path[len]=0;
        if (strlen(path)!=len || !safe_path(root,path)) goto bad;
        if(!io_bytes(f,&e,sizeof(e),false,&hash) || !memchr(e.title,0,sizeof(e.title)) ||
           !memchr(e.key,0,sizeof(e.key)) || !memchr(e.cover,0,sizeof(e.cover)))goto bad;
        if(!append(s,bb_systems[rec[0]-1],path,&e,false))goto bad;
    }
    if (hash!=expected || fgetc(f)!=EOF) goto bad;
    goto done;
bad:
    release(s); s=NULL;
done:
    fclose(f); free(vbuf); return s;
}
static void cache_write(const char *root, snapshot *s, unsigned id)
{
    if (cancelled(id)) return;
    FILE *f=safe_begin(BB_INDEX_CACHE);
    if (!f) return;
    uint32_t count=s->count, rl=strlen(root), hash=2166136261u, zero=0;
    unsigned char magic[8];cache_magic(magic);
    bool ok=io_bytes(f,magic,8,true,NULL) && io32(f,&count,true) && io32(f,&rl,true) &&
        io32(f,&zero,true) && io_bytes(f,(void *)root,rl,true,&hash);
    for(unsigned i=0;i<s->count && ok;i++) {
     item *p=s->items[i];unsigned kind=0,len=strlen(p->path);
     while(kind<bb_system_count && bb_systems[kind]!=p->system)kind++;
     unsigned char rec[3]={kind+1,len&255,len>>8};
     ok=!cancelled(id) && io_bytes(f,rec,3,true,&hash) && io_bytes(f,p->path,len,true,&hash) && io_bytes(f,&p->entry,sizeof(p->entry),true,&hash);
    }
    ok=ok && !fseek(f,16,SEEK_SET) && io32(f,&hash,true);
    bool saved=safe_finish(f,BB_INDEX_CACHE,ok);
    LOG("index: cache write entries=%u bytes=%u ok=%d\n",(unsigned)count,s->bytes,saved);
}
/* The folders still waiting to be walked, per open directory level, with ONE cap over all levels (MAX_PENDING).
 * A level's child list is packed names, {u16 priority, u8 length, name}, in 16 KiB blocks plus an array of pointers in
 * visiting order. It is only a cache: when the cap would be passed, the shallowest open ancestor's list is freed
 * (evicted); that level keeps a cursor (the key of the last child handed to the walk) and, when its child returns,
 * re-reads its directory keeping only what follows the cursor. So a level never needs more than one directory's list, a
 * directory that fits alone always fits, and visited siblings stop costing memory. Files are probed in the first
 * enumeration only. Each block and each array is one allocation, charged its size + ALLOC_OVERHEAD: newlib (the 3DS)
 * adds at most 19, and its fixed 16/32/64 pool slot is preallocated, never new heap; glibc (host tests) up to 40 below its
 * 128 KiB mmap threshold, where an allocation costs page rounding instead (the host test raises the threshold). Capacity
 * is at least trunk's except within one block (16 KiB + 64) of the cap. */
#ifdef BB_INDEX_TEST
/* Test overrides (0 = the real value): the cap, the block size, and the most re-reads a level may make. */
unsigned bb_index_test_cap, bb_index_test_block, bb_index_test_reread_limit;
#define PENDING_CAP (bb_index_test_cap?bb_index_test_cap:MAX_PENDING)
#define LIST_BLOCK (bb_index_test_block?bb_index_test_block:16384u)
#else
#define PENDING_CAP MAX_PENDING
#define LIST_BLOCK 16384u
#endif
#define ALLOC_OVERHEAD 64u
#define MAX_LEVELS 13u   /* directory levels open at once: depth 0..12 */
typedef struct blockhdr { struct blockhdr *next; unsigned used; } blockhdr;
#define BLOCK_PAYLOAD (LIST_BLOCK-sizeof(blockhdr))
typedef struct {
    blockhdr *head,*tail; const unsigned char **order;   /* order: n pointers while resident */
    unsigned n,total,next; bool resident;               /* total/next survive an eviction: what is left to visit */
} plist;
typedef struct {
    plist list; bool have_cursor; unsigned char clen; unsigned cpri; char cname[256];
    unsigned handed,rereads,first;   /* children handed to the walk, re-reads made, children first listed */
    uint64_t prefix;                /* sum of raw name/priority signatures already handed out */
} frame;
typedef struct {
    snapshot *s; unsigned id, dirs, entries, headers, omitted, ambiguous, errors, pending, peak;
    uint64_t start, last; bool limited; frame *frames[MAX_LEVELS];
} scan;
static bool scan_stop(scan *c)
{
    if (cancelled(c->id)) return true;
    /* Cover reads and imports share this lane: they run between directory entries. No deadline: the walk
     * ends when the card has been read (or a memory limit, c->limited, is reached). */
    run_deferred();
    if (cancelled(c->id)) return true;
    return c->limited;
}
static void progress(scan *c, const char *phase)
{
    uint64_t now=millis();
    if(now-c->last>=1000) {
        LOG("index: %s dirs=%u entries=%u headers=%u games=%u ms=%llu\n",
            phase,c->dirs,c->entries,c->headers,c->s->count,(unsigned long long)(now-c->start));
        c->last=now;
    }
}
#ifdef BB_INDEX_TEST
#include <malloc.h>
/* Test hooks. peak/live: the most pending bytes the last scan charged and those still charged after it. real_*: the same
 * measured from the allocator (usable size plus its size word), independent of the charge; slack: the most one allocation
 * cost beyond its request. evictions/rereads: lists freed for room and directories read again. fail_at: an allocation
 * failure at the nth pending allocation (1-based, 0 = off). at_child: called on the worker before each subfolder is
 * walked, with the bytes pending then. */
static unsigned test_peak, test_live, test_real, test_real_peak, test_allocs, test_slack, test_evictions, test_rereads;
unsigned bb_index_test_fail_pending_at;
void (*bb_index_test_at_child)(const char *path,unsigned pending);
unsigned bb_index_test_pending_peak(void){return test_peak;}
unsigned bb_index_test_pending_live(void){return test_live;}
unsigned bb_index_test_pending_real_peak(void){return test_real_peak;}
unsigned bb_index_test_pending_real_live(void){return test_real;}
unsigned bb_index_test_pending_cap(void){return PENDING_CAP;}
unsigned bb_index_test_alloc_overhead(void){return ALLOC_OVERHEAD;}
unsigned bb_index_test_worst_slack(void){return test_slack;}
unsigned bb_index_test_evictions(void){return test_evictions;}
unsigned bb_index_test_rereads(void){return test_rereads;}
unsigned bb_index_test_pending_allocs(void){return test_allocs;}
#endif
/* One allocation charged to the scan; NULL (the scan limited) when the traversal's cap or the heap is out. */
static void *pending_alloc(scan *c,size_t n)
{
    if(c->pending+n+ALLOC_OVERHEAD>PENDING_CAP) { c->limited=true; return NULL; }
    void *p=malloc(n);
#ifdef BB_INDEX_TEST
    test_allocs++;
    if(bb_index_test_fail_pending_at && test_allocs==bb_index_test_fail_pending_at){free(p);p=NULL;}
    if(p){
        unsigned real=(unsigned)(malloc_usable_size(p)+sizeof(size_t));
        test_real+=real;if(test_real>test_real_peak)test_real_peak=test_real;
        if(real>n && real-n>test_slack)test_slack=real-(unsigned)n;
    }
#endif
    if(!p) { c->limited=true; return NULL; }
    c->pending+=n+ALLOC_OVERHEAD; if(c->pending>c->peak)c->peak=c->pending;
    return p;
}
static void pending_release(scan *c,void *p,size_t n)
{
    if(!p) return;
#ifdef BB_INDEX_TEST
    test_real-=(unsigned)(malloc_usable_size(p)+sizeof(size_t));
#endif
    c->pending-=n+ALLOC_OVERHEAD; free(p);
}
static unsigned directory_priority(const char *name)
{
    const bb_system_t *system=bb_system_folder(name);
    if(system)return system->scan_priority;
    return bb_index_folder_hint(name)<0?257:256;
}
/* Free a level's list (blocks and order array); what is left to visit (total, next) stays known. */
static void list_free(scan *c,plist *l)
{
    for(blockhdr *b=l->head,*after;b;b=after){after=b->next;pending_release(c,b,LIST_BLOCK);}
    pending_release(c,(void *)l->order,l->n*sizeof(*l->order));
    l->head=l->tail=NULL;l->order=NULL;l->n=0;l->resident=false;
}
/* Free the shallowest open ancestor's list; false when no ancestor holds one. */
static bool evict(scan *c,unsigned depth)
{
    for(unsigned k=0;k<depth;k++) {
        frame *f=c->frames[k];
        if(f && f->list.resident){
            list_free(c,&f->list);
#ifdef BB_INDEX_TEST
            test_evictions++;
#endif
            return true;
        }
    }
    return false;
}
/* Add one subfolder to a level's list. Room is made by evicting ancestors; the order array it will need (one pointer
 * per folder) is reserved with it. False (the scan limited): one directory's list alone passes the cap. */
static bool push(scan *c,frame *f,unsigned depth,const char *name)
{
    plist *l=&f->list; size_t len=strlen(name),need=3+len;
    bool fresh=!l->tail || l->tail->used+need>BLOCK_PAYLOAD;
    size_t extra=(fresh?LIST_BLOCK+ALLOC_OVERHEAD:0)+(l->n+1)*sizeof(*l->order)+ALLOC_OVERHEAD;
    while(c->pending+extra>PENDING_CAP) if(!evict(c,depth)) { c->limited=true; return false; }
    if(fresh) {
        blockhdr *b=pending_alloc(c,LIST_BLOCK); if(!b) return false;
        b->next=NULL; b->used=0;
        if(l->tail) l->tail->next=b; else l->head=b;
        l->tail=b;
    }
    unsigned char *r=(unsigned char *)(l->tail+1)+l->tail->used; unsigned pri=directory_priority(name);
    r[0]=pri&255; r[1]=pri>>8; r[2]=(unsigned char)len; memcpy(r+3,name,len);
    l->tail->used+=(unsigned)need; l->n++;
    return true;
}
/* The visiting order: priority, then the name without case. */
static int key_cmp(unsigned pa,const unsigned char *na,unsigned la,unsigned pb,const unsigned char *nb,unsigned lb)
{
    if(pa!=pb) return pa<pb?-1:1;
    for(unsigned i=0;i<la && i<lb;i++){int x=tolower(na[i]),y=tolower(nb[i]);if(x!=y)return x<y?-1:1;}
    return la==lb?0:(la<lb?-1:1);
}
static unsigned rec_pri(const unsigned char *r){return r[0]|(unsigned)r[1]<<8;}
static int rec_cmp(const unsigned char *a,const unsigned char *b){return key_cmp(rec_pri(a),a+3,a[2],rec_pri(b),b+3,b[2]);}
static void sift(const unsigned char **v,unsigned root,unsigned n)
{
    for(unsigned child;(child=2*root+1)<n;root=child) {
        if(child+1<n && rec_cmp(v[child],v[child+1])<0) child++;
        if(rec_cmp(v[root],v[child])>=0) return;
        const unsigned char *t=v[root];v[root]=v[child];v[child]=t;
    }
}
/* Where a record was pushed (names equal without case are visited later-pushed first, as the old sorted list did). */
static unsigned chain_pos(const plist *l,const unsigned char *r)
{
    unsigned k=0;
    for(const blockhdr *b=l->head;b;b=b->next)
        for(unsigned off=0;off<b->used;k++){const unsigned char *p=(const unsigned char *)(b+1)+off;if(p==r)return k;off+=3+p[2];}
    return k;
}
static void order_run(const plist *l,const unsigned char **v,unsigned n)
{
    for(unsigned i=1;i<n;i++){
        const unsigned char *cur=v[i]; unsigned pos=chain_pos(l,cur),j=i;
        while(j>0 && chain_pos(l,v[j-1])<pos){v[j]=v[j-1];j--;}
        v[j]=cur;
    }
}
/* Sort what has been pushed into visiting order (heap sort: in place, no recursion). */
PSX3D_COLD static void finish_list(scan *c,frame *f)
{
    plist *l=&f->list; l->total=l->next=0; l->resident=true;
    if(!l->n) return;
    const unsigned char **v=pending_alloc(c,l->n*sizeof(*v));
    if(!v) { list_free(c,l); return; }
    unsigned i=0;
    for(const blockhdr *b=l->head;b;b=b->next)
        for(unsigned off=0;off<b->used;){const unsigned char *r=(const unsigned char *)(b+1)+off;v[i++]=r;off+=3+r[2];}
    for(unsigned k=l->n/2;k-->0;) sift(v,k,l->n);
    for(unsigned end=l->n;end-->1;){const unsigned char *t=v[0];v[0]=v[end];v[end]=t;sift(v,0,end);}
    for(unsigned a=0,b;a<l->n;a=b) {
        for(b=a+1;b<l->n && !rec_cmp(v[a],v[b]);b++);
        if(b-a>1) order_run(l,v+a,b-a);
    }
    l->order=v; l->total=l->n;
}
enum { ENTRY_SKIP, ENTRY_FILE, ENTRY_DIR };
/* What a directory entry is, building its path; ENTRY_DIR only for a folder the walk may descend into. `first` counts
 * omissions once; metadata failures invalidate every pass, including a re-read. */
static int classify(scan *c,const char *dir,unsigned depth,const struct dirent *de,char *path,bool first)
{
    if(de->d_name[0]=='.') return ENTRY_SKIP;
    bool directory=de->d_type==DT_DIR;
    if(de->d_type!=DT_DIR && de->d_type!=DT_REG && de->d_type!=DT_UNKNOWN) return ENTRY_SKIP;
    if(snprintf(path,PATH_CAP,"%s/%s",dir,de->d_name)>=PATH_CAP) { c->omitted+=first; return ENTRY_SKIP; }
    if(de->d_type==DT_UNKNOWN) {
        struct stat st;
        /* lstat prevents symlink cycles on hosts; FAT has no symlinks. */
        if(lstat(path,&st)) { c->errors++; return ENTRY_SKIP; }
        directory=S_ISDIR(st.st_mode);
        if(!directory && !S_ISREG(st.st_mode)) return ENTRY_SKIP;
    }
    if(!directory) return ENTRY_FILE;
    if(!strcasecmp(path,APP_SD_ROOT) || !strcasecmp(path,APP_LEGACY_PSX3D_ROOT) ||
        !strcasecmp(path,"sdmc:/Nintendo 3DS") || !strcasecmp(path,"sdmc:/DCIM")) return ENTRY_SKIP;
    if(depth>=12 || strlen(de->d_name)>255) { c->omitted+=first; return ENTRY_SKIP; }
    return ENTRY_DIR;
}
/* Does a folder come after the cursor in visiting order? An entry equal without case is told apart by readdir order: the
 * old sorted list put the later one first, so one met before the cursor's own entry is still to visit. */
static bool after_cursor(const frame *f,const char *name,bool *seen)
{
    size_t len=strlen(name);
    int k=key_cmp(f->cpri,(const unsigned char *)f->cname,f->clen,directory_priority(name),(const unsigned char *)name,(unsigned)len);
    if(k) return k<0;
    if(len==f->clen && !memcmp(name,f->cname,len)) { *seen=true; return false; }
    return !*seen;
}
/* A bounded, order-independent signature of the consumed names and priorities. Include raw case-sensitive bytes:
 * equal folded names can change places in readdir order while the cursor keeps the same rank. Count plus signature
 * detects changed prefix membership without storing every visited name; as with any finite hash, collisions are possible. */
static uint64_t folder_signature(unsigned pri,const unsigned char *name,unsigned len)
{
    uint64_t h=14695981039346656037ULL;
    h=(h^(pri&255))*1099511628211ULL; h=(h^(pri>>8))*1099511628211ULL;
    h=(h^len)*1099511628211ULL;
    for(unsigned i=0;i<len;i++) h=(h^name[i])*1099511628211ULL;
    return h;
}
/* Read an evicted level again, keeping folders after the cursor. Validate both the count and raw-name/priority
 * signature of the consumed prefix. Changes after the cursor may still be discovered or omitted. Re-reads are bounded
 * (each follows a visited child); a failed validation ends the scan with an error and preserves the cached list. */
static bool rebuild(scan *c,frame *f,const char *dir,unsigned depth)
{
    plist *l=&f->list; list_free(c,l); l->total=l->next=0;
    unsigned limit=2*f->first+16;
#ifdef BB_INDEX_TEST
    if(bb_index_test_reread_limit) limit=bb_index_test_reread_limit;
    test_rereads++;
#endif
    if(++f->rereads>limit) { c->errors++; return false; }
    DIR *d=opendir(dir);
    if(!d) { c->errors++; return false; }
    struct dirent *de; bool seen=false,whole=false; unsigned before=0,errors=c->errors; uint64_t prefix=0;
    while(!scan_stop(c)) {
        errno=0; de=readdir(d);
        if(!de) { if(errno) c->errors++; else whole=true; break; }
        char path[PATH_CAP];
        if(classify(c,dir,depth,de,path,false)!=ENTRY_DIR) continue;
        if(f->have_cursor && !after_cursor(f,de->d_name,&seen)) {
            before++; prefix+=folder_signature(directory_priority(de->d_name),(const unsigned char *)de->d_name,(unsigned)strlen(de->d_name));
            continue;
        }
        if(!push(c,f,depth,de->d_name)) break;
    }
    closedir(d);
    if(whole && f->have_cursor && (before!=f->handed || prefix!=f->prefix)) c->errors++;
    if(c->errors!=errors) { list_free(c,l); return false; }
    finish_list(c,f);
    return l->total>0;
}
PSX3D_COLD static void walk(scan *c, const char *dir, unsigned depth);
/* Folder names prioritize supported systems, never override file validation.
 * Other-system folders cost directory enumeration only for foreign formats;
 * supported files in a mislabeled subtree are still discovered. */
static void walk_children(scan *c,frame *f,const char *dir,unsigned depth)
{
    plist *l=&f->list;
    if(c->limited) list_free(c,l); else finish_list(c,f);
    f->first=l->total;
    while(!scan_stop(c) && l->next<l->total) {
        if(!l->resident && !rebuild(c,f,dir,depth)) break;   /* a child evicted this level's list */
        if(l->next>=l->total) break;
        const unsigned char *r=l->order[l->next++]; f->handed++;
        f->cpri=rec_pri(r); f->clen=r[2]; memcpy(f->cname,r+3,r[2]); f->cname[r[2]]=0; f->have_cursor=true;
        f->prefix+=folder_signature(f->cpri,r+3,r[2]);
        char path[PATH_CAP];
        /* the path fit when the folder was listed */
        snprintf(path,sizeof(path),"%s/%s",dir,f->cname);
#ifdef BB_INDEX_TEST
        if(bb_index_test_at_child)bb_index_test_at_child(path,c->pending);
#endif
        if(!scan_stop(c)) walk(c,path,depth+1);
    }
    list_free(c,l);
}
PSX3D_COLD static void walk(scan *c, const char *dir, unsigned depth)
{
    if(scan_stop(c)) return;
    DIR *d=opendir(dir); c->dirs++;
    if(!d) { c->errors++; return; }
    struct dirent *de;
    frame f; memset(&f,0,sizeof(f)); c->frames[depth]=&f;
    while(!scan_stop(c)) {
        errno=0;
        de=readdir(d);
        if(!de) { if(errno) c->errors++; break; }
        c->entries++; progress(c,"directory");
        char path[PATH_CAP];
        int kind=classify(c,dir,depth,de,path,true);
        if(kind==ENTRY_SKIP) continue;
        if(kind==ENTRY_DIR) { if(!push(c,&f,depth,de->d_name)) break; continue; }
        bb_entry entry={0};
        const bb_system_t *system=bb_system_probe_path(path,&entry);
        if(system) {c->headers++;if(!append(c->s,system,path,&entry,true))c->limited=true; if(!(c->headers%16))pause_worker();}
        else {
            unsigned candidates=0;
            for(unsigned i=0;i<bb_system_count;i++)candidates+=bb_system_extension(bb_systems[i],path);
            if(candidates>1) {
                c->ambiguous++;
                LOG("index: unidentified/ambiguous game %s; place it in a matching system folder or check its header\n",path);
            }
        }
    }
    closedir(d);
    walk_children(c,&f,dir,depth);
    c->frames[depth]=NULL;
}
#ifdef BB_INDEX_TEST
static void *worker_main(void *unused)
#else
static void worker_main(void *unused)
#endif
{
    (void)unused; unsigned handled=0;
    for(;;) {
        bool job_ran=run_deferred();
        char root[PATH_CAP]; unsigned id; bool refresh;
        LOCK();
        if(stopping) {
            /* Seal the final drain under the enqueue lock. A cleanup accepted
             * before this point must run; later submissions must restart us. */
            if(cleanup_job.fn || jobs || visible_jobs || low_jobs){UNLOCK();continue;}
            cleanup_closed=true;UNLOCK();break;
        }
        id=request_id; refresh=force; memcpy(root,requested,sizeof(root));
        UNLOCK();
        if(id==handled) { if(!job_ran)wait_worker(); continue; }
        handled=id;LOCK();handled_request=id;UNLOCK();
        HT("index scan enter");
        uint64_t start=millis();
        LOG("index: cache root=%s\n",root);
        LOCK();bool retained=complete && current && current_id==id; snapshot *s=retained?current:NULL;UNLOCK();
        /* Every start, Hold Y included, shows the cached list at once and checks the card behind it; only a
         * complete check replaces it (a new root has no cache: its first scan fills the shelves as it goes). */
        if(!retained)s=cache_read(root,id);
        bool cached=s!=NULL, finished=false;
        bb_index_status_t result=BB_INDEX_ERROR;
        if(!s) s=calloc(1,sizeof(*s));
        uint32_t was=last_content;
        bool same=cached && have_content && content(s)==was;
        if(!s || (!retained && !publish(s,id,same))) { release(s); goto finish; }
        if(cached) {
            last_content=content(s); have_content=true;
            LOCK();if(request_id==id && !stopping)complete=true;UNLOCK();
            LOG("index: cache hit games=%u ms=%llu%s\n",s->count,(unsigned long long)(millis()-start),same?" (same games: generation kept)":"");
            pause_worker(); /* let the first cached frame render before SD validation */
        }
        /* Cached records stay visible while a private snapshot is validated.
         * FAT timestamps cannot validate content: walk/probe on the worker.
         * Cold scans append incrementally to their published empty snapshot. */
        snapshot *fresh=cached?calloc(1,sizeof(*fresh)):s;
        if(!fresh)goto finish;
        scan c={.s=fresh,.id=id,.start=millis(),.last=millis()};
#ifdef BB_INDEX_TEST
        test_real_peak=test_real; test_allocs=0;
#endif
        walk(&c,root,0);
#ifdef BB_INDEX_TEST
        test_peak=c.peak; test_live=c.pending;
#endif
        LOG("index: finish directory=%u entries=%u header=%u games=%u omitted=%u ambiguous=%u errors=%u limited=%d ms=%llu\n",
            c.dirs,c.entries,c.headers,fresh->count,c.omitted,c.ambiguous,c.errors,c.limited,(unsigned long long)(millis()-c.start));
        /* Omitted entries (a path over PATH_CAP, folders over 12 deep) are logged and skipped the same way every time:
         * they do not stop the check from replacing the cached list. A read error or a memory limit does. */
        finished=!c.limited && !c.errors && !cancelled(id);
        result=c.errors?BB_INDEX_ERROR:(finished?BB_INDEX_COMPLETE:BB_INDEX_LIMITED);
        if(finished) {
            uint32_t next=content(fresh);
            /* Never replace a usable cache by a partial scan (unplugged SD,
             * budget, unreadable subtree). Identical validation keeps IDs. */
            if(cached && next!=last_content) {
                if(!publish(fresh,id,false)){release(fresh);goto finish;}
                s=fresh;
            } else if(cached) {release(fresh);fresh=s;}
            if(!cached || refresh || next!=last_content)cache_write(root,fresh,id);
            last_content=next;have_content=true;
        } else if(cached) release(fresh);

finish:
        HT("index scan leave");
        LOCK(); if(request_id==id && !stopping) { busy=false; complete=current && current_id==id && (cached || finished || current->count); status=result; scans++; } UNLOCK();
    }
LOCK();snapshot *discard=current;current=NULL;walk_at.s=NULL;UNLOCK();release(discard);
#ifdef BB_INDEX_TEST
    LOCK();worker_done=true;UNLOCK();
    return NULL;
#else
    LOCK();worker_done=true;UNLOCK();
#endif
}
static bool reap_worker(void)
{
 if(!have_worker){stopping=false;return true;}
 LOCK();bool done=worker_done;UNLOCK();if(!done)return false;
#ifdef BB_INDEX_TEST
 pthread_join(worker,NULL);
#else
 if(threadJoin(worker,0)!=0)return false;
 threadFree(worker);worker=NULL;
#endif
 have_worker=false;worker_done=false;cleanup_closed=false;stopping=false;
 LOCK();snapshot *old=current;current=NULL;walk_at.s=NULL;UNLOCK();release(old);
 requested[0]=0;request_id=current_id=handled_request=0;
 return true;
}
static bool closed;   /* bb_index_close: the app is exiting */
bool bb_index_background_start(void)
{
 if(closed)return false;
 if(stopping && !reap_worker())return false;
    if(!initialized) {
#ifndef BB_INDEX_TEST
        LightLock_Init(&mutex);
        LightEvent_Init(&wake,RESET_ONESHOT);
#endif
        initialized=true;
    }
    if(!have_worker) {
#ifdef BB_INDEX_TEST
        have_worker=pthread_create(&worker,NULL,worker_main,NULL)==0;
#else
        int core=g_new3ds?2:0;
        worker=threadCreate(worker_main,NULL,96*1024,0x31,core,false);
        if(!worker && core!=0)worker=threadCreate(worker_main,NULL,96*1024,0x31,0,false);
        if(!worker)worker=threadCreate(worker_main,NULL,96*1024,0x31,-2,false);
        LOG("index: worker core=%d priority=49 available=%d\n",core,worker!=NULL);
        have_worker=worker!=NULL;
#endif
        if(!have_worker) { LOCK(); busy=false; status=BB_INDEX_ERROR; UNLOCK(); LOG("index: worker creation failed\n"); }
    }
 return have_worker;
}
void bb_index_start(const char *root, bool refresh)
{
    if(!root || !*root || strlen(root)>=PATH_CAP) return;
    if(stopping && !reap_worker()){snprintf(restart_root,sizeof(restart_root),"%s",root);restart_force=refresh;LOCK();busy=true;status=BB_INDEX_SCANNING;UNLOCK();return;}
    /* Grouped presentation caches validate their content signature. No UI-thread I/O. */
    char normalized[PATH_CAP]; strcpy(normalized,root);
    size_t n=strlen(normalized); while(n>1 && normalized[n-1]=='/') normalized[--n]=0;
    if(!bb_index_background_start())return;
    LOCK();
    if(!refresh && have_worker && request_id && !strcmp(normalized,requested)) { UNLOCK(); return; }
    bool keep=current && current_id==request_id && current->count>0 && !strcmp(normalized,requested);
    strcpy(requested,normalized); request_id++; if(keep)current_id=request_id; busy=true; complete=keep; status=BB_INDEX_SCANNING; force=refresh;signal_worker();
    UNLOCK();

}
void bb_index_service(void)
{
 if(restart_root[0] && reap_worker()){char root[PATH_CAP];strcpy(root,restart_root);restart_root[0]=0;bb_index_start(root,restart_force);}
}
static bool publication_pending;
void bb_index_publish(void)
{
 /* A populated screen cannot freeze another system's presentation. Adapters
  * already detect settled generations and bound incremental publication. */
 bb_index_service();
 bool changed=false;
 for(unsigned i=0;i<bb_system_count;i++) {
  const bb_shell_ops *ops=bb_systems[i]->shell;
  if(ops && ops->poll_library)changed|=ops->poll_library(true);
 }
 publication_pending |= changed;
}
bool bb_index_poll(bool refresh)
{
 (void)refresh; /* Compatibility hint; every adapter always advances. */
 bb_index_publish();
 bool changed=publication_pending;
 publication_pending=false;
 return changed;
}
bool bb_index_busy(void) { if(!initialized) return false; LOCK(); bool v=busy; UNLOCK(); return v; }
bb_index_status_t bb_index_status(void) { if(!initialized) return BB_INDEX_IDLE; LOCK(); bb_index_status_t v=status; UNLOCK(); return v; }
unsigned bb_index_scans(void) { if(!initialized) return 0; LOCK(); unsigned v=scans; UNLOCK(); return v; }
bool bb_index_complete(void) { if(!initialized) return false; LOCK(); bool v=complete && !stopping && current && current_id==request_id; UNLOCK(); return v; }
unsigned bb_index_generation(void) { if(!initialized) return 0; LOCK(); unsigned v=generation; UNLOCK(); return v; }
const char *bb_index_root(void) { return requested; } /* main-thread-owned string */
/* What bb_index_count returns for every system, as one value: a caller (the room's Collection card) keeps the last
 * key and redraws its counts only when it changes, whether games appeared during a first scan, a background check
 * replaced the list or the list was put away for a game. Read under the same lock, no I/O. */
uint32_t bb_index_counts_key(void)
{
 uint32_t h=2166136261u;
 if(!initialized)return h;
 LOCK();
 bool visible=!stopping && current && current_id==request_id;
 h=(h^(visible?1u:0u))*16777619u;
 for(unsigned i=0;i<bb_system_count && i<BB_MAX_SYSTEMS;i++)h=(h^(visible?current->system_counts[i]:0u))*16777619u;
 UNLOCK();return h;
}
int bb_index_count(const bb_system_t *system)
{
 if(!initialized)return 0;
 LOCK();int n=0;
 if(!stopping && current && current_id==request_id)
  for(unsigned i=0;i<bb_system_count && i<BB_MAX_SYSTEMS;i++)if(bb_systems[i]==system){n=current->system_counts[i];break;}
 UNLOCK();return n;
}
/* Callers walk a system's entries in order (the shelves, the GBA list): go on from the last
 * one found instead of from the start (it was a scan of all games per entry: 1 s for 315 GBA
 * games among 1469 on the New 3DS) */
bool bb_index_get(const bb_system_t *system,int index,char *path,size_t cap,bb_entry *entry)
{
 if(!initialized || index<0 || !path || !cap)return false;
 LOCK();item *found=NULL;
 if(!stopping && current && current_id==request_id) {
  unsigned i=0;int want=index;
  if(walk_at.s==current && walk_at.system==system && index>=walk_at.index && walk_at.at<current->count) {i=walk_at.at;want=index-walk_at.index;}
  for(;i<current->count;i++) {
   item *p=current->items[i];
   if(p->system==system && !want--){found=p;walk_at.s=current;walk_at.system=system;walk_at.index=index;walk_at.at=i;break;}
  }
 }
 bool ok=found && strlen(found->path)<cap;
 if(ok){strcpy(path,found->path);if(entry)*entry=found->entry;}
 UNLOCK();return ok;
}
void bb_index_shutdown(void)
{
 if(!initialized)return;
 uint64_t start=millis();
 LOCK();stopping=true;busy=false;complete=false;status=BB_INDEX_IDLE;signal_worker();UNLOCK();
#ifdef BB_INDEX_TEST
 for(int i=0;have_worker && i<45;i++){LOCK();bool done=worker_done;UNLOCK();if(done)break;usleep(1000);}
#else
 if(have_worker)threadJoin(worker,45000000LL);
#endif
 reap_worker();
 LOG("index: shutdown ms=%llu pending=%d\n",(unsigned long long)(millis()-start),have_worker);
}
bool bb_index_shutdown_done(void)
{return !initialized || reap_worker();}
void bb_index_close(void)
{closed=true;restart_root[0]=0;}
static bool directory_exists(const char *path)
{
    struct stat st; return path && *path && !stat(path,&st) && S_ISDIR(st.st_mode);
}
static bool choose(const char *p,char *out,size_t cap)
{
    if(!p || strlen(p)>=cap || !directory_exists(p)) return false;
    strcpy(out,p); return true;
}
bool bb_index_root_normalize(const char *path,char *out,size_t cap)
{
    if (!path || !out || !cap || strlen(path)>=cap) return false;
    strcpy(out,path);
    const char *base=strrchr(path,'/');
    if (!base || !base[1]) return true;
    int hint=bb_index_folder_hint(base+1);
    if (hint==0) return true;
    size_t n=(size_t)(base-path);
    if (n==0) n=1;
    if (n>=cap) return false;
    memcpy(out,path,n); out[n]=0;
    return true;
}
/* One shallow probe per common root, no cartridge reads or recursive walk. */
static bool likely_games(const char *path, uint64_t deadline)
{
    DIR *d=opendir(path); if(!d) return false;
    struct dirent *de; unsigned n=0; bool found=false;
    while(n++<128 && millis()<deadline && (de=readdir(d))) {
        if(de->d_name[0]=='.') continue;
        int hint=bb_index_folder_hint(de->d_name);
        if ((de->d_type==DT_DIR || de->d_type==DT_UNKNOWN) &&
            hint>0) { found=true; break; }
        if ((de->d_type==DT_REG || de->d_type==DT_UNKNOWN) &&
            bb_system_for_path(de->d_name)) { found=true; break; }
    }
    closedir(d); return found;
}
bool bb_index_discover(const char *saved,char *out,size_t cap)
{
    if(!out || !cap) return false;
    char normalized[PATH_CAP];
    if (bb_index_root_normalize(saved,normalized,sizeof(normalized)) && choose(normalized,out,cap)) return true;
    static const char *const common[]={"sdmc:/roms","sdmc:/ROMs","sdmc:/games","sdmc:/retroarch/roms"};
    const char *fallback=NULL; uint64_t deadline=millis()+250;
    for(unsigned i=0;i<sizeof(common)/sizeof(common[0]);i++) {
        if(!directory_exists(common[i])) continue;
        if(!fallback) fallback=common[i];
        if(likely_games(common[i],deadline) && choose(common[i],out,cap)) return true;
    }
    FILE *f=fopen(APP_LEGACY_PSX3D_ROOT "/config.ini","r");
    if(f) {
        char line[PATH_CAP+32]; unsigned lines=0;
        while(lines++<256 && fgets(line,sizeof(line),f)) {
            if(strncmp(line,"games_dir=",10)) continue;
            line[strcspn(line,"\r\n")]=0;
            bool ok=choose(line+10,out,cap); fclose(f); if(ok) return true; f=NULL; break;
        }
        if(f) fclose(f);
    }
    /* One bounded top-level listing and up to eight shallow custom-folder probes. */
    DIR *d=opendir("sdmc:/"); if(!d) return choose(fallback,out,cap);
    struct dirent *de; unsigned entries=0, probes=0; bool found=false; uint64_t start=millis();
    while(entries++<256 && millis()-start<250 && (de=readdir(d))) {
        if(de->d_name[0]=='.') continue;
        if(de->d_type!=DT_DIR && de->d_type!=DT_UNKNOWN) continue;
        if(!strcasecmp(de->d_name,"3ds") || !strcasecmp(de->d_name,"Nintendo 3DS") ||
           !strcasecmp(de->d_name,"DCIM") || !strcasecmp(de->d_name,"luma") ||
           !strcasecmp(de->d_name,"boot9strap")) continue;
        int hint=bb_index_folder_hint(de->d_name);
        char path[PATH_CAP];
        if(snprintf(path,sizeof(path),"sdmc:/%s",de->d_name)>=(int)sizeof(path)) continue;
        /* Select the common SD root so sibling supported systems remain visible. */
        if(hint>0 && directory_exists(path)) {
            found=choose("sdmc:",out,cap); break;
        }
        if(hint==0 && probes++<8 && likely_games(path,start+250)) {
            found=choose(path,out,cap); break;
        }
    }
    closedir(d); return found || choose(fallback,out,cap);
}
