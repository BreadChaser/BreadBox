#include "bb_final.h"
#include "bb_writers.h"
#include <stdlib.h>
#include <string.h>
#ifdef __3DS__
#include <3ds.h>
static void nap(void){svcSleepThread(1000000LL);}
#else
#include <time.h>
static void nap(void){struct timespec t={0,1000000};nanosleep(&t,NULL);}
#endif
/* Three PS1 cards plus Continue; spare slots for future adapters. Each file taken gets the next
 * generation: per path the newest wins. A write of an older one is skipped once a newer one is on
 * the SD card, and an older failed one is let go then (its bytes are kept until that moment). */
static struct { char path[512];safe_format_t fmt;void *data;size_t identity;uint32_t saved,gen;int state;bool stale;void (*done)(void *,bool);void *ctx; } files[BB_FINAL_SLOTS];
static uint32_t generation;
static int capture_failed;
static unsigned metadata_lock,write_lock;
/* Busy: sleep, never spin. On Old the later thread holding it runs on core 0 below the menu thread,
 * so a waiter that spins there never lets it finish (the menu would hang). */
static void lock(void){while(__atomic_exchange_n(&metadata_lock,1,__ATOMIC_ACQUIRE))nap();}
static void unlock(void){__atomic_store_n(&metadata_lock,0,__ATOMIC_RELEASE);}
static int state_of(unsigned i){return __atomic_load_n(&files[i].state,__ATOMIC_ACQUIRE);}
/* One write at a time, whoever calls (the later thread, the menu's retry, a game's end): the
 * validator context below and the shared .tmp paths are never used by two writes at once. */
static void write_begin(void){while(__atomic_exchange_n(&write_lock,1,__ATOMIC_ACQUIRE))nap();}
static void write_end(void){__atomic_store_n(&write_lock,0,__ATOMIC_RELEASE);}
static const void *checking;
static const safe_format_t *checking_format;
static size_t checking_identity;
static bool same_header(const void *head)
{return !memcmp(head,checking,checking_identity) && (!checking_format->valid || checking_format->valid(head));}
/* Under the lock: a kept file let go (its owner told, its bytes freed), the slot free again. */
static void retire(unsigned i)
{
 if(files[i].done)files[i].done(files[i].ctx,false);
 free(files[i].data);files[i].data=NULL;files[i].done=NULL;files[i].stale=false;
 __atomic_store_n(&files[i].state,BB_FINAL_EMPTY,__ATOMIC_RELEASE);
}
int bb_final_take(const char *path,const safe_format_t *fmt,void *data,uint32_t saved,size_t identity)
{
 if(!path || strlen(path)>=sizeof(files[0].path) || !fmt || !data || !fmt->size || identity>fmt->head)return -1;
 lock();
 int pick=-1;   /* a free slot, else the oldest written one (only its outcome is forgotten) */
 for(unsigned i=0;i<BB_FINAL_SLOTS;i++){
  int state=state_of(i);
  if(state==BB_FINAL_EMPTY){pick=(int)i;break;}
  if(state==BB_FINAL_WRITTEN && (pick<0 || files[i].gen<files[pick].gen))pick=(int)i;
 }
 if(pick>=0){
  files[pick].done=NULL;files[pick].ctx=NULL;files[pick].stale=false;files[pick].gen=++generation;
  strcpy(files[pick].path,path);files[pick].fmt=*fmt;files[pick].data=data;files[pick].saved=saved;files[pick].identity=identity;
  __atomic_store_n(&files[pick].state,BB_FINAL_QUEUED,__ATOMIC_RELEASE);
 }
 unlock();return pick;
}
void bb_final_after(int i,void (*done)(void *,bool),void *ctx)
{if(i>=0 && i<BB_FINAL_SLOTS){files[i].done=done;files[i].ctx=ctx;}}
void bb_final_drop(int i)
{
 if(i<0 || i>=BB_FINAL_SLOTS)return;
 lock();if(state_of(i)==BB_FINAL_QUEUED)retire(i);unlock();
}
void bb_final_defer_failed(int i)
{if(i>=0 && i<BB_FINAL_SLOTS)__atomic_store_n(&files[i].state,BB_FINAL_FAILED,__ATOMIC_RELEASE);}
/* Under the lock, i just written: every older file of its path is behind it. A failed one goes,
 * a queued one is skipped when its turn comes, a written one is forgotten (status reads i). */
static void newest_written(unsigned i)
{
 for(unsigned k=0;k<BB_FINAL_SLOTS;k++){
  int state=state_of(k);
  if(k==i || state==BB_FINAL_EMPTY || files[k].gen>files[i].gen || strcmp(files[k].path,files[i].path))continue;
  if(state==BB_FINAL_QUEUED)files[k].stale=true;
  else if(state==BB_FINAL_FAILED)retire(k);
  else __atomic_store_n(&files[k].state,BB_FINAL_EMPTY,__ATOMIC_RELEASE);
 }
 __atomic_store_n(&files[i].state,BB_FINAL_WRITTEN,__ATOMIC_RELEASE);
}
bool bb_final_write(int i)
{
 if(i<0 || i>=BB_FINAL_SLOTS)return false;
 write_begin();
 lock();
 int state=state_of(i);
 bool skip=(state==BB_FINAL_QUEUED || state==BB_FINAL_FAILED) && files[i].stale;
 if(skip)retire(i);   /* a newer one is on the SD card: this one is never written over it */
 unlock();
 if(skip || (state!=BB_FINAL_QUEUED && state!=BB_FINAL_FAILED)){write_end();return skip || state==BB_FINAL_WRITTEN;}
 bool owned=bb_writer_begin();
 safe_format_t fmt=files[i].fmt;checking=files[i].data;checking_format=&files[i].fmt;checking_identity=files[i].identity;
 if(checking_identity)fmt.valid=same_header;
 bool ok=owned && safe_write(files[i].path,&fmt,files[i].data,true);
 checking=NULL;checking_format=NULL;
 if(owned)bb_writer_end();
 if(ok){free(files[i].data);files[i].data=NULL;if(files[i].done)files[i].done(files[i].ctx,true);files[i].done=NULL;}
 lock();
 if(ok)newest_written(i);
 else __atomic_store_n(&files[i].state,BB_FINAL_FAILED,__ATOMIC_RELEASE);
 unlock();
 write_end();return ok;
}
bool bb_final_failed(void)
{
 if(__atomic_load_n(&capture_failed,__ATOMIC_ACQUIRE))return true;
 for(unsigned i=0;i<BB_FINAL_SLOTS;i++)if(state_of(i)==BB_FINAL_FAILED)return true;
 return false;
}
/* The newest failed file first: once it is written, the older ones of its path are let go unwritten. */
bool bb_final_retry(void)
{
 bool tried[BB_FINAL_SLOTS]={0};
 for(;;){
  int next=-1;
  lock();
  for(unsigned i=0;i<BB_FINAL_SLOTS;i++)
   if(!tried[i] && state_of(i)==BB_FINAL_FAILED && (next<0 || files[i].gen>files[next].gen))next=(int)i;
  unlock();
  if(next<0)break;
  tried[next]=true;bb_final_write(next);
 }
 return !bb_final_failed();
}
void bb_final_discard(void)
{
 lock();
 for(unsigned i=0;i<BB_FINAL_SLOTS;i++)if(state_of(i)==BB_FINAL_FAILED)retire(i);
 unlock();
 __atomic_store_n(&capture_failed,0,__ATOMIC_RELEASE);
}
bool bb_final_uncaptured(void){return __atomic_load_n(&capture_failed,__ATOMIC_ACQUIRE)!=0;}
void bb_final_acknowledge(void){__atomic_store_n(&capture_failed,0,__ATOMIC_RELEASE);}
void bb_final_capture_failed(void){__atomic_store_n(&capture_failed,1,__ATOMIC_RELEASE);}
bb_final_outcome bb_final_status(const char *path,uint32_t *saved)
{
 bb_final_outcome result=BB_FINAL_EMPTY;uint32_t when=0,gen=0;if(!saved)saved=&when;lock();
 for(unsigned i=0;i<BB_FINAL_SLOTS;i++){
  int state=state_of(i);
  if(state!=BB_FINAL_EMPTY && !files[i].stale && !strcmp(path,files[i].path) && (result==BB_FINAL_EMPTY || files[i].gen>gen)){gen=files[i].gen;*saved=files[i].saved;result=state;}   /* the newest */
 }
 unlock();return result;
}
