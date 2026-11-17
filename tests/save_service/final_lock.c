/* The final files' metadata lock on one core with strict priorities, exercised with
 * a blocking lock probe. Old 3DS: the later thread writes on core 0 below the menu thread, so once
 * the menu thread wants the lock the later thread runs only while the menu thread sleeps. Model: the later thread
 * is stopped while it holds the lock (inside the real bb_final_write, letting go of an older failed point) until
 * the main thread sleeps (nanosleep, wrapped); the main thread then takes a new point, drops it, or asks a
 * status. A lock that spins and never sleeps never lets the owner finish: the alarm fails the test. */
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "bb_final.h"
bool bb_writer_begin(void){return true;}
void bb_writer_end(void){}
bool safe_write(const char *p,const safe_format_t *f,const void *d,bool b){(void)p;(void)f;(void)d;(void)b;return true;}
static pthread_t main_thread;
static int armed,owner_stopped,main_slept,let_go;
int __real_nanosleep(const struct timespec *,struct timespec *);
int __wrap_nanosleep(const struct timespec *a,struct timespec *b)
{
 if(pthread_equal(pthread_self(),main_thread))__atomic_store_n(&main_slept,1,__ATOMIC_RELEASE);
 return __real_nanosleep(a,b);
}
/* the older failed point let go under the lock by the later thread: it stops there until the main thread sleeps */
static void older_done(void *ctx,bool ok)
{
 (void)ctx;assert(!ok);let_go++;
 if(!__atomic_load_n(&armed,__ATOMIC_ACQUIRE) || pthread_equal(pthread_self(),main_thread))return;
 __atomic_store_n(&owner_stopped,1,__ATOMIC_RELEASE);
 while(!__atomic_load_n(&main_slept,__ATOMIC_ACQUIRE))usleep(1000);
}
static void *later_thread(void *ticket){return (void *)(intptr_t)bb_final_write((int)(intptr_t)ticket);}
static void stuck(int sig)
{
 (void)sig;static const char m[]="final_lock: the main thread spins on the metadata lock; the lower-priority owner never runs: FAIL\n";
 if(write(2,m,sizeof m-1)){}
 _Exit(1);
}
int main(void)
{
 main_thread=pthread_self();signal(SIGALRM,stuck);
 static const char *const contender[]={"take","drop","status"};
 safe_format_t f={.size=4,.head=4};
 for(unsigned c=0;c<3;c++){
  armed=owner_stopped=main_slept=let_go=0;
  int older=bb_final_take("continue",&f,calloc(1,4),1,0);assert(older>=0);
  bb_final_after(older,older_done,NULL);bb_final_defer_failed(older);   /* failed, kept for Retry */
  int newer=bb_final_take("continue",&f,calloc(1,4),2,0);assert(newer>=0);
  int queued=c==1?bb_final_take("continue",&f,calloc(1,4),3,0):-1;   /* claimed back below */
  armed=1;pthread_t t;assert(!pthread_create(&t,NULL,later_thread,(void *)(intptr_t)newer));
  while(!__atomic_load_n(&owner_stopped,__ATOMIC_ACQUIRE))usleep(1000);
  alarm(10);
  if(c==0){int next=bb_final_take("continue",&f,calloc(1,4),4,0);assert(next>=0);bb_final_drop(next);}
  else if(c==1)bb_final_drop(queued);
  else {uint32_t saved=0;bb_final_status("continue",&saved);}
  void *ok;assert(!pthread_join(t,&ok) && ok);
  alarm(0);
  assert(let_go==1 && !bb_final_failed());
  printf("final_lock: %s while the later thread holds the lock: it sleeps, the owner finishes: PASS\n",contender[c]);
 }
 return 0;
}
