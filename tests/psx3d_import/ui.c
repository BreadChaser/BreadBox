/* Production controller: bounded LOW callbacks, menu-owned metadata and
 * cancellation ownership, including close while a file is being copied. */
#define _GNU_SOURCE
#include <string.h>
#include <sys/types.h>
#include "app_log.h"
#include "ui.h"
#include "playlog.h"
#include "home.h"
#include "breadbox_import.h"
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#define SYSCLOCK_ARM11 1000000ULL
static u64 fake_tick=100;
static u64 svcGetSystemTick(void){return fake_tick;}
#include "../../app/ui/breadbox_import_ui.c"
static pthread_t ui,thread;
static bool launched,available=true,idle=true,close_worker,game_live;
static unsigned open_failures,memory_wait_logs,memory_ready_logs,memory_deferred_logs;
static unsigned runs,notices,failures,recoveries,steps,applies,commits,continued;
static u32 latest;
static unsigned hold_transfer,entered_transfer,copied_n=42;
static void (*fn)(void *),(*queued_fn)(void *);static void *arg,*queued_arg;
static bool queued;
static bool cleanup_refused,restart_available;
static unsigned cleanup_wait_logs;
bool bb_index_defer_cleanup(void (*f)(void *),void *a){return !cleanup_refused && bb_index_defer_low(f,a);}
static void *run(void *p){(void)p;usleep(1000);fn(arg);return NULL;}
bool bb_index_background_start(void){if(restart_available)available=true;return available;}
bool bb_index_defer_low(void (*f)(void *),void *a)
{
 if(!available)return false;
 if(!pthread_equal(ui,pthread_self())){assert(!queued);queued_fn=f;queued_arg=a;queued=true;return true;}
 assert(!launched);fn=f;arg=a;launched=true;assert(!pthread_create(&thread,NULL,run,NULL));return true;
}
bool bb_index_cancelled(void){return close_worker;}
bool bb_save_background_idle(void){return idle;}
bool bb_session_active(void){return game_live;}
void log_printf(const char *s,...){if(strstr(s,"retirement waiting"))cleanup_wait_logs++;if(strstr(s,"waiting for validation memory"))memory_wait_logs++;if(strstr(s,"validation memory available"))memory_ready_logs++;if(strstr(s,"deferred until next launch"))memory_deferred_logs++;}
void ui_notice(const char *s,int frames){assert(*s && frames>0);notices++;}
bool psx_import_recover(const char *s){(void)s;assert(pthread_equal(ui,pthread_self()));recoveries++;return true;}
struct psx_import_cursor {unsigned step,task_ticks;};
psx_import_cursor *psx_import_open(const char *s,const char *d,bool force)
{(void)s;(void)d;(void)force;assert(!pthread_equal(ui,pthread_self()));runs++;if(open_failures){open_failures--;errno=ENOMEM;return NULL;}return calloc(1,sizeof(psx_import_cursor));}
bool psx_import_step(psx_import_cursor *c,psx_import_result *r)
{
 assert(!pthread_equal(ui,pthread_self()));steps++;
 if(__atomic_load_n(&hold_transfer,__ATOMIC_ACQUIRE)) {
  __atomic_store_n(&entered_transfer,1,__ATOMIC_RELEASE);
  while(__atomic_load_n(&hold_transfer,__ATOMIC_ACQUIRE))usleep(1000);
 }
 *r=(psx_import_result){.copied=copied_n,.failed=failures};return ++c->step==3;
}
bool psx_import_changed(const psx_import_cursor *c){(void)c;return true;}
bool psx_import_finish(psx_import_cursor *c){(void)c;assert(!pthread_equal(ui,pthread_self()));commits++;return true;}
bool psx_import_task_step(psx_import_cursor *c,bool (*fn)(void *),void *ctx,bool *ok){if(++c->task_ticks%3)return false;*ok=fn(ctx);return true;}
bool psx_import_cancel_step(psx_import_cursor *c){(void)c;assert(!pthread_equal(ui,pthread_self()));return true;}
void psx_import_close(psx_import_cursor *c){assert(!c || !pthread_equal(ui,pthread_self()));free(c);}
u32 playlog_latest(void){assert(pthread_equal(ui,pthread_self()));return latest;}
char *playlog_import_psx3d_read(const char *p){(void)p;assert(!pthread_equal(ui,pthread_self()));return strdup("history");}
bool playlog_import_psx3d_apply(const char *p){assert(pthread_equal(ui,pthread_self()) && !strcmp(p,"history"));applies++;latest=100;return true;}
bool playlog_import_psx3d_commit(void){assert(!pthread_equal(ui,pthread_self()));return true;}
u32 playlog_last(const char *p){(void)p;return 0;}
void home_note_game(const char *p,const char *s,const char *t,const char *a){(void)p;(void)s;(void)t;(void)a;assert(pthread_equal(ui,pthread_self()));continued++;}
void psx_import_read_continue(const char *s,const char *d,psx_import_continue *r){(void)s;(void)d;memset(r,0,sizeof(*r));r->saved=100;strcpy(r->rom,"Game.bin");strcpy(r->state,"Exact Game (USA)");}
void psx_import_cursor_continue(const psx_import_cursor *c,psx_import_continue *r){(void)c;psx_import_read_continue(NULL,NULL,r);}
bool psx_import_continue_ready(const char *s,const char *d,const char *n,unsigned *t){(void)s;(void)d;(void)n;(void)t;return false;}
static void pump(void)
{
 if(launched){assert(!pthread_join(thread,NULL));launched=false;}
 if(queued){queued=false;fn=queued_fn;arg=queued_arg;launched=true;assert(!pthread_create(&thread,NULL,run,NULL));}
}
static void tick(void){pump();fake_tick+=SYSCLOCK_ARM11;breadbox_import_poll();}
static void drain(void){for(unsigned i=0;i<100 && (job || launched || queued);i++)tick();assert(!job && !launched && !queued);}
/* Review finding 5: the settings file copied at the first start counts (one cheat + settings: 2),
 * once; Import again says saves only */
static void settings_count(void)
{
 config_psx3d_imported=true;copied_n=1;breadbox_import_start(false);drain();
 assert(strstr(notice,"Brought your PSX3D saves and settings over: 2 files.") && !config_psx3d_imported);
 {unsigned kinds;assert(breadbox_import_settled(&kinds) && (kinds&PSX_IMPORT_SETTINGS));}   /* the welcome's PSX3D card names the settings */
 breadbox_import_notice_quiet();assert(!notice[0]);   /* its card says it: no second account on top */
 strcpy(notice,"PSX3D import paused: SD card full. It resumes next launch.");breadbox_import_notice_quiet();assert(notice[0]);   /* problems stay */
 breadbox_import_start(false);drain();assert(strstr(notice,"Brought your PSX3D saves over: 1 file."));
 config_psx3d_imported=true;copied_n=0;breadbox_import_start(false);drain();
 assert(strstr(notice,"Brought your PSX3D settings over: 1 file."));
 copied_n=42;
}
int main(void)
{
 ui=pthread_self();paused=true;breadbox_import_init();assert(recoveries==1); // unconditional even paused
 breadbox_import_resume();game_live=true;breadbox_import_start(true);assert(!job);game_live=false;idle=false;breadbox_import_start(true);assert(!job && !runs);
 idle=true;
 assert(bb_writers_try_exclusive());breadbox_import_poll();assert(!job);
 bb_writers_release_exclusive();
 available=false;breadbox_import_poll();assert(job && !launched);
 assert(bb_writers_busy() && !bb_writers_try_exclusive());
 available=true;breadbox_import_poll();assert(job && !runs); // room can draw before callback
 {unsigned kinds;assert(!breadbox_import_settled(&kinds));}   /* the welcome waits for it */
 tick();assert(steps>=1 && !finished && bb_writers_busy());drain();assert(!bb_writers_busy());assert(runs==1 && finished && applies==1 && commits==1 && continued==1);
 breadbox_import_notice();assert(notices==1);assert(strstr(notice,"saves over: 42 files."));
 assert(breadbox_import_pause() && finished);breadbox_import_resume();
 for(int i=0;i<100;i++)breadbox_import_start(true);
 assert(runs==1 && !job); // returns do not rescan
 breadbox_import_start(false);assert(strstr(notice,"Waiting"));drain();assert(runs==2);
 finished=false;breadbox_import_poll();assert(!breadbox_import_pause());
 drain();assert(!job && !finished); // cancellation before queued chunk
 breadbox_import_resume();breadbox_import_poll();drain();assert(finished);
 breadbox_import_start(false);breadbox_import_shutdown();assert(!job && !started && bb_writers_busy());
 tick();assert(!bb_writers_busy()); // queued callback owns reservation after HOME close
 breadbox_import_resume();breadbox_import_start(true);drain();assert(finished);
 __atomic_store_n(&hold_transfer,1,__ATOMIC_RELEASE);breadbox_import_start(false);
 for(int i=0;i<1000 && !__atomic_load_n(&entered_transfer,__ATOMIC_ACQUIRE);i++)usleep(1000);
 assert(entered_transfer);breadbox_import_shutdown();assert(!job && bb_writers_busy() && !bb_writers_try_exclusive());
 __atomic_store_n(&hold_transfer,0,__ATOMIC_RELEASE);tick();drain();assert(!bb_writers_busy());
 breadbox_import_resume();breadbox_import_start(true);drain();assert(finished);
 breadbox_import_start(false);
 for(unsigned i=0;;i++){assert(i<100);pump();if(job && job->phase==2 && job->done)break;fake_tick+=SYSCLOCK_ARM11;breadbox_import_poll();}
 assert(!breadbox_import_pause() && !job);drain();assert(!bb_writers_busy());
 breadbox_import_resume();breadbox_import_start(true);drain();assert(finished);
 failures=1;breadbox_import_start(false);drain();assert(finished && strstr(notice,"could not"));
 unsigned before=runs;breadbox_import_poll();assert(runs==before && !job);
 failures=0;open_failures=1;before=runs;breadbox_import_start(false);
 tick();assert(job && job->retry_at && !finished && !job->result.failed);
 assert(strstr(notice,"waiting for memory"));
 assert(!bb_writers_busy() && bb_writers_try_exclusive());
 fake_tick=job->retry_at;breadbox_import_poll();assert(runs==before+1);
 bb_writers_release_exclusive();
 breadbox_import_poll();assert(runs==before+1); // no immediate retry/spin
 drain();assert(finished && runs==before+2 && memory_wait_logs==1 && memory_ready_logs==1);
 open_failures=100;before=runs;breadbox_import_start(false);
 for(unsigned attempt=0;attempt<6;attempt++){
  tick();assert(!bb_writers_busy());
  if(attempt==5){assert(!job && finished);break;}
  assert(job && !job->result.failed);
  if(attempt<5){
   assert(job->retry_at==fake_tick-SYSCLOCK_ARM11+SYSCLOCK_ARM11*(2ULL<<attempt));
   u64 until=notice_until;fake_tick++;breadbox_import_poll();assert(notice_until==until);
   fake_tick=job->retry_at;breadbox_import_poll();
  }
 }
 breadbox_import_poll();assert(!job && finished && runs==before+6 && memory_wait_logs==2 && memory_deferred_logs==1);
 for(int i=0;i<100;i++){fake_tick+=SYSCLOCK_ARM11*60;breadbox_import_poll();}
 assert(runs==before+6 && !bb_writers_busy());
 assert(breadbox_import_pause() && finished);breadbox_import_resume();breadbox_import_start(true);assert(runs==before+6 && !job);breadbox_import_shutdown();
 open_failures=0;breadbox_import_resume();breadbox_import_start(true);drain();assert(finished);settings_count();breadbox_import_shutdown();
 /* Reproduce the S3 interleaving: the callback released after the UI
  * observed !done, leaving the UI with the final reference and a cursor. */
 import_job *race=calloc(1,sizeof(*race));assert(race);race->refs=1;
 race->cursor=calloc(1,sizeof(psx_import_cursor));assert(race->cursor);
 race->writer=bb_writer_begin();assert(race->writer);
 race->retired=true;__atomic_add_fetch(&retiring,1,__ATOMIC_RELEASE);
 cleanup_refused=true;unsigned waits=cleanup_wait_logs;
 release(race,false);assert(pending_cleanup==race && bb_writers_busy());
 for(unsigned i=0;i<3;i++)assert(!breadbox_import_pause());
 assert(cleanup_wait_logs==waits+1); // occupied slot is retained, reported once
 cleanup_refused=false;available=false;restart_available=true;
 assert(!breadbox_import_pause() && !pending_cleanup && launched);
 drain();assert(!breadbox_import_busy() && !bb_writers_busy());
 restart_available=false;
 puts("Automatic import: startup recovery, bounded LOW lane, UI metadata, once/return, queue retry, manual waiting, pause and HOME-close: PASS");
}

psx_import_io psx_import_io_counts(const psx_import_cursor *c){(void)c;return (psx_import_io){0};}
