#include "ui_terms.h"
#include "ht_trace.h"
#include "bb_save.h"
#include "ui_format.h"
#include "bb_final.h"
#include "breadbox_import.h"
#include "bb_writers.h"
#include "app_policy.h"
#include "safefile.h"
#include "save_writer.h"
#include "history.h"
#include <stdlib.h>
#include <stddef.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <strings.h>
#include <errno.h>
#include <zlib.h>
#ifdef SAVE_WRITER_TEST
#include "writer_sdk.h"
#else
#include <3ds.h>
#endif
/* A session cannot be replaced until the worker has drained and joined. */
static bb_save_config config;
static const bb_save_ops *ops;
static char key[128],battery_path[512],state_base[488],rtc_path[544];
static size_t ram_bytes,core_bytes,rtc_bytes,snapshot_bytes;
static unsigned char *pending,*saved_rtc;
static bool durable,rtc_durable,poisoned;
/* live: a session this game began (bb_save_begin succeeded) and has not ended. Everything a
 * game calls acts only on its own live session: a game whose begin was refused (a failed
 * final write of the last game is kept) never touches the other game's kept data. */
static bool live;
static bool cont_fresh,cont_hold;   /* the Continue point: see bb_save_pause_take */
static bool captured;
static bool sync_ok=true;   /* no writer thread: the last synchronous write's result (end_job) */
static uint32_t durable_crc;
static save_writer *writer;
typedef struct {char magic[8];uint32_t version,bytes,identity,content_bytes,crc;} state_header;
static const char *state_magic(void){return ops->state_magic?ops->state_magic:"BBSTATE1";}
static const char *rtc_magic(void){return ops->rtc_magic?ops->rtc_magic:"BBRTC001";}
static void notice(const char *s){if(config.notice)config.notice(s);}
static bool battery(void **p)
{
 size_t n=0;*p=NULL;
 return ops && ops->battery && ops->battery(p,&n) && *p && n==ram_bytes;
}
static safe_format_t battery_format(void){return (safe_format_t){.size=ram_bytes};}
#define ONLY_CURRENT (1u<<SAFE_BACKUP | 1u<<SAFE_TEMP)   /* safe_load skip bits */
/* RTC records sit at odd offsets in the snapshot (after RAM and a flag byte).
 * Cores store whole words (gpSP: STM, which faults unaligned on the 3DS), so
 * ops only ever see this word-aligned copy. rtc_bytes<=244 (bb_save_begin). */
typedef struct {uint32_t w[61];} rtc_words;
static bool rtc_valid(const void *p)
{
 uint32_t crc;rtc_words v;memcpy(&crc,(const char *)p+8+rtc_bytes,4);memcpy(&v,(const char *)p+8,rtc_bytes);
 return !memcmp(p,rtc_magic(),8) && crc==crc32(0,p,8+rtc_bytes) &&
  (!ops->rtc_valid || ops->rtc_valid(&v,rtc_bytes));
}
static safe_format_t rtc_format(void)
{return (safe_format_t){.size=12+rtc_bytes,.head=12+rtc_bytes,.valid=rtc_valid};}
/* Only this callback touches durable metadata while a worker exists. No core
 * pointers or descriptor calls are used on that thread (RTC already validated). */
static bool persist_body(const void *snapshot)
{
 const unsigned char *p=snapshot;
 if(rtc_bytes && p[ram_bytes] && (!rtc_durable || memcmp(saved_rtc,p+ram_bytes+1,12+rtc_bytes))){
  safe_format_t fmt=rtc_format();
  if(!safe_write(rtc_path,&fmt,p+ram_bytes+1,true))return false;
  memcpy(saved_rtc,p+ram_bytes+1,12+rtc_bytes);rtc_durable=true;
 }
 uint32_t crc=crc32(0,p,ram_bytes);
 if(!ram_bytes || (durable && crc==durable_crc))return true;
 safe_format_t fmt=battery_format();safe_info_t info;
 /* The current file (never its backup in its place) goes to history before it becomes the backup;
  * one the SD card cannot read stops the write. A damaged one is parked as .bad by safe_write. */
 void *prior=durable?safe_load(battery_path,&fmt,ONLY_CURRENT,&info):NULL;
 if(durable && !prior && info.outcome!=SAFE_ABSENT && info.outcome!=SAFE_DAMAGED)return false;
 if(prior && !history_add_quiet(key,HIST_SAVE,".sav",prior,ram_bytes)){free(prior);return false;}
 free(prior);
 if(!safe_write(battery_path,&fmt,p,true))return false;
 durable_crc=crc;durable=true;return true;
}
static bool persist(const void *snapshot)
{if(!bb_writer_begin())return false;bool ok=persist_body(snapshot);bb_writer_end();return ok;}
void bb_save_invalidate(void){if(live)poisoned=true;}
bool bb_save_runnable(void){return live && !poisoned;}
bool bb_save_queue(void)
{
 if(!live || poisoned)return false;
 captured=false;
 void *ram;
 if(ram_bytes && !battery(&ram))return false;
 if(ram_bytes)memcpy(pending,ram,ram_bytes);
 pending[ram_bytes]=0;
 if(rtc_bytes){
  unsigned char *r=pending+ram_bytes+1;rtc_words v;
  memset(r,0,12+rtc_bytes);
  if(!ops->rtc_get(&v,rtc_bytes))return false;
  {
   memcpy(r,rtc_magic(),8);memcpy(r+8,&v,rtc_bytes);uint32_t crc=crc32(0,r,8+rtc_bytes);memcpy(r+8+rtc_bytes,&crc,4);
   if(!rtc_valid(r))return false;
   pending[ram_bytes]=1;
  }
 }
 captured=true;
 if(writer){save_writer_submit(writer,pending);return true;}
 return sync_ok=persist(pending);
}
/* The game is ending (bb_save_state_end with defer): from then on nothing waits for the card;
 * the battery writer is drained and stopped by the later thread (end_job), which also ends the
 * session. bb_save_settle (any game start, state read, exit) waits for it. */
static bool ending;
bool bb_save_flush(void)
{
 if(ending)return bb_save_queue();   /* on its way: the later thread drains the writer */
 bool ok=bb_save_queue();
 if(writer)ok=save_writer_flush(writer) && ok;
 if(!ok)notice("Not saved. Check the SD card.");
 return ok;
}
bool bb_save_failed(void){return writer && save_writer_failed(writer);}
static bool header_valid(const void *p)
{
 const state_header *h=p;
 return !memcmp(h->magic,state_magic(),8) && h->version==1 &&
  h->bytes==core_bytes+ram_bytes && h->identity==config.identity && h->content_bytes==config.content_bytes;
}
static bool payload_valid(const void *p,size_t n)
{
 const state_header *h=p;
 return n==sizeof(*h)+h->bytes && crc32(0,(const Bytef *)(h+1),h->bytes)==h->crc;
}
static safe_format_t state_format(void)
{return (safe_format_t){.size=sizeof(state_header)+core_bytes+ram_bytes,.head=sizeof(state_header),.valid=header_valid,.deep=payload_valid};}
static bool slot_path(unsigned slot,char *p,size_t n)
{return live && slot<=BB_SLOT_LAST && snprintf(p,n,"%s.state%u",state_base,slot)<(int)n;}
/* A state may be there: the file, its backup or a good unfinished copy, or the SD card could not
 * say (safe_stat). Continue discovery asks then: an unreadable Continue is never taken for none. */
bool bb_save_exists(unsigned slot)
{char p[512];safe_format_t fmt=state_format();return slot_path(slot,p,sizeof(p)) && safe_stat(p,&fmt,NULL);}
/* Known to be missing: only "no such file" proves it (safe_name). Any other stat error (an SD read
 * error) is not proof, so a postcard that may be there is never offered as free and written over. */
bool bb_save_absent(unsigned slot)
{char p[512];return slot_path(slot,p,sizeof(p)) && safe_name(p,NULL)==SAFE_NAME_ABSENT;}
static uint32_t slots_one_by_one(void)   /* unsure = used */
{
 uint32_t present=0;
 for(unsigned slot=0;slot<=BB_SLOT_LAST;slot++)if(!bb_save_absent(slot))present|=1u<<slot;
 return present;
}
/* Every slot file at once: on the 3DS SD card each stat of a name scans the whole folder (~83 ms in
 * a full GBA states folder on the Old), so the pause card's twelve held it back ~1 s. One pass over the
 * folder instead. A folder that cannot be opened, or a read error part way through, falls back to a stat
 * per slot: a partial list would call a used postcard free and X would write over it. Only the names
 * slot_path() writes count ("state1", not "state01"). */
uint32_t bb_save_slots_present(void)
{
 uint32_t present=0;
 if(!live)return 0;
 const char *slash=strrchr(state_base,'/');
 char dir[sizeof(state_base)];const char *base=slash?slash+1:state_base;size_t base_len=strlen(base);
 snprintf(dir,sizeof(dir),"%.*s",slash?(int)(slash-state_base):1,slash?state_base:".");
 DIR *d=opendir(dir);
 if(!d)return slots_one_by_one();
 struct dirent *e;
 for(errno=0;(e=readdir(d));errno=0) {
  const char *n=e->d_name,*digits=n+base_len+sizeof(".state")-1;
  if(strncasecmp(n,base,base_len) || strncasecmp(n+base_len,".state",sizeof(".state")-1) || !*digits ||
     strspn(digits,"0123456789")!=strlen(digits) || strlen(digits)>2 || (digits[0]=='0' && digits[1]))continue;
  unsigned slot=(unsigned)atoi(digits);
  if(slot<=BB_SLOT_LAST)present|=1u<<slot;
 }
 bool failed=errno!=0;
 closedir(d);
 return failed?slots_one_by_one():present;
}
bool bb_save_time_write(const char *path,uint32_t crc,uint32_t saved)
{
 char p[544];uint32_t stamp[2]={crc,saved};
 if(snprintf(p,sizeof(p),"%s.time",path)>=(int)sizeof(p))return false;
 safe_format_t fmt={.size=sizeof(stamp)};return safe_write(p,&fmt,stamp,true);
}
uint32_t bb_save_time(const char *path,const char *magic)
{
 state_header h;uint32_t stamp[2];char p[544];FILE *f=fopen(path,"rb");
 bool ok=f && fread(&h,1,sizeof(h),f)==sizeof(h);if(f)fclose(f);
 if(!ok || memcmp(h.magic,magic?magic:"BBSTATE1",8))return 0;
 if(snprintf(p,sizeof(p),"%s.time",path)>=(int)sizeof(p))return 0;
 f=fopen(p,"rb");ok=f && fread(stamp,1,sizeof(stamp),f)==sizeof(stamp);if(f)fclose(f);
 return ok && stamp[0]==h.crc?stamp[1]:0;
}
/* Postcard picture beside a state: <state>.thm, {magic, state crc, saved,
 * played, w, h} + RGB565 rows. Optional like .time: a picture that does not
 * match its state's CRC is never shown. */
typedef struct {char magic[8];uint32_t crc,saved,played;uint16_t w,h;} thumb_header;
static uint32_t played_now(void){return config.played?config.played():0;}
/* The caller's buffer (config.picture_buffer): no block of ours on the game's
 * heap in the middle of play; a fragmented heap starves PS1 after a GBA session. */
static thumb_header *thumb;
_Static_assert(sizeof(thumb_header)<=32,"BB_THUMB_BYTES header room");
static void thumb_write(const char *path,uint32_t crc,uint32_t saved)
{
 char p[544];unsigned w=0,h=0;
 if(!config.picture || snprintf(p,sizeof(p),"%s.thm",path)>=(int)sizeof(p))return;
 if(thumb && config.picture((uint16_t *)(thumb+1),&w,&h) && w && h && w<=BB_THUMB_MAX_W && h<=BB_THUMB_MAX_H){
  memcpy(thumb->magic,"BBTHUMB1",8);thumb->crc=crc;thumb->saved=saved;thumb->played=played_now();thumb->w=w;thumb->h=h;
  safe_format_t fmt={.size=sizeof(*thumb)+w*h*2};
  safe_write(p,&fmt,thumb,false);   /* optional: a failed picture leaves the card blank */
 }else safe_remove(p);   /* never leave an older picture beside a new state */
}
/* The last session's state files' base (<base>.stateN), kept after it ended: the room's corkboard
 * reads the new pictures from there after the game (room_walls.c). "" before any session. */
const char *bb_save_last_base(void){return state_base;}
bool bb_save_picture(uint16_t *px,unsigned *w,unsigned *h)
{
 *w=*h=0;
 return ops && config.picture && config.picture(px,w,h) && *w && *h && *w<=BB_THUMB_MAX_W && *h<=BB_THUMB_MAX_H;
}
bool bb_save_slot_info(unsigned slot,bb_slot_info *info,uint16_t *px)
{
 char path[512],p[544];state_header h;thumb_header t;
 memset(info,0,sizeof(*info));
 if(!slot_path(slot,path,sizeof(path)))return false;
 snprintf(p,sizeof(p),"%s.bak",path);
 safe_name_t at=safe_name(path,NULL);bool cur=at==SAFE_NAME_PRESENT;
 info->used=at!=SAFE_NAME_ABSENT || safe_name(p,NULL)!=SAFE_NAME_ABSENT;   /* unsure = used */
 if(!cur)return true;
 FILE *f=fopen(path,"rb");bool ok=f && fread(&h,1,sizeof(h),f)==sizeof(h);if(f)fclose(f);
 if(!ok || memcmp(h.magic,state_magic(),8))return true;
 info->saved=bb_save_time(path,state_magic());
 if(snprintf(p,sizeof(p),"%s.thm",path)>=(int)sizeof(p))return true;
 f=fopen(p,"rb");
 ok=f && fread(&t,1,sizeof(t),f)==sizeof(t) && !memcmp(t.magic,"BBTHUMB1",8) && t.crc==h.crc &&
  t.w && t.h && t.w<=BB_THUMB_MAX_W && t.h<=BB_THUMB_MAX_H;
 if(ok && px)ok=fread(px,2,(size_t)t.w*t.h,f)==(size_t)t.w*t.h;
 if(f)fclose(f);
 if(ok){info->w=t.w;info->h=t.h;info->played=t.played;if(!info->saved)info->saved=t.saved;}
 return true;
}
bool bb_save_delete(unsigned slot)
{
 char path[512],bak[520],thm[544],ext[16];
 if(bb_slot_card((int)slot)<0 || !slot_path(slot,path,sizeof(path)))return false;
 snprintf(bak,sizeof(bak),"%s.bak",path);snprintf(thm,sizeof(thm),"%s.thm",path);snprintf(ext,sizeof(ext),".state%u",slot);
 /* a postcard (or its backup) the SD card cannot answer for is never removed unseen by the backups */
 safe_name_t at=safe_name(path,NULL),at_bak=at==SAFE_NAME_ABSENT?safe_name(bak,NULL):SAFE_NAME_ABSENT;
 if(at==SAFE_NAME_UNREADABLE || at_bak==SAFE_NAME_UNREADABLE){notice("Could not delete: the SD card could not be read.");return false;}
 bool cur=at==SAFE_NAME_PRESENT,old=at_bak==SAFE_NAME_PRESENT;
 /* Like PS1: the deleted state is moved to the backups, never just erased. */
 if((cur || old) && !history_adopt(key,HIST_DELETE,ext,cur?path:bak,NULL,bb_save_time(cur?path:bak,state_magic()),NULL)){
  notice("Could not delete: the save could not be moved to the backups.");return false;
 }
 safe_remove(path);safe_remove(thm);
 snprintf(thm,sizeof(thm),"%s.time",path);safe_remove(thm);
 return true;
}
static bool pause_busy(void);
static bool continue_now(void);
static void save_log(const char *what)
{
#ifndef SAVE_WRITER_TEST
 extern void log_printf(const char *,...);
 log_printf("save: %s\n",what);
#else
 (void)what;
#endif
}
bool bb_save_state(unsigned slot,bool save)
{
 char path[512];if(poisoned || !slot_path(slot,path,sizeof(path)))return false;
 if(save && slot==BB_SLOT_CONTINUE)return continue_now();   /* the one writer of Continue points */
 if(!save){bb_save_pause_over();if(slot==BB_SLOT_CONTINUE && pause_busy()){bb_save_pause_put();bb_save_settle();}}   /* a load: the game changes */
 safe_format_t fmt=state_format();safe_info_t info={0};
 state_header *h=save?malloc(fmt.size):safe_load(path,&fmt,0,&info);
 bool ok=h!=NULL;
 if(save && ok){
  size_t len=0;void *ram=NULL;
  ok=ops->state_save(h+1,core_bytes,&len) && len==core_bytes && (!ram_bytes || battery(&ram));
  if(ok){
   if(ram_bytes)memcpy((unsigned char *)(h+1)+core_bytes,ram,ram_bytes);
   memcpy(h->magic,state_magic(),8);h->version=1;h->bytes=core_bytes+ram_bytes;
   h->identity=config.identity;h->content_bytes=config.content_bytes;h->crc=crc32(0,(const Bytef *)(h+1),h->bytes);
   ok=safe_write(path,&fmt,h,true);
   if(ok){uint32_t now=(uint32_t)time(NULL);bb_save_time_write(path,h->crc,now);if(slot<=BB_SLOT_CONTINUE || slot>=BB_SLOT_FIRST_NEW)thumb_write(path,h->crc,now);}
  }
 }else if(!save){
  /* Flush before touching the core. Read the authoritative committed battery;
   * restore it after success AND partial failure, reacquiring relocated RAM. */
  ok=ok && !info.weak && payload_valid(h,fmt.size) && bb_save_flush();
  safe_format_t bf=battery_format();safe_info_t bi;
  void *ram_save=ok && ram_bytes?safe_load(battery_path,&bf,ONLY_CURRENT,&bi):NULL;   /* just flushed: never an older copy */
  void *rtc=ok && rtc_bytes?malloc(rtc_bytes):NULL;
  if(ok && ((ram_bytes && !ram_save) || (rtc_bytes && !rtc)))ok=false;
  bool has_rtc=ok && rtc_bytes && ops->rtc_get(rtc,rtc_bytes);
  if(ok && rtc_bytes && !has_rtc)ok=false;
  if(ok){
   bb_state_result result=ops->state_load(h+1,core_bytes,false);
   ok=result==BB_STATE_OK;
   if(result==BB_STATE_PARTLY)poisoned=true;
   if(has_rtc && !ops->rtc_set(rtc,rtc_bytes)){ok=false;poisoned=true;}
   void *ram;
   if(ram_bytes){if(battery(&ram))memcpy(ram,ram_save,ram_bytes);else {ok=false;poisoned=true;}}
  }
  free(rtc);free(ram_save);
  if(ok && info.from!=SAFE_CURRENT)safe_repair(path,&fmt,h,info.from);
  /* a Continue the SD card could not read stays: nothing makes a new one over it this session */
  if(!ok && slot==BB_SLOT_CONTINUE && (info.outcome==SAFE_UNREADABLE || info.outcome==SAFE_NO_MEMORY))cont_hold=true;
 }
 free(h);
 if(!save && ok && config.restored)config.restored();
 /* one word for a saved moment: the postcard (ui_format.h); Continue, Undo and quick saves say nothing here */
 int card=bb_slot_card((int)slot);char said[96];
 if(poisoned)notice("Postcard load failed. Restart the game to continue.");
 else if(!save){
  if(!ok)notice(ui_term(UI_TERM_LOAD_FAILED));
  else if(card>=0){snprintf(said,sizeof(said),ui_term(info.from==SAFE_CURRENT?UI_TERM_LOADED:UI_TERM_LOADED_BACKUP),bb_slot_shown(card));notice(said);}
 }
 else if(!ok)notice(ui_term(UI_TERM_SAVE_FAILED));
 else if(card>=0){snprintf(said,sizeof(said),ui_term(UI_TERM_SAVED),bb_slot_shown(card));notice(said);}
 return ok;
}
/* Files written after the menu is back (bb_later): a game's Continue state at its end
 * (its atomic write reads the last one back: 1.3 s on the New 3DS, 2.3 s on the Old,
 * the game's picture frozen all that time) and the TV's last picture. One thread works
 * through them in order; bb_save_settle() waits for it, before any game starts, before
 * a state is read and at exit. */
#define LATER_MAX 32   /* a quit queues ~6 (state, battery, picture, play log, music, hands): never full in practice */
static struct {
 LightLock lock;bool init,running;Thread thread;int core,prio;
 unsigned n;struct {void (*fn)(void *);void *arg;} q[LATER_MAX];
} later={.core=1,.prio=0x30};
/* Where the later thread runs (main.c at start). Old 3DS: core 0 below the menu thread: on the
 * system core every bit of its work (the saves after a game, the music and hands reads) held up
 * the GPU's and the screen's events there and the menu missed whole frames (100-360 ms after a
 * quit on 2490); on core 0 it runs in the menu's idle time between frames, mostly waiting on the
 * SD card anyway. New 3DS: core 2. */
void bb_later_place(int core,int prio){later.core=core;later.prio=prio;}
static void later_main(void *unused)
{
 (void)unused;
 for(;;){
  LightLock_Lock(&later.lock);
  if(!later.n){later.running=false;LightLock_Unlock(&later.lock);return;}
  void (*fn)(void *)=later.q[0].fn;void *arg=later.q[0].arg;
  memmove(later.q,later.q+1,--later.n*sizeof(later.q[0]));
  LightLock_Unlock(&later.lock);
  HT("later job enter");fn(arg);HT("later job leave");
 }
}
bool bb_save_writers_busy(void)
{ return !bb_save_background_idle() || breadbox_import_busy(); }
bool bb_save_background_idle(void)
{
 if(later.init) {
  LightLock_Lock(&later.lock);bool idle=!later.running;LightLock_Unlock(&later.lock);
  if(!idle)return false;
 }
 return !live && !ending && !bb_save_final_failed();
}
bool bb_save_settle_timeout(uint64_t timeout)
{
 if(!later.init || !later.thread)return true;
 if(threadJoin(later.thread,timeout)!=0)return false;
 threadFree(later.thread);later.thread=NULL;return true;
}
void bb_save_settle(void)
{(void)bb_save_settle_timeout(U64_MAX);}
void bb_later(void (*fn)(void *),void *arg)
{
 if(!later.init){LightLock_Init(&later.lock);later.init=true;}
 for(;;){
  LightLock_Lock(&later.lock);
  if(later.n<LATER_MAX)break;
  LightLock_Unlock(&later.lock);bb_save_settle();   /* full: the menu waits (it never should) */
#ifndef SAVE_WRITER_TEST
  {extern void log_printf(const char *,...);log_printf("later: queue full, waited\n");}
#endif
 }
 later.q[later.n].fn=fn;later.q[later.n++].arg=arg;
 bool start=!later.running;later.running=true;
 LightLock_Unlock(&later.lock);
 if(!start)return;
 bb_save_settle();   /* the last batch's thread, done */
 /* below the menu thread; Old: the system core, New: core 2 (as the battery writer) */
 later.thread=threadCreate(later_main,NULL,32*1024,later.prio,later.core,false);
 if(!later.thread)later_main(NULL);   /* no thread: here and now, as before */
}
/* A file read in by the later thread into the caller's buffer (the hands, the music after
 * a game): the menu thread only picks it up. bb_later_wait takes a read back that has not
 * started and does it itself (a take-out right after a game). */
static bool read_whole(bb_later_file *r)
{
 HT("read whole enter");
 FILE *f=fopen(r->path,"rb");
 if(f && !r->buf){   /* its size and memory found here (16-byte aligned, freed by the caller) */
  long n=fseek(f,0,SEEK_END)?-1:ftell(f);
  r->buf=n>0 && !fseek(f,0,SEEK_SET)?memalign(16,(size_t)n):NULL;
  r->size=r->buf?(size_t)n:0;
 }
 /* 64 KB a request: the SD card is shared, one long request kept everyone else waiting */
 bool ok=f && r->buf;
 for(size_t at=0;ok && at<r->size;at+=64*1024){
  size_t n=r->size-at<64*1024?r->size-at:64*1024;
  ok=fread((char *)r->buf+at,1,n,f)==n;
 }
 if(f)fclose(f);
 if(ok && r->after)r->after(r);
 HT("read whole leave");
 return ok;
}
static bool take(bb_later_file *r,int to)
{
 int q=BB_LATER_QUEUED;
 return __atomic_compare_exchange_n(&r->state,&q,to,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
static void read_run(void *arg)
{
 HT("music/read enter");
 bb_later_file *r=arg;
 if(!take(r,BB_LATER_READING)){HT("music/read cancelled");return;}   /* taken back */
 __atomic_store_n(&r->state,read_whole(r)?BB_LATER_DONE:BB_LATER_FAILED,__ATOMIC_RELEASE);
 HT("music/read leave");
}
static void nap(void)
{
#ifdef SAVE_WRITER_TEST
 struct timespec t={0,1000000};nanosleep(&t,NULL);
#else
 svcSleepThread(1000000LL);
#endif
}
void bb_later_read(bb_later_file *r){__atomic_store_n(&r->state,BB_LATER_QUEUED,__ATOMIC_RELEASE);bb_later(read_run,r);}
int bb_later_state(bb_later_file *r){return __atomic_load_n(&r->state,__ATOMIC_ACQUIRE);}
bool bb_later_wait(bb_later_file *r)
{
 if(take(r,BB_LATER_READING))__atomic_store_n(&r->state,read_whole(r)?BB_LATER_DONE:BB_LATER_FAILED,__ATOMIC_RELEASE);
 while(bb_later_state(r)==BB_LATER_READING)nap();
 return bb_later_state(r)==BB_LATER_DONE;
}
/* Work for that thread (no files): done there, or here if it has not started when needed */
static bool take_task(bb_later_task *t)
{
 int q=BB_LATER_QUEUED;
 return __atomic_compare_exchange_n(&t->state,&q,BB_LATER_READING,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
static void task_run(void *arg)
{
 bb_later_task *t=arg;
 if(!take_task(t))return;
 t->fn(t->arg);
 __atomic_store_n(&t->state,BB_LATER_DONE,__ATOMIC_RELEASE);
}
void bb_later_do(bb_later_task *t){__atomic_store_n(&t->state,BB_LATER_QUEUED,__ATOMIC_RELEASE);bb_later(task_run,t);}
bool bb_later_done(bb_later_task *t){return __atomic_load_n(&t->state,__ATOMIC_ACQUIRE)==BB_LATER_DONE;}
void bb_later_finish(bb_later_task *t)
{
 if(take_task(t)){t->fn(t->arg);__atomic_store_n(&t->state,BB_LATER_DONE,__ATOMIC_RELEASE);}
 while(__atomic_load_n(&t->state,__ATOMIC_ACQUIRE)==BB_LATER_READING)nap();
}
void bb_later_cancel(bb_later_file *r)
{
 take(r,BB_LATER_FAILED);
 while(bb_later_state(r)==BB_LATER_READING)nap();
}
typedef struct {
 char path[512];state_header *h;size_t size;uint32_t crc,now;
 thumb_header *thumb;size_t thumb_size;int ticket;
} state_job;
bool bb_save_writing(const char *path,uint32_t *saved)
{return bb_final_status(path,saved)==BB_FINAL_QUEUED;}
static bool job_header_valid(const void *p)
{
 const state_header *h=p;
 return h->version==1 && h->bytes>0 && h->bytes<=33*1024*1024;
}
/* One per final-file ticket (bb_final.h): taking a point needs no memory for its metadata. */
static state_job jobs[BB_FINAL_SLOTS];
static void state_job_done(void *arg,bool ok)
{
 state_job *j=arg;
 if(ok){
  bb_save_time_write(j->path,j->crc,j->now);
  char p[544];
  if(snprintf(p,sizeof(p),"%s.thm",j->path)<(int)sizeof(p)){
   if(j->thumb){safe_format_t tf={.size=j->thumb_size};safe_write(p,&tf,j->thumb,false);}
   else safe_remove(p);
  }
 }
 free(j->thumb);j->thumb=NULL;
}
static void state_job_run(void *arg)
{state_job *j=arg;(void)bb_final_write(j->ticket);}
/* The state now, owned by the final files (bb_final_take), with its postcard (optional): its job,
 * or NULL when it could not be made (quiet: nothing said). Nothing is written here. */
static state_job *state_take(unsigned slot,bool quiet)
{
 safe_format_t fmt=state_format();char path[512];
 state_header *h=malloc(fmt.size);
 if(!h || !slot_path(slot,path,sizeof(path))){free(h);return NULL;}
 size_t len=0;void *ram=NULL;
 bool ok=ops->state_save(h+1,core_bytes,&len) && len==core_bytes && (!ram_bytes || battery(&ram));
 if(!ok){free(h);if(!quiet)notice("Could not make the Continue point.");return NULL;}
 if(ram_bytes)memcpy((unsigned char *)(h+1)+core_bytes,ram,ram_bytes);
 memcpy(h->magic,state_magic(),8);h->version=1;h->bytes=core_bytes+ram_bytes;
 h->identity=config.identity;h->content_bytes=config.content_bytes;h->crc=crc32(0,(const Bytef *)(h+1),h->bytes);
 uint32_t now=(uint32_t)time(NULL);
 /* Identity is fixed across points; the payload CRC changes and belongs to
  * deep validation, so a valid older Continue can still become the backup. */
 safe_format_t owned_fmt={.size=fmt.size,.head=offsetof(state_header,crc),.valid=job_header_valid,.deep=payload_valid};
 int ticket=bb_final_take(path,&owned_fmt,h,now,offsetof(state_header,crc));
 if(ticket<0){free(h);if(!quiet)notice("Could not make the Continue point.");return NULL;}
 state_job *j=&jobs[ticket];   /* free: its ticket's last owner was told (written or let go) */
 memset(j,0,sizeof(*j));
 strcpy(j->path,path);j->h=h;j->size=fmt.size;j->crc=h->crc;j->now=now;j->ticket=ticket;
 unsigned w=0,ph=0;
 if((slot<=BB_SLOT_CONTINUE || slot>=BB_SLOT_FIRST_NEW) && thumb && config.picture && config.picture((uint16_t *)(thumb+1),&w,&ph) && w && ph &&
    w<=BB_THUMB_MAX_W && ph<=BB_THUMB_MAX_H && (j->thumb=malloc(sizeof(*thumb)+w*ph*2))){
  memcpy(j->thumb->magic,"BBTHUMB1",8);j->thumb->crc=h->crc;j->thumb->saved=j->now;j->thumb->played=played_now();
  j->thumb->w=w;j->thumb->h=ph;memcpy(j->thumb+1,thumb+1,w*ph*2);j->thumb_size=sizeof(*thumb)+w*ph*2;
 }
 bb_final_after(j->ticket,state_job_done,j);
 return j;
}
/* The Continue point of a pause (bb_save_pause_*). Every Continue write goes through the final files'
 * one writer (bb_final_write), newest wins. A pause's is written by the later thread; one more may wait, and
 * a newer pause's replaces the waiting one (claimed back before it starts, then let go), so pausing again and
 * again holds two snapshots at most and never fills the final files. fresh: the Continue point taken or
 * written is the game as it is now (paused since): the end and HOME/sleep make no second one.
 * hold: the Continue question is unanswered, or the player went back from it: nothing makes one. */
static struct {bb_later_task task;state_job *job;} pause_cell[2];
static state_job *pause_taken;
static void pause_write(void *arg)
{
 state_job *j=arg;
#ifndef SAVE_WRITER_TEST
 u64 t0=osGetTime();
#endif
 save_log("Continue point from the pause: writing (the card is up)");
 bool ok=bb_final_write(j->ticket);   /* j is freed once written; a failed one is kept for the retry */
#ifndef SAVE_WRITER_TEST
 {extern void log_printf(const char *,...);log_printf("save: Continue point from the pause %s in %llu ms\n",ok?"written":"NOT written, kept to try again",osGetTime()-t0);}
#else
 (void)ok;
#endif
}
/* the waiting one (not started), claimed back and let go: a newer one replaces it */
static void pause_drop_waiting(void)
{
 for(unsigned i=0;i<2;i++){
  int q=BB_LATER_QUEUED;
  if(!__atomic_compare_exchange_n(&pause_cell[i].task.state,&q,BB_LATER_READING,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))continue;
  bb_final_drop(pause_cell[i].job->ticket);pause_cell[i].job=NULL;
  __atomic_store_n(&pause_cell[i].task.state,BB_LATER_DONE,__ATOMIC_RELEASE);
 }
}
static bool pause_busy(void)
{
 for(unsigned i=0;i<2;i++){int st=__atomic_load_n(&pause_cell[i].task.state,__ATOMIC_ACQUIRE);if(st==BB_LATER_QUEUED || st==BB_LATER_READING)return true;}
 return false;
}
bool bb_save_pause_take(void)
{
 if(!live || poisoned || cont_hold)return false;
 bb_save_pause_put();   /* never two taken: the older goes first */
 state_job *j=state_take(BB_SLOT_CONTINUE,true);
 cont_fresh=j!=NULL;
 if(!j){save_log("Continue point from the pause: not made (the game's end makes it)");return false;}
 pause_taken=j;return true;
}
void bb_save_pause_put(void)
{
 state_job *j=pause_taken;
 if(!j)return;
 pause_taken=NULL;
 pause_drop_waiting();
 /* the cell not writing now (one writes at most: one later thread; the waiting one was let go above) */
 int st=__atomic_load_n(&pause_cell[0].task.state,__ATOMIC_ACQUIRE);
 unsigned i=st==BB_LATER_QUEUED || st==BB_LATER_READING;
 bb_later_finish(&pause_cell[i].task);   /* free already: returns at once */
 pause_cell[i].job=j;pause_cell[i].task.fn=pause_write;pause_cell[i].task.arg=j;
 bb_later_do(&pause_cell[i].task);
}
void bb_save_pause_over(void){cont_fresh=false;}
void bb_save_continue_hold(bool hold){cont_hold=hold;}
/* Start over: the old Continue point and its backup go to the backups (history), never written over;
 * false: one could not be moved, or the SD card could not say whether it is there (only "not there" is
 * absent): it stays, and nothing makes a Continue point this session (hold). The caller does not start over. */
static bool aside(const char *file,hist_kind_t kind,const char *thm)
{
 safe_name_t at=safe_name(file,NULL);
 if(at!=SAFE_NAME_PRESENT)return at==SAFE_NAME_ABSENT;
 return history_adopt(key,kind,".state10",file,thm,bb_save_time(file,state_magic()),NULL);
}
bool bb_save_continue_aside(void)
{
 char path[512],bak[520],thm[544],stamp[544];
 if(!live || !slot_path(BB_SLOT_CONTINUE,path,sizeof(path))){cont_hold=true;return false;}
 snprintf(bak,sizeof(bak),"%s.bak",path);snprintf(thm,sizeof(thm),"%s.thm",path);snprintf(stamp,sizeof(stamp),"%s.time",path);
 bool ok=aside(bak,HIST_EXIT,NULL) && aside(path,HIST_START,thm);   /* older first */
 if(ok)safe_remove(stamp);
 cont_hold=!ok;
 save_log(ok?"Start over: the old Continue point is in the backups":"Start over NOT done: the old Continue point could not be moved (kept)");
 return ok;
}
/* A game's end: the state made now; defer: its files written after the menu is back
 * (else now, as bb_save_state). False if it could not be made. */
bool bb_save_state_end(unsigned slot,bool defer)
{
 if(!live || poisoned)return false;
 if(slot==BB_SLOT_CONTINUE && (cont_hold || cont_fresh)){   /* none to make, or the pause's is the game now */
  if(cont_fresh){bb_save_pause_put();if(defer)ending=true;}
  return true;
 }
 if(defer)ending=true;
 state_job *j=state_take(slot,false);
 if(!j){bb_final_capture_failed();return false;}   /* an older pause's waiting point still lands */
 if(slot==BB_SLOT_CONTINUE)pause_drop_waiting();   /* an older pause's, not started: this one replaces it */
 if(defer){bb_later(state_job_run,j);return true;}
 return bb_final_write(j->ticket);   /* behind a write under way, never under a newer one */
}
static bool continue_now(void)
{
 state_job *j=state_take(BB_SLOT_CONTINUE,false);
 if(!j){bb_final_capture_failed();return false;}   /* an older pause's waiting point still lands */
 pause_drop_waiting();   /* an older pause's, not started: this one replaces it */
 return bb_final_write(j->ticket);   /* behind a write under way; a failed one is kept for the retry */
}
bool bb_save_checkpoint(bool make_continue)
{
 bool ok=bb_save_flush();
 if(make_continue && cont_hold)return ok;   /* the Continue question is unanswered: the old one stays */
 /* the pause's Continue point is the game now: on the SD card before HOME or sleep, not made twice */
 if(make_continue && cont_fresh){bb_save_pause_put();bb_save_settle();return ok;}
 /* Continue never supersedes the latest durable battery on failure. */
 if(make_continue && ok)ok=continue_now();
 return ok;
}
/* A final battery write that failed keeps its snapshot (pending) and the session's paths
 * until the menu thread retries it or the player lets it go (bb_save_final_*): sticky, so a
 * game start or the exit can never pass over it unseen. */
static int final_failed;
static void session_free(void)
{
 free(pending);free(saved_rtc);pending=saved_rtc=NULL;thumb=NULL;ops=NULL;
 __atomic_store_n(&final_failed,0,__ATOMIC_RELEASE);
}
static void end_job(void *unused)
{
 (void)unused;
 bool ok=(writer?save_writer_flush(writer):sync_ok) && captured;   /* the writer's result, or the synchronous one */
 save_writer_stop(writer);writer=NULL;
 ending=false;
 if(ok)session_free();
 else __atomic_store_n(&final_failed,1,__ATOMIC_RELEASE);   /* kept for bb_save_final_retry */
 save_log(ok?"battery written (after the game)":"battery NOT written (after the game): kept to try again");
}
bool bb_save_final_failed(void){return __atomic_load_n(&final_failed,__ATOMIC_ACQUIRE)!=0 || bb_final_failed();}
bool bb_save_final_uncaptured(void)
{return bb_final_uncaptured() || (__atomic_load_n(&final_failed,__ATOMIC_ACQUIRE) && !captured);}
void bb_save_final_acknowledge(void)
{
 bb_save_settle();
 bb_final_acknowledge();
 if(__atomic_load_n(&final_failed,__ATOMIC_ACQUIRE) && !captured)session_free();
}
bool bb_save_final_retry(void)
{
 bb_save_settle();
 if(!bb_save_final_failed())return true;
 bool files_ok=bb_final_retry();
 bool ok=!__atomic_load_n(&final_failed,__ATOMIC_ACQUIRE) || (captured && persist(pending));   /* the menu thread now: no worker left */
 save_log(ok?"battery written on a new try":"battery still NOT written");
 if(ok && ops)session_free();
 return ok && files_ok;
}
void bb_save_final_discard(void)
{
 bb_save_settle();
 if(!bb_save_final_failed())return;
 bb_final_discard();
 save_log("battery NOT written: the player let it go");
 session_free();
}
/* The one choke point for the last game's saves: settle (when asked to), then, while its final
 * battery write is failed and kept, ask (the shell's dialog: BB_SAVE_RETRY / LATER / LOSE).
 * closing(): the system is closing the app (HOME > Close, power off): it gets one more try,
 * then the data goes; the caller must stop at once (false: no GPU work, no game start). */
bool bb_save_resolve(bool settle,bool (*closing)(void),int (*ask)(void *),void *ctx)
{
 if(settle)bb_save_settle();
 while(bb_save_final_failed()) {
  if(closing()) {
   if(!bb_save_final_retry())bb_save_final_discard();
   return false;
  }
  int answer=ask(ctx);
  if(closing())continue;   /* closed inside the dialog: the try above, then stop */
  if(answer==BB_SAVE_LATER)return true;
  if(answer==BB_SAVE_ACK)bb_save_final_acknowledge();
  else if(answer==BB_SAVE_LOSE)bb_save_final_discard();
  else bb_save_final_retry();
 }
 return !closing();
}

/* Suspend/sleep barrier (the shell's APT hook, bb_save_apt_init): every write queued after a
 * game (Continue state, final battery, play log, picture) on the card, or failed and kept,
 * before HOME or sleep is acknowledged; no save file open across the handoff. No GPU work. */
void bb_save_barrier(void){bb_save_settle();}
#ifndef SAVE_WRITER_TEST
static aptHookCookie apt_cookie;
static void apt_barrier(APT_HookType hook,void *unused)
{
 (void)unused;
 if(hook==APTHOOK_ONSUSPEND || hook==APTHOOK_ONSLEEP)bb_save_barrier();
}
void bb_save_apt_init(void){aptHook(&apt_cookie,apt_barrier,NULL);}
#endif
bool bb_save_end(bool make_continue)
{
 if(!live)return true;   /* not this game's session (its begin was refused), or ended */
 bb_save_pause_put();   /* a pause's Continue point not handed on yet (left before the card's first frame) */
 if(ending && !make_continue && !poisoned){   /* the snapshot now, the card and the session's end after the menu is back */
  bool ok=bb_save_queue();
  live=false;thumb=NULL;
  bb_later(end_job,NULL);
  return ok;
 }
 if(poisoned){
  /* The flush before restore completed. No snapshot from the invalid machine. */
  save_writer_stop(writer);writer=NULL;live=ending=false;session_free();return false;
 }
 bool ok=bb_save_queue();
 /* Shared libretro ends (NES): publish Continue only after the battery result.
  * A failed battery keeps its snapshot; it must not acquire a newer Continue. */
 if(make_continue){
  if(writer)ok=save_writer_flush(writer) && ok;
  if(ok)ok=bb_save_state_end(BB_SLOT_CONTINUE,false);
 }
 live=false;thumb=NULL;
 if(ending){bb_later(end_job,NULL);return ok;}
 end_job(NULL);   /* identical retention/outcome path, including synchronous fallback */
 return ok && !bb_save_final_failed();
}
bool bb_save_continue_read(const bb_system_t *system,const bb_content_id *id,uint32_t *saved,bool (*cancel)(void *),void *arg)
{
 if(!system || !system->save || !id || !id->key[0])return false;
 if(cancel && cancel(arg))return false;
 char path[512];state_header h;struct stat st;
 if(snprintf(path,sizeof(path),APP_SD_ROOT "/states/%s/%s.state10",system->id,id->key)>=(int)sizeof(path))return false;
 FILE *f=fopen(path,"rb");bool ok=f && !fstat(fileno(f),&st) && fread(&h,1,sizeof(h),f)==sizeof(h);
 const char *magic=system->save->state_magic?system->save->state_magic:"BBSTATE1";
 ok=ok && !memcmp(h.magic,magic,8) && h.version==1 && h.bytes>0 && h.bytes<=33*1024*1024 &&
  st.st_size==(long long)sizeof(h)+h.bytes && (!id->crc || h.identity==id->crc);
 uint32_t crc=crc32(0,NULL,0);unsigned char buf[4096];size_t n;
 if(ok){while(!(cancel && cancel(arg)) && (n=fread(buf,1,sizeof(buf),f)))crc=crc32(crc,buf,n);ok=!(cancel && cancel(arg)) && !ferror(f) && crc==h.crc;}
 if(f)fclose(f);
 if(ok && saved)*saved=bb_save_time(path,magic);
 return ok;
}
bool bb_save_continue(const bb_system_t *system,const bb_content_id *id,uint32_t *saved)
{
 bb_save_settle();
 return bb_save_continue_read(system,id,saved,NULL,NULL);
}
/* The newest backup of this game's battery (the ring keeps one before each overwrite). */
static bool battery_backup(hist_entry_t *e)
{
 enum {MAX=64};
 hist_entry_t *list=malloc(MAX*sizeof(*list));
 int n=list?history_list(key,false,list,MAX):0;bool found=false;
 for(int i=0;i<n && !found;i++)
  if(!strcmp(list[i].ext,".sav") && list[i].size==(long)ram_bytes){*e=list[i];found=true;}
 free(list);
 return found;
}
/* why the damaged-save question could not be carried out (empty: the player went back) */
static char damage_error[160];
/* A battery file the cartridge's chip could have written in another emulator (bb_save_ops.
 * battery_native: its size, smaller than the core's). Only the player decides it is one. */
static bool foreign_size(size_t *n)
{
 struct stat st;
 if(!ops->battery_native || stat(battery_path,&st) || st.st_size<=0 || (unsigned long long)st.st_size>=ram_bytes ||
  !ops->battery_native((size_t)st.st_size))return false;
 *n=(size_t)st.st_size;return true;
}
/* Backups holds these n bytes exactly (read back from the SD card). */
static bool in_backups(const unsigned char *p,size_t n)
{
 enum {MAX=64};
 hist_entry_t *list=malloc(MAX*sizeof(*list));unsigned char *b=malloc(n+1);
 int count=list && b?history_list(key,false,list,MAX):0;bool found=false;
 for(int i=0;i<count && !found;i++){
  if(strcmp(list[i].ext,".sav") || list[i].size!=(long)n)continue;
  FILE *f=fopen(list[i].path,"rb");
  found=f && fread(b,1,n+1,f)==n && !memcmp(b,p,n);
  if(f)fclose(f);
 }
 free(list);free(b);
 return found;
}
/* "Use it" for such a file: the original goes into Backups byte for byte first; only then is the
 * copy padded with battery_fill written, read back and put in place (safe_restore; the short file
 * is set aside as .bad). A cut before the Backups copy leaves the question; after it, the
 * original is in Backups whatever happens next. */
static bool use_foreign(void *ram,size_t n)
{
 unsigned char *p=malloc(ram_bytes);FILE *f=p?fopen(battery_path,"rb"):NULL;
 bool ok=f && fread(p,1,n,f)==n && fgetc(f)==EOF;
 if(f)fclose(f);
 ok=ok && (in_backups(p,n) || (history_add(key,HIST_SAVE,".sav",p,n) && in_backups(p,n)));
 if(ok){
  memset(p+n,ops->battery_fill,ram_bytes-n);safe_format_t fmt=battery_format();
  ok=safe_restore(battery_path,&fmt,p,NULL,NULL);
 }
 if(ok){memcpy(ram,p,ram_bytes);durable=true;}
 free(p);
 {char line[96];snprintf(line,sizeof(line),"%zu-byte battery %s",n,ok?"used, padded (original in Backups)":"NOT used");save_log(line);}
 if(!ok)snprintf(damage_error,sizeof(damage_error),"%s","The game save could not be used. Check the SD card.");
 return ok;
}
/* A damaged battery (or clock) file with no good copy: the shell asks before the game
 * runs. Put the newest backup back, or start empty; either way the damaged file is set aside
 * first. A battery file of a size another emulator writes for this cartridge adds "Use it"
 * (use_foreign); nothing is ever converted without that answer. Without a question
 * (bb_save_on_damage unset) nothing changes and the game does not start.
 * false: the player went back (damage_error empty) or it could not be done (damage_error set). */
static int (*damage_ask)(const bb_save_damage *);
void bb_save_on_damage(int (*ask)(const bb_save_damage *)){damage_ask=ask;}
static bool damaged_choice(const char *path,bool clock,void *ram)
{
 if(!damage_ask){snprintf(damage_error,sizeof(damage_error),"%s","The game save is damaged. Keep the file and check the SD card.");return false;}
 bb_save_damage d={.clock=clock};hist_entry_t e;size_t n=0;
 d.backup=!clock && battery_backup(&e);
 if(d.backup)d.when=e.when;
 d.foreign=!clock && foreign_size(&n);
 int answer=damage_ask(&d);
 if(answer==BB_DAMAGE_BACK || (answer==BB_DAMAGE_USE && !d.foreign))return false;
 if(answer==BB_DAMAGE_USE){
  if(!use_foreign(ram,n))return false;
  notice("Using that save. The original is in Backups.");
  return true;
 }
 if(answer==BB_DAMAGE_RESTORE && d.backup){
  /* written and read back before the damaged file goes (safe_restore): a cut leaves the question
   * or the restored save, never a blank start; "put back" only once it is the save */
  safe_format_t fmt=battery_format();void *copy=safe_load(e.path,&fmt,0,NULL);
  bool read=copy!=NULL,ok=read && safe_restore(battery_path,&fmt,copy,NULL,NULL);
  if(ok){memcpy(ram,copy,ram_bytes);durable=true;}
  free(copy);
  if(!ok){snprintf(damage_error,sizeof(damage_error),"%s",read?"The backup could not be put back. Check the SD card.":"The backup could not be read. Check the SD card.");return false;}
  save_log("damaged battery: the newest backup put back");
  notice("Game save put back from Backups.");
  return true;
 }
 if(!safe_set_aside(path)){snprintf(damage_error,sizeof(damage_error),"%s","The damaged save could not be set aside. Check the SD card.");return false;}
 save_log(clock?"damaged clock: starts over":"damaged battery: started empty");
 notice(clock?"The game clock starts over.":"Started with an empty game save.");
 return true;
}
/* No copy of a battery/clock file came back (safe_load's outcome): only "no such file" starts
 * empty; a damaged one asks the player; a file the SD card could not read (or no memory) stops
 * the game before anything can be written over it. */
typedef enum {START_EMPTY,START_FAIL,START_DECLINED} start_t;
static start_t start_without(const char *path,bool clock,void *ram,safe_outcome_t outcome)
{
 switch(outcome){
 case SAFE_ABSENT:return START_EMPTY;
 case SAFE_DAMAGED:return damaged_choice(path,clock,ram)?START_EMPTY:damage_error[0]?START_FAIL:START_DECLINED;
 default:
  {char line[96];snprintf(line,sizeof(line),"%s could not be read: not started",clock?"clock":"battery");save_log(line);}
  return START_FAIL;
 }
}
bool bb_save_begin(const bb_save_config *c,char *error,size_t cap)
{
 damage_error[0]=0;
 bb_save_settle();   /* the last session's end (end_job) first */
 if(bb_save_final_failed()){   /* the shell asks the player first (bb_platform_final_save) */
  if(error && cap)snprintf(error,cap,"The last game's save is not on the SD card yet. Check the card.");
  return false;
 }
 if(ops || !c || !c->system || !c->system->save)return false;
 config=*c;ops=c->system->save;durable=rtc_durable=poisoned=false;sync_ok=true;cont_fresh=cont_hold=false;
 if(!ops->state_size || !ops->state_save || !ops->state_load)goto fail;
 if(!c->key || !c->battery_path || !c->state_base ||
  (c->history_key?snprintf(key,sizeof(key),"%s",c->history_key):
   snprintf(key,sizeof(key),"%s-%s",c->system->id,c->key))>=(int)sizeof(key) ||
  snprintf(battery_path,sizeof(battery_path),"%s",c->battery_path)>=(int)sizeof(battery_path) ||
  snprintf(state_base,sizeof(state_base),"%s",c->state_base)>=(int)sizeof(state_base))goto fail;
 core_bytes=ops->state_size();ram_bytes=0;void *ram=NULL;
 if(ops->battery)ops->battery(&ram,&ram_bytes);
 rtc_bytes=ops->rtc_size?ops->rtc_size():0;
 if(!core_bytes || core_bytes>32*1024*1024 || ram_bytes>1024*1024 ||
  (ram_bytes && !ram) || rtc_bytes>244 || (rtc_bytes && (!ops->rtc_get || !ops->rtc_set)))goto fail;
 snapshot_bytes=ram_bytes+1+(rtc_bytes?12+rtc_bytes:0);
 pending=calloc(1,snapshot_bytes);saved_rtc=rtc_bytes?malloc(12+rtc_bytes):NULL;
 thumb=c->picture?c->picture_buffer:NULL;   /* NULL: no pictures */
 if(!pending || (rtc_bytes && !saved_rtc))goto fail;
 if(rtc_bytes){
  /* Battery path ends .sav; keep legacy .rtc beside it. */
  size_t n=strlen(battery_path);if(n<4 || strcmp(battery_path+n-4,".sav"))goto fail;
  snprintf(rtc_path,sizeof(rtc_path),"%.*s.rtc",(int)n-4,battery_path);
  safe_format_t fmt=rtc_format();safe_info_t info;void *p=safe_load(rtc_path,&fmt,0,&info);
  if(p){
   rtc_words v;memcpy(&v,(char *)p+8,rtc_bytes);bool ok=ops->rtc_set(&v,rtc_bytes);
   if(ok){memcpy(saved_rtc,p,fmt.size);rtc_durable=true;if(info.from!=SAFE_CURRENT)safe_repair(rtc_path,&fmt,p,info.from);}
   free(p);if(!ok)goto fail;
  }else switch(start_without(rtc_path,true,NULL,info.outcome)){case START_FAIL:goto fail;case START_DECLINED:goto declined;default:break;}
 }
 if(ram_bytes){
  safe_format_t fmt=battery_format();safe_info_t info;void *p=safe_load(battery_path,&fmt,0,&info);
  if(p){memcpy(ram,p,ram_bytes);durable=true;if(info.from!=SAFE_CURRENT){safe_repair(battery_path,&fmt,p,info.from);notice("Battery save restored from backup.");}free(p);}
  else switch(start_without(battery_path,false,ram,info.outcome)){case START_FAIL:goto fail;case START_DECLINED:goto declined;default:break;}
  durable_crc=crc32(0,ram,ram_bytes);
 }
 writer=(ram_bytes || rtc_bytes)?save_writer_start(snapshot_bytes,persist,c->writer_core):NULL;
 live=true;
 return true; /* Thread/allocation failure has the proven synchronous fallback. */
fail:
 free(pending);free(saved_rtc);pending=saved_rtc=NULL;thumb=NULL;ops=NULL;
 if(error && cap)snprintf(error,cap,"%s",damage_error[0]?damage_error:"Could not read the save files. Keep them and check the SD card.");
 return false;
declined:   /* the player went back at the damaged-save question: nothing changed, no error */
 free(pending);free(saved_rtc);pending=saved_rtc=NULL;thumb=NULL;ops=NULL;
 if(error && cap)error[0]=0;
 return false;
}
