#define _DEFAULT_SOURCE
#include "bb_index.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
static pthread_t ui_thread;
static int deferred_done;
static void deferred_read(void *arg) {
 assert(arg==&deferred_done && !pthread_equal(ui_thread,pthread_self()));
 __atomic_store_n(&deferred_done,1,__ATOMIC_RELEASE);
}
static int cleanup_done;
static void cleanup_job(void *unused){(void)unused;assert(!pthread_equal(ui_thread,pthread_self()));__atomic_store_n(&cleanup_done,1,__ATOMIC_RELEASE);}
static void noop_job(void *unused){(void)unused;}
static int held,release_hold,order,low_done,cancel_seen,visible_done,checkpoint;
/* A slow card: while set, every clock read is 10 minutes after the last (linked with -Wl,--wrap=clock_gettime). The
 * scan has no deadline, so a check that "takes hours" still reads the whole card. */
static int slow_card;
int __real_clock_gettime(clockid_t,struct timespec *);
int __wrap_clock_gettime(clockid_t c,struct timespec *t)
{
 static long long extra;int r=__real_clock_gettime(c,t);
 if(__atomic_load_n(&slow_card,__ATOMIC_ACQUIRE))__atomic_add_fetch(&extra,600,__ATOMIC_ACQ_REL);
 t->tv_sec+=__atomic_load_n(&extra,__ATOMIC_ACQUIRE);return r;
}
static void visible_job(void *unused){(void)unused;assert(order==1);order=2;__atomic_store_n(&visible_done,1,__ATOMIC_RELEASE);}
static void checkpoint_job(void *unused){(void)unused;__atomic_store_n(&held,1,__ATOMIC_RELEASE);while(!__atomic_load_n(&release_hold,__ATOMIC_ACQUIRE))usleep(1000);assert(bb_index_visible_checkpoint());assert(visible_done && order==2);__atomic_store_n(&checkpoint,1,__ATOMIC_RELEASE);}
static void hold_job(void *unused){(void)unused;__atomic_store_n(&held,1,__ATOMIC_RELEASE);while(!__atomic_load_n(&release_hold,__ATOMIC_ACQUIRE))usleep(1000);}
static void urgent_job(void *unused){(void)unused;assert(!order);order=1;}
static void low_job(void *unused){(void)unused;assert(order==2);order=3;__atomic_store_n(&low_done,1,__ATOMIC_RELEASE);}
static void cancellation_job(void *unused){(void)unused;assert(bb_index_cancelled());__atomic_store_n(&cancel_seen,1,__ATOMIC_RELEASE);}
static void until(int *value){for(int i=0;i<1000 && !__atomic_load_n(value,__ATOMIC_ACQUIRE);i++)usleep(1000);assert(*value);}
static double clock_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec/1e6;}
/* The shutdown's own waits, counted on the calling (UI) thread instead of timed: a loaded host can stretch 45
 * sleeps of 1 ms past any tight wall-clock limit (merge review: 85 ms once), but the bound is the number of them.
 * The device waits once in threadJoin for 45 ms (bb_index.c). Linked with -Wl,--wrap=usleep (run.sh). */
int __real_usleep(useconds_t);
static int counting,shutdown_polls;static unsigned long shutdown_sleep_us;
int __wrap_usleep(useconds_t us)
{
 if(__atomic_load_n(&counting,__ATOMIC_ACQUIRE) && pthread_equal(ui_thread,pthread_self())){shutdown_polls++;shutdown_sleep_us+=us;}
 return __real_usleep(us);
}
static void wait_done(void)
{
    for(int i=0;bb_index_busy() && i<1000;i++){bb_index_poll(true);usleep(10000);}
    assert(!bb_index_busy());
}
int main(int argc,char **argv)
{
    ui_thread=pthread_self();
    assert(!bb_index_defer(deferred_read,&deferred_done));
    assert(bb_index_background_start());
    assert(!bb_index_busy() && !bb_index_complete());
    assert(bb_index_defer(deferred_read,&deferred_done));until(&deferred_done);
    assert(!bb_index_busy() && bb_index_status()==BB_INDEX_IDLE && !bb_index_root()[0]);
    deferred_done=0;
    assert(bb_index_defer_low(deferred_read,&deferred_done));until(&deferred_done);
    assert(!bb_index_busy() && !bb_index_complete());
    bb_index_shutdown();deferred_done=0;
    char p[512]; bb_entry entry;
    if(argc==3 && !strcmp(argv[1],"changes")) {
        bb_index_start("roms",false);wait_done();
        assert(bb_index_complete() && bb_index_gba_count()==atoi(argv[2]));
        bb_index_shutdown();puts("bb_index: automatic added/removed files: ok");return 0;
    }
    const bb_system_t *cart=bb_system_by_id("gba");
    assert(bb_index_folder_hint("(2001) Nintendo Game Boy Advance")==1);
    assert(bb_index_folder_hint("Sony-PlayStation")==1);
    assert(bb_index_folder_hint("(1994) Sony PlayStation - V1.1")==1);
    assert(bb_index_root_normalize("sdmc:/roms/(1994) Sony PlayStation - V1.0",p,sizeof(p)) && !strcmp(p,"sdmc:/roms"));
    assert(bb_index_root_normalize("sdmc:/roms/(2001) Nintendo Game Boy Advance - V1.2",p,sizeof(p)) && !strcmp(p,"sdmc:/roms"));
    assert(bb_index_root_normalize("sdmc:/roms/gba",p,sizeof(p)) && !strcmp(p,"sdmc:/roms"));
    assert(bb_index_root_normalize("sdmc:/roms/gb",p,sizeof(p)) && !strcmp(p,"sdmc:/roms"));
    assert(bb_index_discover("sdmc:/roms/(1994) Sony PlayStation - V1.0",p,sizeof(p)) && !strcmp(p,"sdmc:/roms"));
    assert(bb_index_folder_hint("1989")==-1);
    assert(bb_index_folder_hint("(1998) Nintendo Game Boy Color")==-1);
    if (argc == 1) {
        assert(bb_index_discover(NULL,p,sizeof(p)) && !strcmp(p,"sdmc:/roms"));
        bb_index_start(p,false);
        for(int i=0;!bb_index_gba_count() && bb_index_busy() && i<1000;i++)usleep(1000);
        assert(bb_index_gba_count()>0 && bb_index_busy()); /* first-run filling */
        wait_done();
        assert(bb_index_gba_count()==81 && bb_index_complete());
        bb_index_shutdown();
    }
    assert(bb_index_discover("missing",p,sizeof(p)) && !strcmp(p,"sdmc:/roms"));
    assert(bb_index_folder_hint("GB.C")==-1);
    assert(bb_index_discover("roms",p,sizeof(p)) && !strcmp(p,"roms"));
    bb_index_start("roms/",false);
    if(argc>1 && !strcmp(argv[1],"cached")) {
        /* A separate process shows the persisted games before validating an
         * empty directory; removed files disappear after validation. */
        for(int i=0;!bb_index_gba_count() && bb_index_busy() && i<1000;i++)usleep(1000);
        assert(bb_index_gba_count()==3 && bb_index_busy() && bb_index_complete());
        wait_done();assert(bb_index_gba_count()==0 && bb_index_ps1_count()==0);
        bb_index_shutdown();puts("bb_index: cached-first, background removals: ok");return 0;
    }
    wait_done();
    assert(!strcmp(bb_index_root(),"roms"));
    assert(bb_index_complete() && bb_index_status()==BB_INDEX_COMPLETE);
    assert(bb_index_defer(deferred_read,&deferred_done));
    for(int i=0;!__atomic_load_n(&deferred_done,__ATOMIC_ACQUIRE) && i<1000;i++)usleep(1000);
    assert(deferred_done);
    if(argc==1) {
      assert(bb_index_defer(hold_job,NULL));until(&held);
      assert(bb_index_defer_low(low_job,NULL));assert(bb_index_defer(urgent_job,NULL));assert(bb_index_defer_visible(visible_job,NULL));
      __atomic_store_n(&release_hold,1,__ATOMIC_RELEASE);until(&low_done);assert(order==3);
      held=release_hold=visible_done=0;order=1;assert(bb_index_defer_low(checkpoint_job,NULL));until(&held);
      assert(bb_index_defer_visible(visible_job,NULL));__atomic_store_n(&release_hold,1,__ATOMIC_RELEASE);until(&checkpoint);
      held=release_hold=0;assert(bb_index_defer(hold_job,NULL));until(&held);
      assert(bb_index_defer(cancellation_job,NULL));double start=clock_ms();
      __atomic_store_n(&counting,1,__ATOMIC_RELEASE);bb_index_shutdown();__atomic_store_n(&counting,0,__ATOMIC_RELEASE);
      /* a still-blocked syscall must not hold the UI join: it waits its 45 polls of 1 ms, no more, and returns
       * with the worker still running; the wall-clock limit only catches a wait of a different kind */
      assert(shutdown_polls==45 && shutdown_sleep_us==45000);assert(clock_ms()-start<1000);assert(!bb_index_shutdown_done());
      bb_index_start("roms",false);__atomic_store_n(&release_hold,1,__ATOMIC_RELEASE);
      // Synchronous library progress services the reap without adapter polling.
      for(int i=0;i<1000 && !bb_index_complete();i++){bb_index_service();usleep(1000);}
      wait_done();until(&cancel_seen);assert(bb_index_complete() && bb_index_gba_count()==3);
      puts("bb_index: visible-read priority, cancellation, bounded shutdown/restart: ok");
    }
    if(argc>1 && !strcmp(argv[1],"cached")) {
        assert(bb_index_gba_count()==3);
    } else {
        assert(bb_index_gba_count()==3);
        assert(bb_index_ps1_count()==2);
        assert(!bb_index_get(cart,-1,p,sizeof(p),&entry));
        assert(!bb_index_get(cart,0,p,2,&entry));
        assert(bb_index_get(cart,0,p,sizeof(p),&entry));
        assert(entry.title[0] && entry.key[0]);
        unsigned gen=bb_index_generation();
        bb_index_start("roms",false);
        assert(!bb_index_busy() && gen==bb_index_generation());
        // New root + refresh requests cancel old work; getters remain safe.
        for(int i=0;i<20;i++) {
            bb_index_start(i%2?"roms":"empty",true);
            (void)bb_index_get(cart,0,p,sizeof(p),&entry);
        }
        bb_index_start("roms",true); wait_done();
        assert(bb_index_gba_count()==3 && bb_index_ps1_count()==2);
    }
    // Forced validation keeps the current snapshot visible even before the
    // worker can start scanning. It must never publish an empty replacement.
    held=release_hold=0;assert(bb_index_defer(hold_job,NULL));until(&held);
    unsigned visible_generation=bb_index_generation();
    assert(!remove("index")); // successful refresh must persist even an unchanged retained snapshot
    bb_index_start("roms",true);
    assert(bb_index_complete() && bb_index_gba_count()==3);
    assert(bb_index_generation()==visible_generation);
    __atomic_store_n(&release_hold,1,__ATOMIC_RELEASE);wait_done();
    assert(!access("index",F_OK));
    unsigned before_game=bb_index_generation();
    bb_index_shutdown();
    assert(!bb_index_busy() && !bb_index_complete() && bb_index_gba_count()==0);
    bb_index_start("roms",false); wait_done();
    assert(bb_index_gba_count()==3);
    // a game in between: the same games read back from the cache keep their generation
    assert(bb_index_generation()==before_game);
    if(argc == 1) {
        unsigned scans=bb_index_scans();   // each finished scan is a new result (the Library tells an early stop once per scan)
        /* bb_index_counts_key: unchanged by a check that finds the same games, changed by a different list and by
           the list going away (a game) and coming back */
        {
            bb_index_start("roms",true); wait_done();
            uint32_t k=bb_index_counts_key();
            bb_index_start("roms",true); wait_done(); assert(bb_index_counts_key()==k);
            bb_index_start("many",true); wait_done(); assert(bb_index_counts_key()!=k);
            uint32_t many=bb_index_counts_key();
            bb_index_shutdown(); assert(bb_index_counts_key()!=many);
            bb_index_start("roms",false); wait_done(); assert(bb_index_counts_key()==k);
            scans=bb_index_scans();
        }
        /* folders over 12 deep are skipped the same way each time: not an early stop */
        bb_index_start("deep",true); wait_done();
        assert(bb_index_gba_count()==1 && bb_index_complete() && bb_index_status()==BB_INDEX_COMPLETE && bb_index_scans()==scans+1);
        /* a big folder on a slow card: no time, file or folder limit, every game is found */
        __atomic_store_n(&slow_card,1,__ATOMIC_RELEASE);
        bb_index_start("many",true); wait_done();
        __atomic_store_n(&slow_card,0,__ATOMIC_RELEASE);
        assert(bb_index_ps1_count()==8300 && bb_index_complete() && bb_index_status()==BB_INDEX_COMPLETE && bb_index_scans()==scans+2);
        /* a library that grows past the memory limit (MAX_ITEMS 12,288) keeps the games the last complete check
           found: the check stops at the limit (LIMITED), the shown list and the cache stay */
        bb_index_start("grow",true); wait_done();
        assert(bb_index_ps1_count()==10 && bb_index_status()==BB_INDEX_COMPLETE);
        for(int i=0;i<12400;i++){char f[64];snprintf(f,sizeof(f),"grow/more%05d.bin",i);FILE *o=fopen(f,"wb");assert(o);fclose(o);}
        bb_index_start("grow",true); wait_done();
        assert(bb_index_ps1_count()==10 && bb_index_complete() && bb_index_status()==BB_INDEX_LIMITED);
        bb_index_shutdown();bb_index_start("grow",false); wait_done();   /* a later start: the cache is the complete list */
        assert(bb_index_ps1_count()==10 && bb_index_status()==BB_INDEX_LIMITED);
        for(int i=0;i<12400;i++){char f[64];snprintf(f,sizeof(f),"grow/more%05d.bin",i);assert(!remove(f));}   /* the next process starts from 10 again */
        bb_index_start("roms",true); wait_done();
        assert(bb_index_complete() && bb_index_status()==BB_INDEX_COMPLETE);
    }
    // An empty completed snapshot must lose readiness on forced rescan,
    // even while its worker is held before the first directory read.
    bb_index_start("empty",true);wait_done();assert(bb_index_complete());
    held=release_hold=0;assert(bb_index_defer(hold_job,NULL));until(&held);
    bb_index_start("empty",true);assert(bb_index_busy() && !bb_index_complete());
    __atomic_store_n(&release_hold,1,__ATOMIC_RELEASE);wait_done();assert(bb_index_complete());
    bb_index_start("roms",true);wait_done(); // preserve the fixture cache for the next process
    bb_index_start("does-not-exist",true); wait_done();
    assert(bb_index_status()==BB_INDEX_ERROR && !bb_index_complete());
    bb_index_start("many",true);
    bb_index_shutdown(); // joining during an active request must be safe
    if(argc == 1) {
        // Prefer a populated common root over an empty conventional folder.
        /* Conventional empty roms root and populated games root are fixtures. */
        assert(bb_index_discover("missing",p,sizeof(p)) && !strcmp(p,"sdmc:/roms"));
        // Explicitly chosen existing folders always win, including empty ones.
        assert(bb_index_discover("sdmc:/roms",p,sizeof(p)) && !strcmp(p,"sdmc:/roms"));
        assert(bb_index_discover("sdmc:/roms/(2001) Nintendo Game Boy Advance - V1.2",p,sizeof(p)) && !strcmp(p,"sdmc:/roms"));
        mkdir("sdmc:/3ds",0700);mkdir("sdmc:/3ds/PSX3D",0700);
        FILE *f=fopen("sdmc:/3ds/PSX3D/config.ini","w");assert(f);fputs("games_dir=roms\n",f);fclose(f);
        assert(bb_index_discover(NULL,p,sizeof(p)) && !strcmp(p,"sdmc:/roms"));
        assert(!remove("sdmc:/3ds/PSX3D/config.ini"));
        assert(!mkdir("sdmc:/(1994) Sony PlayStation - V1.1",0700));
        assert(bb_index_discover(NULL,p,sizeof(p)) && !strcmp(p,"sdmc:/roms"));
        assert(!rmdir("sdmc:/(1994) Sony PlayStation - V1.1"));
    }
    assert(bb_index_background_start());held=release_hold=0;
    assert(bb_index_defer(hold_job,NULL));until(&held);
    for(unsigned i=0;i<16;i++)assert(bb_index_defer_low(noop_job,NULL));
    assert(!bb_index_defer_low(noop_job,NULL));
    assert(bb_index_defer_cleanup(cleanup_job,NULL));
    assert(!bb_index_defer_cleanup(cleanup_job,NULL));
    __atomic_store_n(&release_hold,1,__ATOMIC_RELEASE);until(&cleanup_done);
    bb_index_shutdown();while(!bb_index_shutdown_done())usleep(1000);
    assert(!bb_index_defer_cleanup(noop_job,NULL)); // sealed/stopped worker rejects
    assert(bb_index_background_start());
    assert(bb_index_defer_cleanup(noop_job,NULL)); // restart can retire stranded work
    bb_index_shutdown();while(!bb_index_shutdown_done())usleep(1000);
    // The exit's final shutdown: after the close nothing starts a worker again (an
    // importer's retry_cleanup, a queued or new scan), so that shutdown's worker was the last.
    bb_index_close();
    assert(!bb_index_background_start());
    assert(!bb_index_defer_cleanup(noop_job,NULL));
    bb_index_start("sdmc:/roms",false);
    assert(!bb_index_busy() && bb_index_shutdown_done());
    bb_index_service();
    assert(!bb_index_background_start() && bb_index_shutdown_done());
    puts("bb_index: ok");
    return 0;
}
