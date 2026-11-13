#define _GNU_SOURCE
#include <stddef.h>
#include <string.h>
#include "app_log.h"
#include "playlog.h"
#include "bb_save.h"
#include <assert.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
/* the later thread (bb_save.c): here and now */
/* the later thread (bb_save.c): here and now, or held in order until run_queue (deferred) */
static int deferred,nq;static struct {void (*fn)(void *);void *arg;} q[64];
void bb_later(void (*fn)(void *),void *arg){if(deferred){q[nq].fn=fn;q[nq++].arg=arg;}else fn(arg);}
static void run_queue(void){for(int i=0;i<nq;i++)q[i].fn(q[i].arg);nq=0;}
void bb_save_settle(void){run_queue();}
static bool file_has(const char *path,const char *text)
{
 FILE *f=fopen(path,"r");if(!f)return false;char buf[8192];size_t n=fread(buf,1,sizeof(buf)-1,f);fclose(f);buf[n]=0;
 return strstr(buf,text)!=NULL;
}
static bool refuse_defer;
bool bb_index_defer(void (*fn)(void *),void *arg) {if(refuse_defer)return false;bb_later(fn,arg);return true;}
bool bb_index_defer_low(void (*fn)(void *),void *arg){return bb_index_defer(fn,arg);}
bool bb_index_cancelled(void){return false;}
static u64 tick_now;
u64 svcGetSystemTick(void){return tick_now;}
void log_printf(const char *fmt,...){(void)fmt;}
extern void playlog_adopt_key_later(const char *,const char *);
extern bool playlog_poll_adoptions(void);
extern u32 playlog_latest(void);
extern bool playlog_import_psx3d(const char *);
int main(void) {
 char dir[]="/tmp/bb-log-XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
 mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir("sdmc:/3ds/BreadBox",0700);mkdir("sdmc:/3ds/BreadBox/saves",0700);
 assert(playlog_toggle_favourite("TEST"));
 playlog_adopt_key("TEST","gba-TEST-12345678");assert(playlog_favourite("gba-TEST-12345678"));
 assert(!playlog_toggle_favourite("gba-TEST-12345678"));
 playlog_adopt_key("TEST","gba-TEST-12345678");assert(!playlog_favourite("gba-TEST-12345678"));
 assert(playlog_favourite("TEST"));
 assert(playlog_toggle_favourite("gba:SAME"));
 assert(!playlog_favourite("ps1:SAME"));
 assert(playlog_toggle_favourite("ps1:SAME"));
 assert(!playlog_toggle_favourite("gba:SAME"));
 assert(playlog_favourite("ps1:SAME"));
 playlog_adopt_key("TEST","gba:TEST");assert(playlog_favourite("gba:TEST"));
 assert(!playlog_toggle_favourite("gba:TEST"));
 playlog_adopt_key("TEST","gba:TEST");assert(!playlog_favourite("gba:TEST"));
 assert(!access("sdmc:/3ds/BreadBox/saves/gba_TEST.playlog-id",F_OK));

 assert(playlog_toggle_favourite("BOUND_OLD1"));assert(playlog_toggle_favourite("BOUND_OLD2"));
 deferred=1;
 playlog_adopt_key_later("BOUND_OLD1","gba:BOUND1");playlog_adopt_key_later("BOUND_OLD2","gba:BOUND2");
 run_queue();playlog_poll_adoptions();
 assert((int)playlog_favourite("gba:BOUND1")+(int)playlog_favourite("gba:BOUND2")==1);
 run_queue();playlog_poll_adoptions();run_queue();
 assert(playlog_favourite("gba:BOUND1") && playlog_favourite("gba:BOUND2"));
 int queued=nq;playlog_adopt_key_later("BOUND_OLD1","gba:BOUND1");assert(nq==queued);
 deferred=0;
 puts("GBA legacy favorite adoption, preserved legacy entry, durable unstar: PASS");
 /* one ordered owner (round 10): a game's end queues its snapshot; a migration asked after it
  * cannot be overwritten by it, and its marker lands only after its log was written */
 const char *LOG="sdmc:/3ds/BreadBox/saves/playlog.ini";
 playlog_begin("OLDKEY");playlog_end();
 deferred=1;
 playlog_begin("gba:GAME");playlog_end();                  /* queued A: the end of a game */
 playlog_adopt_key("OLDKEY","gba:NEWKEY");                 /* queued B: a migration, after A */
 assert(!file_has(LOG,"[gba:GAME]") && nq==1);             /* nothing written here; the migration joined the waiting job */
 assert(access("sdmc:/3ds/BreadBox/saves/gba_NEWKEY.playlog-id",F_OK));
 run_queue();
 assert(file_has(LOG,"[gba:GAME]") && file_has(LOG,"[gba:NEWKEY]") && file_has(LOG,"OLDKEY]"));
 assert(!access("sdmc:/3ds/BreadBox/saves/gba_NEWKEY.playlog-id",F_OK));
 /* the log cannot be written: the migration's marker is not written either */
 deferred=0;playlog_begin("OLD2");playlog_end();deferred=1;
 assert(!mkdir("sdmc:/3ds/BreadBox/saves/playlog.ini.tmp",0700));   /* its temporary file cannot be made */
 playlog_adopt_key("OLD2","gba:N2");run_queue();
 assert(access("sdmc:/3ds/BreadBox/saves/gba_N2.playlog-id",F_OK));
 assert(!rmdir("sdmc:/3ds/BreadBox/saves/playlog.ini.tmp"));
 playlog_adopt_key("OLD2","gba:N2");assert(nq==0);         /* queued already: its marker stays the writer's */
 playlog_flush();run_queue();                              /* the failed write, asked again (exit, the next write) */
 assert(file_has(LOG,"[gba:N2]") && !access("sdmc:/3ds/BreadBox/saves/gba_N2.playlog-id",F_OK));
 /* round 11, N4: a second adoption of a game whose marker the writer owns (queued, its .tmp
  * open) must not touch that marker's files */
 deferred=0;assert(playlog_toggle_favourite("TMPOLD"));deferred=1;
 playlog_adopt_key("TMPOLD","gba:TMPNEW");assert(nq==1);
 FILE *tmp=fopen("sdmc:/3ds/BreadBox/saves/gba_TMPNEW.playlog-id.tmp","w");assert(tmp);fputs("1",tmp);fclose(tmp);   /* the writer, mid-write */
 playlog_adopt_key("TMPOLD","gba:TMPNEW");playlog_adopt_key("TMPOLD","gba:TMPNEW");
 assert(!access("sdmc:/3ds/BreadBox/saves/gba_TMPNEW.playlog-id.tmp",F_OK));   /* untouched by the menu thread */
 run_queue();assert(!access("sdmc:/3ds/BreadBox/saves/gba_TMPNEW.playlog-id",F_OK));
 /* N5 (the reviewer's reproduction, the other way round): 20 migrations before the writer runs:
  * every marker is written; an unstarred one stays unstarred on the next adoption */
 deferred=0;assert(playlog_toggle_favourite("OVERFLOW_OLD"));deferred=1;
 for(int j=0;j<20;j++){char key[40];snprintf(key,sizeof(key),"gba:OVERFLOW_%02d",j);playlog_adopt_key("OVERFLOW_OLD",key);}
 assert(nq==1);run_queue();
 for(int j=0;j<20;j++){char m[96];snprintf(m,sizeof(m),"sdmc:/3ds/BreadBox/saves/gba_OVERFLOW_%02d.playlog-id",j);assert(!access(m,F_OK));}
 assert(!playlog_toggle_favourite("gba:OVERFLOW_16"));run_queue();
 playlog_adopt_key("OVERFLOW_OLD","gba:OVERFLOW_16");run_queue();
 assert(!playlog_favourite("gba:OVERFLOW_16"));
 /* List publication cannot touch marker files: queries are deferred and
  * ready adoptions only change tables when polled on the UI thread. */
 deferred=0;assert(playlog_toggle_favourite("ASYNC_OLD"));deferred=1;
 playlog_adopt_key_later("ASYNC_OLD","gba:ASYNC_NEW");assert(nq==1);
 assert(!playlog_favourite("gba:ASYNC_NEW") && !playlog_poll_adoptions());
 run_queue();assert(!playlog_favourite("gba:ASYNC_NEW"));
 assert(playlog_poll_adoptions() && playlog_favourite("gba:ASYNC_NEW"));run_queue();
 assert(!playlog_toggle_favourite("gba:ASYNC_NEW"));run_queue();
 playlog_adopt_key_later("ASYNC_OLD","gba:ASYNC_NEW");assert(!playlog_poll_adoptions());
 assert(!playlog_favourite("gba:ASYNC_NEW"));
 /* Existing durable marker: background check never re-adds the star. */
 FILE *done=fopen("sdmc:/3ds/BreadBox/saves/gba_ASYNC_PRESENT.playlog-id","w");assert(done);fputs("1",done);fclose(done);
 playlog_adopt_key_later("ASYNC_OLD","gba:ASYNC_PRESENT");run_queue();
 assert(!playlog_poll_adoptions() && !playlog_favourite("gba:ASYNC_PRESENT"));
 /* A missing/full worker queue never executes marker I/O inline. */
 refuse_defer=true;playlog_adopt_key_later("ASYNC_OLD","gba:RETRY_NEW");
 assert(!nq && !playlog_poll_adoptions() && !playlog_favourite("gba:RETRY_NEW"));
 refuse_defer=false;assert(!playlog_poll_adoptions() && nq==1);
 run_queue();assert(playlog_poll_adoptions() && playlog_favourite("gba:RETRY_NEW"));run_queue();
 /* PSX3D migration preserves names/regions and merges by maxima, without
  * adding time twice or merging another system's namespace. */
 bb_save_settle();
 FILE *legacy=fopen("legacy.ini","w");assert(legacy);
 fputs("[Same (USA)]\nlast=4000000000\nseconds=42\n[Same (Europe)]\nlast=123\nseconds=7\n",legacy);fclose(legacy);
 assert(playlog_import_psx3d("legacy.ini"));
 assert(playlog_last("Same (USA)")==4000000000u && playlog_seconds("Same (USA)")==42);
 assert(playlog_last("Same (Europe)")==123 && !playlog_last("gba:Same (USA)"));
 assert(playlog_latest()==4000000000u);
 assert(playlog_import_psx3d("legacy.ini") && playlog_seconds("Same (USA)")==42);
 legacy=fopen("legacy.ini","w");assert(legacy);fputs("[Same (USA)]\nlast=1\nseconds=1\n",legacy);fclose(legacy);
 assert(playlog_import_psx3d("legacy.ini"));
 assert(playlog_last("Same (USA)")==4000000000u && playlog_seconds("Same (USA)")==42);
 assert(!mkdir("sdmc:/3ds/BreadBox/saves/playlog.ini.tmp",0700));
 assert(!playlog_import_psx3d("legacy.ini"));
 assert(!rmdir("sdmc:/3ds/BreadBox/saves/playlog.ini.tmp"));
 assert(playlog_import_psx3d("legacy.ini"));
 /* Played today (the room's Quit card): two games today add up; the day's count starts again on a new day and
  * is written beside last/seconds (older builds skip the keys). */
 bb_save_settle();
 tick_now=1;   /* 0 is the stopped clock */
 u32 now=(u32)time(NULL),today0=playlog_today(now);
 playlog_begin("gba:TODAY_A");tick_now+=20*60*SYSCLOCK_ARM11;playlog_end();
 playlog_begin("ps1:TODAY_B");tick_now+=60*60*SYSCLOCK_ARM11;playlog_pause(true);tick_now+=99*60*SYSCLOCK_ARM11;playlog_pause(false);playlog_end();
 run_queue();
 now=(u32)time(NULL);
 assert(playlog_today(now)==today0+80*60);
 assert(playlog_today(now+86400)==0 && playlog_today(now-86400)==0);
 assert(playlog_seconds("gba:TODAY_A")==20*60 && playlog_seconds("ps1:TODAY_B")==60*60);
 {char want[64];snprintf(want,sizeof(want),"day=%lu\ntoday=%d\n",(unsigned long)PLAYLOG_DAY(now),20*60);assert(file_has("sdmc:/3ds/BreadBox/saves/playlog.ini",want));}
 puts("Play log: played today over all games, paused time left out, a new day starts at 0, day/today written: PASS");
 puts("Play log markers: the writer's marker untouched by the menu, 20 pending markers all written, an unstar kept: PASS");
 puts("Play log: one ordered writer, a game's end and a migration in order, marker only after its log: PASS");
}
