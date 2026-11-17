/* The one writer of Continue points under allocation failure, overlapping writes, late failure
 * and failed Start over, on the real save service, final
 * files, later thread, safe files and history, under ASan/UBSan:
 * 1: allocation failure at a pause with a queued and with an in-flight older point: nothing is written before
 *    the card's first frame, two writes never overlap, the newest point is the one on the SD card;
 * 2: an older write that fails after a newer point was taken never comes back over the newer one (Retry or
 *    the ordinary end); when both fail, Retry writes the newest;
 * 2c: the newest waiting pause point is kept when the end's or HOME/sleep's new point cannot be made;
 * 3: Start over fails closed: an SD error at the stat (point or backup), or a failed rename, keeps the hold and
 *    each original at its place or in the backups; a failed read only loses the date. */
#define main legacy_check_main
#include "check.c"
#undef main
#include <errno.h>
#include <pthread.h>
#include "bb_final.h"
static pthread_t main_thread;
static bool on_main(void){return pthread_equal(pthread_self(),main_thread);}
/* 1: every allocation on the core (main) thread fails; 2: calloc only (what the old pause metadata used) */
static int fail_alloc;
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);
void *__wrap_malloc(size_t n){return fail_alloc==1 && on_main()?NULL:__real_malloc(n);}
void *__wrap_calloc(size_t n,size_t z){return fail_alloc && on_main()?NULL:__real_calloc(n,z);}
/* the next write of the Continue point off the main thread waits at its start until released; fails if fail_held */
static int hold_next,held,released,fail_held;
/* SD errors for Start over: stat (EIO), the read of a point's date, a rename */
static const char *stat_fails,*read_fails,*rename_fails;
FILE *__real_fopen(const char *,const char *);
FILE *__wrap_fopen(const char *p,const char *m)
{
 if(!on_main() && !strcmp(p,"game.state10.tmp") && m[0]=='w' && __atomic_exchange_n(&hold_next,0,__ATOMIC_ACQ_REL)){
  __atomic_store_n(&held,1,__ATOMIC_RELEASE);
  while(!__atomic_load_n(&released,__ATOMIC_ACQUIRE))usleep(1000);
  if(fail_held){errno=ENOSPC;return NULL;}
 }
 if(read_fails && on_main() && !strcmp(p,read_fails) && m[0]=='r'){errno=EIO;return NULL;}
 return __real_fopen(p,m);
}
int __real_stat(const char *,struct stat *);
int __wrap_stat(const char *p,struct stat *s){if(stat_fails && !strcmp(p,stat_fails)){errno=EIO;return -1;}return __real_stat(p,s);}
int __real_rename(const char *,const char *);
int __wrap_rename(const char *a,const char *b){if(rename_fails && !strcmp(a,rename_fails)){errno=EIO;return -1;}return __real_rename(a,b);}
static int file_position(const char *path)
{
 FILE *f=__real_fopen(path,"rb");if(!f)return -1;
 int p=fseek(f,28,SEEK_SET)?-1:fgetc(f);fclose(f);return p;
}
static int point(void){return file_position("game.state10");}
static int backup(void){return file_position("game.state10.bak");}
static char top[512];
static void fresh_dir(const char *name)
{
 fail_alloc=0;hold_next=held=released=fail_held=0;stat_fails=read_fails=rename_fails=NULL;
 assert(!chdir(top) && !mkdir(name,0700) && !chdir(name));history_set_root("history");
 begin();position=98;assert(bb_save_checkpoint(true));assert(bb_save_end(false));
 begin();position=99;assert(bb_save_checkpoint(true));assert(bb_save_end(false));bb_save_settle();
 assert(point()==99 && backup()==98);
}
static void wait_held(void){while(!__atomic_load_n(&held,__ATOMIC_ACQUIRE))usleep(1000);}
static void release(void){__atomic_store_n(&released,1,__ATOMIC_RELEASE);}
/* CW_ONLY=<name part>: only the cases whose name holds it (negative controls run one case on older code) */
static bool want(const char *name){const char *only=getenv("CW_ONLY");return !only || strstr(name,only);}
static int gate;
static void gate_job(void *unused){(void)unused;__atomic_store_n(&held,1,__ATOMIC_RELEASE);while(!__atomic_load_n(&gate,__ATOMIC_ACQUIRE))usleep(1000);}
static void quit(unsigned at)   /* the game's end (gba.c): its own point unless the pause's is the game now */
{
 position=at;assert(bb_save_state_end(BB_SLOT_CONTINUE,true));assert(bb_save_end(false));bb_save_settle();
 assert(!bb_save_final_failed());
}
/* a game's end with the point written now on another thread (bb_save_end(true): libretro systems) */
static void *end_now(void *unused){(void)unused;return (void *)(intptr_t)bb_save_state_end(BB_SLOT_CONTINUE,false);}
static int history_positions(int *pos,int n)
{
 hist_entry_t e[8];int k=history_list("test-test-unique",false,e,8);
 for(int i=0;i<k && i<n;i++)pos[i]=file_position(e[i].path);
 return k;
}
static bool in_history(int value)
{
 int pos[8],k=history_positions(pos,8);
 for(int i=0;i<k;i++)if(pos[i]==value)return true;
 return false;
}
int main(int argc,char **argv)
{
 assert(argc==2 && !chdir(argv[1]) && getcwd(top,sizeof(top)));main_thread=pthread_self();

 /* 1a: no memory at the pause, an older pause's point waiting behind other work: none is made (nothing
  * written before the card), the older one lands, the end makes the newest */
 if(want("oom-queued")){fresh_dir("oom-queued");
 begin();gate=0;held=0;bb_later(gate_job,NULL);wait_held();
 position=42;assert(bb_save_pause_take());bb_save_pause_put();bb_save_pause_over();
 position=60;fail_alloc=1;assert(!bb_save_pause_take());fail_alloc=0;
 assert(point()==99);
 __atomic_store_n(&gate,1,__ATOMIC_RELEASE);bb_save_settle();assert(point()==42);
 quit(70);assert(point()==70 && backup()==42);

 /* 1b: only the small allocations fail (the old pause metadata): the point is made with no SD access, the
  * waiting older one is replaced, the newest is on the card */
 }
 if(want("calloc-queued")){fresh_dir("calloc-queued");
 begin();gate=0;held=0;bb_later(gate_job,NULL);wait_held();
 position=42;assert(bb_save_pause_take());bb_save_pause_put();bb_save_pause_over();
 position=60;fail_alloc=2;assert(bb_save_pause_take());fail_alloc=0;
 assert(point()==99);
 bb_save_pause_put();__atomic_store_n(&gate,1,__ATOMIC_RELEASE);bb_save_settle();
 assert(point()==60 && backup()==99);
 position=7;assert(bb_save_state_end(BB_SLOT_CONTINUE,true));assert(bb_save_end(false));bb_save_settle();   /* quit while paused */
 assert(point()==60 && !bb_save_final_failed());

 /* 1c: no memory at the pause while the older point is being written */
 }
 if(want("oom-writing")){fresh_dir("oom-writing");
 begin();hold_next=1;position=42;assert(bb_save_pause_take());bb_save_pause_put();bb_save_pause_over();wait_held();
 position=60;fail_alloc=1;assert(!bb_save_pause_take());fail_alloc=0;
 assert(point()==99);
 release();bb_save_settle();assert(point()==42);
 quit(70);assert(point()==70 && backup()==42);

 /* 1d: small allocations fail while the older point is being written: the newer one waits its turn */
 }
 if(want("calloc-writing")){fresh_dir("calloc-writing");
 begin();hold_next=1;position=42;assert(bb_save_pause_take());bb_save_pause_put();bb_save_pause_over();wait_held();
 position=60;fail_alloc=2;assert(bb_save_pause_take());fail_alloc=0;bb_save_pause_put();
 assert(point()==99);
 release();bb_save_settle();assert(point()==60 && backup()==42);
 position=7;assert(bb_save_state_end(BB_SLOT_CONTINUE,true));assert(bb_save_end(false));bb_save_settle();
 assert(point()==60 && !bb_save_final_failed());

 /* 1e: a point written now (a libretro game's end) while an older pause's is being written: it waits for the
  * one writer (two writers sharing the validator could crash), then lands as the newest */
 }
 if(want("overlap")){fresh_dir("overlap");
 begin();hold_next=1;position=42;assert(bb_save_pause_take());bb_save_pause_put();bb_save_pause_over();wait_held();
 position=60;pthread_t t;assert(!pthread_create(&t,NULL,end_now,NULL));
 usleep(200000);assert(point()==99);   /* not written beside the held one */
 release();void *ok;assert(!pthread_join(t,&ok) && ok);bb_save_settle();
 assert(point()==60 && backup()==42 && !bb_save_final_failed());
 assert(bb_save_end(false));bb_save_settle();

 /* 2a: the older write fails after the newer point was taken: the newer one lands, the older is let go;
  * Retry and the ordinary end never put it back */
 }
 if(want("late-failure-retry")){fresh_dir("late-failure-retry");
 begin();hold_next=1;fail_held=1;position=42;assert(bb_save_pause_take());bb_save_pause_put();bb_save_pause_over();wait_held();
 position=60;assert(bb_save_pause_take());bb_save_pause_put();
 release();bb_save_settle();
 assert(point()==60 && !bb_save_final_failed());
 assert(bb_save_state_end(BB_SLOT_CONTINUE,true));assert(bb_save_end(false));bb_save_settle();   /* quit while paused */
 assert(bb_save_final_retry() && point()==60);
 }
 if(want("late-failure-end")){fresh_dir("late-failure-end");
 begin();hold_next=1;fail_held=1;position=42;assert(bb_save_pause_take());bb_save_pause_put();bb_save_pause_over();wait_held();
 position=60;assert(bb_save_pause_take());bb_save_pause_put();
 release();bb_save_settle();bb_save_pause_over();
 quit(70);assert(point()==70 && backup()==60);
 begin();assert(bb_save_end(false));bb_save_settle();assert(point()==70);   /* the next game starts: nothing older comes back */

 /* 2b: both fail: Retry writes the newest, the older one goes unwritten */
 }
 if(want("both-fail")){fresh_dir("both-fail");
 begin();hold_next=1;fail_held=1;position=42;assert(bb_save_pause_take());bb_save_pause_put();bb_save_pause_over();wait_held();
 position=60;assert(bb_save_pause_take());bb_save_pause_put();
 assert(!mkdir("game.state10.tmp",0700));FILE *x=__real_fopen("game.state10.tmp/x","w");assert(x);fclose(x);   /* the newer write fails too */
 release();bb_save_settle();
 assert(point()==99 && bb_save_final_failed());
 assert(bb_save_state_end(BB_SLOT_CONTINUE,true));assert(bb_save_end(false));bb_save_settle();
 assert(!unlink("game.state10.tmp/x") && !rmdir("game.state10.tmp"));
 assert(bb_save_final_retry() && point()==60 && backup()==99 && !bb_save_final_failed());

 }
 /* 2c: a pause's point waits (behind other work, or behind an older pause's write),
  * play goes on, then the end's or HOME/sleep's new point cannot be made (no memory): nothing is cancelled, the
  * waiting point still lands, and the failure is reported (the end and the checkpoint alike) */
 static const char *const lost_end[]={"newest-kept-end-gate","newest-kept-end-older","newest-kept-checkpoint-gate","newest-kept-checkpoint-older"};
 for(unsigned i=0;i<4;i++){
  if(!want(lost_end[i]))continue;
  fresh_dir(lost_end[i]);begin();
  bool older=i&1,end=i<2;
  if(older){hold_next=1;position=20;assert(bb_save_pause_take());bb_save_pause_put();bb_save_pause_over();wait_held();}
  else {gate=0;held=0;bb_later(gate_job,NULL);wait_held();}
  position=42;assert(bb_save_pause_take());bb_save_pause_put();bb_save_pause_over();
  assert(bb_save_writing("game.state10",NULL));
  position=60;fail_alloc=1;
  bool ok=end?bb_save_state_end(BB_SLOT_CONTINUE,true):bb_save_checkpoint(true);
  fail_alloc=0;assert(!ok);
  if(older)release();else __atomic_store_n(&gate,1,__ATOMIC_RELEASE);
  bb_save_settle();
  if(point()!=42 || backup()!=(older?20:99)){fprintf(stderr,"%s: point %d backup %d (the waiting 42 was lost)\n",lost_end[i],point(),backup());return 1;}
  assert(bb_save_final_uncaptured());   /* the player is told a point could not be made */
  assert(bb_save_end(false) || !end);bb_save_settle();   /* the checkpoint's failure is still unacknowledged at the end */
  bb_save_final_acknowledge();assert(!bb_save_final_failed() && point()==42);
 }
 /* 3: Start over with SD errors */
 static const struct {const char *name,*stat,*read,*rename;bool ok;} over[]={
  {"over-stat-backup","game.state10.bak",NULL,NULL,false},{"over-stat-point","game.state10",NULL,NULL,false},
  {"over-rename-backup",NULL,NULL,"game.state10.bak",false},{"over-rename-point",NULL,NULL,"game.state10",false},
  {"over-read",NULL,"game.state10",NULL,true},{"over-read-backup",NULL,"game.state10.bak",NULL,true}};
 for(unsigned i=0;i<sizeof(over)/sizeof(*over);i++){
  if(!want(over[i].name))continue;
  fresh_dir(over[i].name);
  begin();bb_save_continue_hold(true);
  stat_fails=over[i].stat;read_fails=over[i].read;rename_fails=over[i].rename;
  bool ok=bb_save_continue_aside();
  stat_fails=read_fails=rename_fails=NULL;
  if(ok!=over[i].ok){fprintf(stderr,"%s: Start over said %d\n",over[i].name,ok);return 1;}
  /* each original where it was or in the backups */
  assert((point()==99 || in_history(99)) && (backup()==98 || in_history(98)));
  if(ok){assert(point()<0 && backup()<0 && in_history(99) && in_history(98));quit(8);assert(point()==8 && backup()<0);continue;}
  /* not started over: the hold stays, nothing replaces what is left (checkpoints, a pause, the end) */
  int before[2]={point(),backup()};
  position=42;assert(bb_save_checkpoint(true));assert(!bb_save_pause_take());
  position=43;assert(bb_save_state_end(BB_SLOT_CONTINUE,true));assert(bb_save_end(false));bb_save_settle();
  assert(point()==before[0] && backup()==before[1]);
  assert((point()==99 || in_history(99)) && (backup()==98 || in_history(98)));
 }
 puts("Continue writer: allocation failure at a pause (queued / in-flight older point), a written-now end beside a held write, an older write failing after a newer point (Retry / end / both failing), a waiting pause point kept when the next cannot be made (end / checkpoint), Start over with stat/rename/read errors: the newest point wins, nothing overlaps, originals kept: PASS");
 return 0;
}
