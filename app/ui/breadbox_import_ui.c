/* Copy-only migration. The UI publishes metadata/notice; LOW callbacks grant
 * bounded transfer permits to the suspended importer transaction. */
#include "app_policy.h"
#include "cold_code.h"
#include "app_log.h"
#include "ui.h"
#include "playlog.h"
#include "home.h"
#include "breadbox_import.h"
#include "bb_index.h"
#include "bb_save.h"
#include "bb_writers.h"
#include "bb_registry.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
extern u32 playlog_latest(void);
extern char *playlog_import_psx3d_read(const char *);
extern bool playlog_import_psx3d_apply(const char *);
extern bool playlog_import_psx3d_commit(void);
typedef struct {
 psx_import_result result; psx_import_cursor *cursor;
 unsigned done,cancelled,refs,phase; bool force,changed,final,writer,memory_wait,retired;
 unsigned memory_attempts,max_metadata; bool memory_notice;
 u64 queued_at,last_queued,queue_ticks,run_ticks,max_ticks,retry_at; unsigned chunks; u32 newest;
 char *history;psx_import_continue resume;
} import_job;
static import_job *job;
static unsigned retiring;
/* One predecessor at most: no successor starts until retirement completes. */
static import_job *pending_cleanup;
static bool cleanup_warned;
static PSX3D_COLD void worker(void *ctx);
static PSX3D_COLD void retry_cleanup(void)
{
 if(!pending_cleanup)return;
 if(bb_index_defer_cleanup(worker,pending_cleanup) ||
    (bb_index_background_start() && bb_index_defer_cleanup(worker,pending_cleanup))){pending_cleanup=NULL;cleanup_warned=false;return;}
 if(!cleanup_warned){log_printf("import: retirement waiting for index cleanup worker\n");cleanup_warned=true;}
}
static bool started,finished,paused,recovered,force_next;
static unsigned brought;   /* PSX_IMPORT_* kinds copied this start (breadbox_import_settled) */
bool config_psx3d_imported;
static char notice[160],notice_next[160];   /* next: shown when the first one ends */
static u64 notice_until;
static PSX3D_COLD void say(const char *text)
{snprintf(notice,sizeof(notice),"%s",text);notice_next[0]=0;notice_until=svcGetSystemTick()+SYSCLOCK_ARM11*8ULL;}
/* Startup repair precedes all cover/game pause paths, even with no source or
 * a durable finished receipt. Only importer-owned destination files change. */
PSX3D_COLD void breadbox_import_init(void)
{
 recovered=psx_import_recover(APP_SD_ROOT);
 if(!recovered)say("PSX3D saves need recovery. Check free space on the SD card.");
}
PSX3D_COLD bool breadbox_import_busy(void){return job!=NULL || __atomic_load_n(&retiring,__ATOMIC_ACQUIRE)!=0;}
static PSX3D_COLD void release(import_job *j,bool on_worker)
{
 if(__atomic_sub_fetch(&j->refs,1,__ATOMIC_ACQ_REL)==0){
  if(j->cursor && !on_worker){
   /* The worker may have dropped its reference after retire_job saw !done.
    * Retain the last reference and transfer cleanup instead of unwinding here. */
   __atomic_store_n(&j->refs,1,__ATOMIC_RELEASE);
   __atomic_store_n(&j->cancelled,1,__ATOMIC_RELEASE);
   if(!j->retired){j->retired=true;__atomic_add_fetch(&retiring,1,__ATOMIC_RELEASE);}
   pending_cleanup=j;retry_cleanup();return;
  }
  psx_import_close(j->cursor);free(j->history);if(j->writer)bb_writer_end();if(j->retired)__atomic_sub_fetch(&retiring,1,__ATOMIC_RELEASE);free(j);}
}
static PSX3D_COLD bool read_history(void *ctx)
{import_job *j=ctx;j->history=playlog_import_psx3d_read(APP_LEGACY_PSX3D_ROOT "/saves/playlog.ini");return j->history!=NULL;}
static PSX3D_COLD bool commit_history(void *ctx)
{(void)ctx;return playlog_import_psx3d_commit();}
static PSX3D_COLD bool finish_manifest(void *ctx)
{import_job *j=ctx;return psx_import_finish(j->cursor);}
static PSX3D_COLD void worker(void *ctx)
{
 import_job *j=ctx;u64 start=svcGetSystemTick();
 uint64_t metadata_before=j->cursor?psx_import_io_counts(j->cursor).metadata_calls:0;
 if(j->last_queued)j->queue_ticks+=start-j->last_queued;
 if(!j->chunks)log_printf("import: lane wait %lu ms (LOW shared index worker)\n",(unsigned long)((start-j->queued_at)/(SYSCLOCK_ARM11/1000)));
 if(__atomic_load_n(&j->cancelled,__ATOMIC_ACQUIRE) || bb_index_cancelled()) {
  j->result.interrupted=true;j->final=!j->cursor || psx_import_cancel_step(j->cursor);
 } else if(j->phase==0) {
  if(!j->cursor){errno=0;j->cursor=psx_import_open(APP_LEGACY_PSX3D_ROOT,APP_SD_ROOT,j->force);}
  if(!j->cursor && errno==ENOMEM){
   if(!j->memory_wait)log_printf("import: waiting for validation memory\n");
   j->memory_wait=true;
   /* Six attempts over 62 seconds, then leave receipts for the next boot.
    * Waiting owns no filesystem work and must not block update installation. */
   if(++j->memory_attempts>=6){log_printf("import: memory unavailable; deferred until next launch\n");j->final=true;}
   else j->retry_at=svcGetSystemTick()+SYSCLOCK_ARM11*(1ULL<<j->memory_attempts);
   bb_writer_end();j->writer=false;goto measured;
  }
  if(j->cursor && j->memory_wait){log_printf("import: validation memory available\n");j->memory_wait=false;}
  if(!j->cursor){log_printf("import: failure op=open-cursor path=%s errno=%d\n",APP_LEGACY_PSX3D_ROOT,errno);j->result.failed++;j->final=true;}
  else if(psx_import_step(j->cursor,&j->result)) {
   j->changed=psx_import_changed(j->cursor);
   if(j->result.failed || j->result.interrupted)j->final=true;
   else j->phase=j->changed?1:3;
  }
 } else if(j->phase==1) {
  bool ok;if(!psx_import_task_step(j->cursor,read_history,j,&ok))goto measured;
  if(!ok){j->final=true;log_printf("import: failure op=read-playlog path=%s errno=%d\n",APP_LEGACY_PSX3D_ROOT "/saves/playlog.ini",errno);j->result.failed++;}
  psx_import_cursor_continue(j->cursor,&j->resume);j->phase=2;
 } else if(j->phase==3 && j->changed) {
  bool ok;errno=0;if(!psx_import_task_step(j->cursor,commit_history,j,&ok))goto measured;
  if(!ok){log_printf("import: failure op=commit-playlog path=%s errno=%d\n",APP_SD_ROOT "/saves/playlog.ini",errno);j->result.failed++;if(errno==ENOSPC)j->result.no_space=true;}
  j->phase=4;
 } else {
  bool ok=true;errno=0;if(!j->result.failed && !psx_import_task_step(j->cursor,finish_manifest,j,&ok))goto measured;
  if(!ok){log_printf("import: failure op=publish-manifest path=%s errno=%d\n",APP_SD_ROOT "/import/psx3d/finished",errno);j->result.failed++;if(errno==ENOSPC)j->result.no_space=true;}
  j->final=true;
 }
 /* Cancellation unwinds open handles on the worker too. Include that close
  * time in the chunk maximum rather than hiding it after instrumentation. */
measured:;
 psx_import_io final_io={0};bool had_cursor=j->cursor!=NULL;
 psx_import_io callback_io=j->cursor?psx_import_io_counts(j->cursor):(psx_import_io){0};
 if(j->final && j->cursor){final_io=callback_io;psx_import_close(j->cursor);j->cursor=NULL;}
 unsigned metadata=(unsigned)(callback_io.metadata_calls-metadata_before);
 if(metadata>j->max_metadata)j->max_metadata=metadata;
 if(metadata>1)log_printf("import: metadata permit violation count=%u op=%s path=%s\n",metadata,callback_io.last_op,callback_io.last_path);
 u64 elapsed=svcGetSystemTick()-start;
 if(elapsed>SYSCLOCK_ARM11/20)log_printf("import: slow callback phase=%u us=%lu op=%s path=%s metadata=%llu\n",j->phase,(unsigned long)(elapsed/(SYSCLOCK_ARM11/1000000)),callback_io.last_op[0]?callback_io.last_op:"reserve/dispatch",callback_io.last_path[0]?callback_io.last_path:APP_LEGACY_PSX3D_ROOT,(unsigned long long)(callback_io.metadata_calls-metadata_before));
 j->chunks++;j->run_ticks+=elapsed;if(elapsed>j->max_ticks)j->max_ticks=elapsed;
 if(!(j->chunks%512) && j->cursor){psx_import_io io=psx_import_io_counts(j->cursor);
  log_printf("import: progress chunks=%u worker=%lu ms read=%llu written=%llu\n",j->chunks,(unsigned long)(j->run_ticks/(SYSCLOCK_ARM11/1000)),(unsigned long long)io.read_bytes,(unsigned long long)io.written_bytes);}
 if(j->final) {
  log_printf("import: lane total wait=%lu ms wall=%lu ms metadata_max=%u\n",(unsigned long)(j->queue_ticks/(SYSCLOCK_ARM11/1000)),(unsigned long)((svcGetSystemTick()-j->queued_at)/(SYSCLOCK_ARM11/1000)),j->max_metadata);
  log_printf("import: chunks mean=%lu us max=%lu ms (includes SD wait)\n",(unsigned long)(j->run_ticks/(SYSCLOCK_ARM11/1000000)/j->chunks),(unsigned long)(j->max_ticks/(SYSCLOCK_ARM11/1000)));
  if(had_cursor){psx_import_io io=final_io;log_printf("import: io read=%llu written=%llu bytes\n",(unsigned long long)io.read_bytes,(unsigned long long)io.written_bytes);log_printf("import: read calls=%llu elapsed=%llu ms max=%llu us\n",(unsigned long long)io.read_calls,(unsigned long long)(io.read_us/1000),(unsigned long long)io.max_read_us);log_printf("import: metadata dir=%llu open=%llu stat=%llu close=%llu seek=%llu ms\n",(unsigned long long)(io.dir_us/1000),(unsigned long long)(io.open_us/1000),(unsigned long long)(io.stat_us/1000),(unsigned long long)(io.close_us/1000),(unsigned long long)(io.seek_us/1000));}
  if(had_cursor)log_printf("import: phases write=%llu us calls=%llu max=%llu us flush=%llu us mutate=%llu us crc=%llu us bytes=%llu\n",
   (unsigned long long)final_io.write_us,(unsigned long long)final_io.write_calls,(unsigned long long)final_io.max_write_us,
   (unsigned long long)final_io.flush_us,(unsigned long long)final_io.mutate_us,(unsigned long long)final_io.crc_us,(unsigned long long)final_io.crc_bytes);
  if(had_cursor)log_printf("import: calls dir=%llu open=%llu stat=%llu close=%llu seek=%llu flush=%llu mutate=%llu\n",
   (unsigned long long)final_io.dir_calls,(unsigned long long)final_io.open_calls,(unsigned long long)final_io.stat_calls,
   (unsigned long long)final_io.close_calls,(unsigned long long)final_io.seek_calls,(unsigned long long)final_io.flush_calls,(unsigned long long)final_io.mutate_calls);
  log_printf("import: auto %lu ms chunks=%u copied=%u unchanged=%u conflicts=%u rejected=%u failed=%u interrupted=%d changed=%d\n",
   (unsigned long)(j->run_ticks/(SYSCLOCK_ARM11/1000)),j->chunks,j->result.copied,j->result.unchanged,j->result.conflicts,j->result.rejected,j->result.failed,j->result.interrupted,j->changed);
 }
 /* Transfer the callback's reference to a new LOW queue entry. Visible and
  * normal jobs still win the next turn; waiting for the UI to grant each
  * permit would add a frame of artificial delay to every 8 ms slice. */
 j->last_queued=svcGetSystemTick();
 if(!j->final && (j->result.interrupted || __atomic_load_n(&j->cancelled,__ATOMIC_ACQUIRE) || bb_index_cancelled()) && bb_index_defer_cleanup(worker,j))return;
 if(!j->final && !j->retry_at && j->phase!=2 && !__atomic_load_n(&j->cancelled,__ATOMIC_ACQUIRE) &&
    !bb_index_cancelled() && bb_index_defer_low(worker,j))return;
 __atomic_store_n(&j->done,1,__ATOMIC_RELEASE);release(j,true);
}
/* Transfer the UI reference to allocation-free worker teardown. No cursor
 * unwind, file close, rollback or tmp removal is allowed on the menu thread. */
static PSX3D_COLD bool retire_job(void)
{
 import_job *j=job;if(!j)return true;
 __atomic_store_n(&j->cancelled,1,__ATOMIC_RELEASE);
 if(!__atomic_load_n(&j->done,__ATOMIC_ACQUIRE)){j->retired=true;__atomic_add_fetch(&retiring,1,__ATOMIC_RELEASE);release(j,false);job=NULL;return true;}
 if(!j->cursor){release(j,false);job=NULL;return true;}
 j->retired=true;__atomic_add_fetch(&retiring,1,__ATOMIC_RELEASE);
 pending_cleanup=j;retry_cleanup();
 job=NULL;return true;
}
PSX3D_COLD void breadbox_import_poll(void)
{
 retry_cleanup();
 if(job && __atomic_load_n(&job->cancelled,__ATOMIC_ACQUIRE)){
  if(retire_job())finished=false;
  return;
 }
 if(job && __atomic_load_n(&job->done,__ATOMIC_ACQUIRE)) {
  if(job->final) {
   psx_import_result r=job->result;release(job,false);job=NULL;finished=!r.interrupted;
   brought|=r.kinds|(config_psx3d_imported?PSX_IMPORT_SETTINGS:0u);
   if(r.failed)say(r.no_space?"PSX3D import paused: SD card full. It resumes next launch.":"Some PSX3D saves could not be copied. Try Import again in Files.");
   else if(r.copied || config_psx3d_imported) {
    /* every file that came over, the settings file (config_import_psx3d, once) included */
    char text[160],backups[48]="";unsigned n=r.copied+(config_psx3d_imported?1u:0u);
    if(r.conflicts)snprintf(backups,sizeof(backups)," (%u kept in Backups)",r.conflicts);
    snprintf(text,sizeof(text),"Brought your PSX3D %s over: %u file%s.%s",!config_psx3d_imported?"saves":r.copied?"saves and settings":"settings",
     n,n==1?"":"s",backups);say(text);
    /* J-08: PSX3D counted postcards from 0, BreadBox from 1 */
    if(r.slots)snprintf(notice_next,sizeof(notice_next),"%s","PSX3D postcard slot 0 is now slot 1, and so on.");
   }
   config_psx3d_imported=false;   /* Import again never copies the settings again */
  } else if(job->retry_at) {
   if(!job->memory_notice){say("PSX3D import is waiting for memory…");job->memory_notice=true;}
   if(svcGetSystemTick()<job->retry_at)return;
   job->retry_at=0;
  } else if(job->phase==2) {
   /* Owned text handoff: no filesystem read or table mutation on the worker. */
   if(!playlog_import_psx3d_apply(job->history)){log_printf("import: failure op=apply-playlog path=%s errno=%d\n",APP_LEGACY_PSX3D_ROOT "/saves/playlog.ini",errno);job->result.failed++;}
   free(job->history);job->history=NULL;
   u32 last=playlog_last(job->resume.save[0]?job->resume.save:job->resume.state);
   if(job->resume.saved>last)last=job->resume.saved;
   if(job->resume.saved && last>job->newest && last>=playlog_latest())
    home_note_game(job->resume.rom,job->resume.save,job->resume.title,job->resume.state);
   job->phase=3;
  }
 }
 if(__atomic_load_n(&retiring,__ATOMIC_ACQUIRE))return;
 if(!started || finished || paused || !recovered || bb_session_active() || !bb_save_background_idle())return;
 if(!job) {
  /* Reserve before enqueueing, through all LOW turns and final close.
   * The last callback retains the reservation after UI shutdown. */
  if(!bb_writer_begin())return;
  job=calloc(1,sizeof(*job));if(!job){bb_writer_end();return;}
  job->writer=true;job->refs=1;job->done=1;job->force=force_next;force_next=false;
  job->newest=playlog_latest();job->queued_at=svcGetSystemTick();
 }
 if(!__atomic_load_n(&job->done,__ATOMIC_ACQUIRE))return;
 if(!job->writer){if(!bb_writer_begin())return;job->writer=true;}
 if(!bb_index_background_start())return;
 __atomic_store_n(&job->done,0,__ATOMIC_RELEASE);__atomic_add_fetch(&job->refs,1,__ATOMIC_RELAXED);
 job->last_queued=svcGetSystemTick();
 if(!bb_index_defer_low(worker,job)){__atomic_store_n(&job->done,1,__ATOMIC_RELEASE);release(job,false);}
}
PSX3D_COLD void breadbox_import_start(bool first_boot)
{
 started=true;
 if(!first_boot){say("Waiting for saves…");if(!job){finished=false;force_next=true;}}
 breadbox_import_poll();
}
PSX3D_COLD bool breadbox_import_pause(void)
{
 paused=true;retry_cleanup();
 if(job && retire_job())finished=false;
 /* A retired callback may still own the writer; the caller retries next frame. */
 return !breadbox_import_busy();
}
PSX3D_COLD void breadbox_import_resume(void){paused=false;}
/* Settled: finished, or no import can run this start (recovery failed). */
PSX3D_COLD bool breadbox_import_settled(unsigned *kinds)
{
 *kinds=brought;
 return !recovered || (started && finished && !job);
}
PSX3D_COLD void breadbox_import_notice(void)
{
 if(notice[0] && svcGetSystemTick()>=notice_until && notice_next[0]){
  snprintf(notice,sizeof(notice),"%s",notice_next);notice_next[0]=0;notice_until=svcGetSystemTick()+SYSCLOCK_ARM11*8ULL;
 }
 if(notice[0] && svcGetSystemTick()<notice_until)ui_notice(notice,60);
}
PSX3D_COLD void breadbox_import_notice_quiet(void)
{
 if(strncmp(notice,"Brought your PSX3D ",19))return;
 if(notice_next[0]){snprintf(notice,sizeof(notice),"%s",notice_next);notice_next[0]=0;notice_until=svcGetSystemTick()+SYSCLOCK_ARM11*8ULL;}
 else notice[0]=0;
}
PSX3D_COLD void breadbox_import_shutdown(void)
{
 paused=true;started=false;finished=false;
 (void)retire_job();
}
