#define _DEFAULT_SOURCE
/* The scan's waiting subfolders: a library trunk completes completes here too (cold, no cache), the memory they hold stays
 * under one cap over the whole traversal (counted, and measured against the allocator), and a limit, an allocation
 * failure or a cancel frees them all and keeps the cached games. Built for tests/bb_index/run.sh with -DBB_INDEX_TEST
 * (hooks in bb_index.c). */
#include "bb_index.h"
#include <assert.h>
#include <stdbool.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
unsigned bb_index_test_pending_peak(void);
unsigned bb_index_test_pending_live(void);
unsigned bb_index_test_pending_real_peak(void);
unsigned bb_index_test_pending_real_live(void);
unsigned bb_index_test_pending_cap(void);
unsigned bb_index_test_alloc_overhead(void);
unsigned bb_index_test_worst_slack(void);
unsigned bb_index_test_evictions(void);
unsigned bb_index_test_rereads(void);
extern unsigned bb_index_test_fail_pending_at;
extern void (*bb_index_test_at_child)(const char *path,unsigned pending);
extern unsigned bb_index_test_cap,bb_index_test_block,bb_index_test_reread_limit;
static void name(char *out,char kind,unsigned i,unsigned len)
{
    memset(out,'x',len);out[len]=0;
    out[0]=kind;snprintf(out+1,8,"%04u",i);out[5]='x';   /* 'c' < 's' < 'z': the chain is walked before its siblings */
}
static void make(const char *path){if(mkdir(path,0700) && access(path,F_OK))assert(!"mkdir");}
static void touch(const char *dir,const char *file)
{
    char p[1200];snprintf(p,sizeof(p),"%s/%s",dir,file);
    FILE *f=fopen(p,"wb");assert(f);fclose(f);
}
/* `count` empty folders of `len` characters named kind+number in dir. */
static void siblings(const char *dir,char kind,unsigned count,unsigned len)
{
    for(unsigned i=0;i<count;i++){char n[300],p[1200];name(n,kind,i,len);snprintf(p,sizeof(p),"%s/%s",dir,n);make(p);}
}
/* root with a chain of `levels` folders (8-character names), `count` siblings of `len` characters at every level; a game in
 * the root and one at the chain's bottom. Existing folders stay, so a tree only grows. */
static void chain(const char *root,unsigned levels,unsigned count,unsigned len)
{
    char dir[600],n[300];snprintf(dir,sizeof(dir),"%s",root);make(dir);
    touch(dir,"top.cue");
    for(unsigned level=0;level<levels;level++) {
        siblings(dir,'s',count,len);
        name(n,'c',0,8);size_t l=strlen(dir);snprintf(dir+l,sizeof(dir)-l,"/%s",n);make(dir);
    }
    touch(dir,"deep.cue");
}
/* `count` folders of `len` characters in root, a game in each. */
static void flat(const char *root,unsigned count,unsigned len)
{
    make(root);
    for(unsigned i=0;i<count;i++){char n[300],d[1200];name(n,'d',i,len);snprintf(d,sizeof(d),"%s/%s",root,n);make(d);touch(d,"g.cue");}
}
static void wait_done(void)
{
    for(int i=0;bb_index_busy() && i<30000;i++)usleep(10000);
    assert(!bb_index_busy());
}
static void scan(const char *root){bb_index_start(root,true);wait_done();}
/* the scan's own count and the allocator's size words both stay under the cap, one allocation never costs more than its
 * allowance, and everything is returned */
static void check_bounds(void)
{
    const unsigned cap=bb_index_test_pending_cap();
    assert(bb_index_test_pending_peak()<=cap && bb_index_test_pending_real_peak()<=cap);
    assert(bb_index_test_worst_slack()<=bb_index_test_alloc_overhead());
    assert(bb_index_test_pending_live()==0 && bb_index_test_pending_real_live()==0);
}
static void complete(const char *root,int games)
{
    scan(root);
    assert(bb_index_status()==BB_INDEX_COMPLETE && bb_index_ps1_count()==games);check_bounds();
}
static volatile int armed,reached,release;static volatile unsigned reached_pending;
static void hold(const char *path,unsigned pending)
{
    (void)path;
    if(!armed || !pending)return;
    armed=0;reached_pending=pending;__atomic_store_n(&reached,1,__ATOMIC_RELEASE);
    for(int i=0;i<5000 && !__atomic_load_n(&release,__ATOMIC_ACQUIRE);i++)usleep(1000);
}
/* glibc: holes of 48 bytes that a 32-byte request must take whole (the remainder is under the 32-byte minimum chunk) */
static void fragment(void)
{
    mallopt(M_MXFAST,0);
    enum{N=4000};static void *keep[N],*hole[N];
    for(int i=0;i<N;i++){hole[i]=malloc(40);keep[i]=malloc(24);}
    for(int i=0;i<N;i++)free(hole[i]);
    (void)keep;
}
static void order(void)
{
    /* the visiting order: registered system folders first, then the rest by name without case, folders of other systems
     * last (the same list as trunk's) */
    make("order");
    const char *dirs[]={"zzz","aaa","Beta","PSX","NES","alpha"};
    for(unsigned i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++) {
        char d[64],f[64];snprintf(d,sizeof(d),"order/%s",dirs[i]);make(d);snprintf(f,sizeof(f),"%s.cue",dirs[i]);touch(d,f);
    }
    scan("order");
    const char *want[]={"PSX","aaa","alpha","Beta","zzz","NES"};
    assert(bb_index_status()==BB_INDEX_COMPLETE && bb_index_ps1_count()==6);
    for(int i=0;i<6;i++) {
        char p[512];assert(bb_index_ps1_get(i,p,sizeof(p)));
        char tail[64];snprintf(tail,sizeof(tail),"/%s/%s.cue",want[i],want[i]);
        assert(strlen(p)>=strlen(tail) && !strcmp(p+strlen(p)-strlen(tail),tail));
    }
}
/* A folder changed while a level's list was evicted: the scan must end as an error and keep the cached games (as trunk,
 * whose stale path no longer opens), never report COMPLETE with games skipped or walked twice. The change is made from the
 * worker's child checkpoint, deterministically, with a small cap so that the big folder `b` evicts its parent's list. */
static const char *mutate_at;static void (*mutate_do)(void);
static void mutate_hook(const char *path,unsigned pending)
{
    (void)pending;
    if(mutate_do && !strcmp(path,mutate_at)){void (*a)(void)=mutate_do;mutate_do=NULL;a();}
}
static void rename_d_before(void){assert(!rename("mut/d","mut/0d"));}          /* an unvisited folder moves before the cursor */
static void rename_a_after(void){assert(!rename("mut/a","mut/z"));}            /* a visited folder moves after it */
static void add_e_after(void){make("mut/e");touch("mut/e","g.cue");}           /* a folder added after it (trunk misses it) */
static void rename_tie_after(void){assert(!rename("tie/Alpha","tie/z"));}      /* a visited case variant moves after it */
static void mkmut(const char *root,const char *bigs,const char *names)
{
    make(root);
    for(const char *k=names;*k;k++){char d[200];snprintf(d,sizeof(d),"%s/%c",root,*k);make(d);touch(d,"g.cue");if(strchr(bigs,*k))siblings(d,'s',300,30);}
}
static void mutated(const char *root,const char *at,void (*act)(void),int games,bool error)
{
    unsigned ev=bb_index_test_evictions();
    mutate_at=at;mutate_do=act;scan(root);
    assert(!mutate_do);   /* the change was made */
    assert(bb_index_test_evictions()>ev);   /* and the level had been evicted */
    check_bounds();
    if(error) assert(bb_index_status()==BB_INDEX_ERROR && bb_index_complete() && bb_index_ps1_count()==games);   /* cached games stay */
    else assert(bb_index_status()==BB_INDEX_COMPLETE && bb_index_ps1_count()==games);
}
static void mutation(void)
{
    bb_index_test_cap=14336;bb_index_test_block=1024;bb_index_test_at_child=mutate_hook;
    mkmut("mut","b","abcd");complete("mut",4);   /* seed: evicts and reads again, finds the 4 games */
    mutated("mut","mut/b",rename_d_before,4,true);assert(!rename("mut/0d","mut/d"));
    mutated("mut","mut/b",rename_a_after,4,true);assert(!rename("mut/z","mut/a"));
    mutated("mut","mut/b",add_e_after,5,false);   /* an addition after the cursor is simply found */
    assert(!system("rm -rf mut/e"));complete("mut",4);
    char tie[]="tie";make(tie);
    {const char *v[]={"ALPHA","Alpha","aLpHa","b","c"};for(int i=0;i<5;i++){char d[64];snprintf(d,sizeof(d),"tie/%s",v[i]);make(d);touch(d,"g.cue");if(i==3)siblings(d,'s',300,30);}}
    complete("tie",5);
    mutated("tie","tie/b",rename_tie_after,5,true);assert(!rename("tie/z","tie/Alpha"));
    /* the re-reads of a level are bounded: two of them with a limit of one end the scan as an error */
    mkmut("mut2","bc","abcd");complete("mut2",4);
    bb_index_test_reread_limit=1;
    unsigned ev=bb_index_test_evictions();scan("mut2");
    assert(bb_index_test_evictions()>ev+1 && bb_index_status()==BB_INDEX_ERROR && bb_index_ps1_count()==4);check_bounds();
    bb_index_test_reread_limit=0;complete("mut2",4);
    bb_index_test_at_child=NULL;bb_index_test_cap=bb_index_test_block=0;
    puts("bb_index: folders changed while a level was evicted end the scan as an error, cache kept: ok");
}
int main(void)
{
    const unsigned cap=bb_index_test_pending_cap();
    mallopt(M_MMAP_THRESHOLD,32*1024*1024);   /* no page-rounded mmap allocations: the allowance below is for the heap */
    mallopt(M_ARENA_MAX,1);   /* the worker thread shares the main heap, so fragment() reaches its allocations */
    order();
    /* Allocator slack: every pending allocation lands in a hole that cannot be split. */
    fragment();
    chain("slack",4,0,8);   /* one folder per level: an 8-byte order array */
    complete("slack",2);
    /* A library of 9,000 folders in one folder completes with short names and with 94-character ones, and 3,500 with
     * 255-character ones (all complete on trunk), cold. */
    flat("flatshort",9000,8);complete("flatshort",9000);
    flat("flat",9000,94);complete("flat",9000);
    assert(bb_index_test_pending_peak()>0 && bb_index_test_pending_peak()<cap);
    flat("flatmax",3500,255);complete("flatmax",3500);
    /* Trees whose every directory fits trunk's one-megabyte allowance but whose open levels together do not: all
     * complete on trunk, so all complete here, cold. Lists of outer levels are freed for room and read again. */
    unsigned evictions=bb_index_test_evictions(),rereads=bb_index_test_rereads();
    chain("branch",12,600,200);complete("branch",2);   /* 12 levels, 600 folders of 200 characters each */
    assert(bb_index_test_evictions()>evictions && bb_index_test_rereads()>rereads);
    chain("twolevel",2,3000,200);complete("twolevel",2);   /* 705 KB per directory on trunk */
    make("mixed");touch("mixed","top.cue");siblings("mixed",'a',3900,170);   /* visited siblings are not held */
    {char z[400];name(z,'z',0,170);char zd[600];snprintf(zd,sizeof(zd),"mixed/%s",z);make(zd);siblings(zd,'a',2200,170);touch(zd,"deep.cue");}
    complete("mixed",2);
    printf("bb_index: cold trees complete as on trunk (peak %u of %u bytes, allocator %u, evictions %u, re-reads %u, worst slack %u): ok\n",
        bb_index_test_pending_peak(),cap,bb_index_test_pending_real_peak(),bb_index_test_evictions(),bb_index_test_rereads(),bb_index_test_worst_slack());
    /* Cache retention (the same trees seeded by a complete check first). A modest tree, then one directory whose own list
     * passes the cap (4,200 folders of 255 characters: LIMITED on trunk too): the check stops, the games stay, also read
     * back from the cache by a later start. */
    chain("retain",3,20,36);complete("retain",2);
    make("retain/wide");siblings("retain/wide",'w',4200,255);
    scan("retain");
    check_bounds();
    assert(bb_index_status()==BB_INDEX_LIMITED && bb_index_complete() && bb_index_ps1_count()==2);
    bb_index_shutdown();bb_index_start("retain",false);wait_done();
    assert(bb_index_ps1_count()==2 && bb_index_status()==BB_INDEX_LIMITED);
    /* An allocation failure (the first block): a limited result, nothing leaked, the cached games stay. */
    bb_index_test_fail_pending_at=1;
    scan("retain");
    bb_index_test_fail_pending_at=0;
    assert(bb_index_status()==BB_INDEX_LIMITED && bb_index_complete() && bb_index_ps1_count()==2);check_bounds();
    /* A scan cancelled while subfolders are waiting: held on the worker with pending bytes, cancelled, released. It
     * frees all of them and the cache of the last complete check stays. */
    bb_index_test_at_child=hold;armed=1;reached=release=0;
    bb_index_start("branch",true);
    for(int i=0;i<5000 && !__atomic_load_n(&reached,__ATOMIC_ACQUIRE);i++)usleep(1000);
    assert(reached && reached_pending>0);
    bb_index_shutdown();
    __atomic_store_n(&release,1,__ATOMIC_RELEASE);
    while(!bb_index_shutdown_done())usleep(1000);
    bb_index_test_at_child=NULL;
    assert(bb_index_test_pending_live()==0 && bb_index_test_pending_real_live()==0);
    bb_index_start("branch",false);wait_done();
    assert(bb_index_ps1_count()==2);
    bb_index_shutdown();while(!bb_index_shutdown_done())usleep(1000);
    puts("bb_index: limit, allocation failure and cancel free every pending folder and keep the cache: ok");
    mutation();
    return 0;
}
