#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#define U64_MAX UINT64_MAX
#define RESET_ONESHOT 0
typedef pthread_mutex_t LightLock;
#define LightLock_Init(p) pthread_mutex_init(p,NULL)
#define LightLock_Lock pthread_mutex_lock
#define LightLock_Unlock pthread_mutex_unlock
typedef struct { pthread_mutex_t lock; pthread_cond_t cond; int signaled; } LightEvent;
static inline void LightEvent_Init(LightEvent *e,int mode){(void)mode;pthread_mutex_init(&e->lock,NULL);pthread_cond_init(&e->cond,NULL);e->signaled=0;}
static inline void LightEvent_Clear(LightEvent *e){pthread_mutex_lock(&e->lock);e->signaled=0;pthread_mutex_unlock(&e->lock);}
static inline void LightEvent_Signal(LightEvent *e){pthread_mutex_lock(&e->lock);e->signaled=1;pthread_cond_signal(&e->cond);pthread_mutex_unlock(&e->lock);}
static inline void LightEvent_Wait(LightEvent *e){pthread_mutex_lock(&e->lock);while(!e->signaled)pthread_cond_wait(&e->cond,&e->lock);e->signaled=0;pthread_mutex_unlock(&e->lock);}
typedef struct host_thread {pthread_t id;void (*fn)(void *);void *arg;} *Thread;
static inline void *host_start(void *p){Thread t=p;t->fn(t->arg);return NULL;}
static inline Thread threadCreate(void (*fn)(void *),void *arg,size_t stack,int prio,int core,int detached){(void)stack;(void)prio;(void)core;(void)detached;Thread t=malloc(sizeof(*t));if(!t)return NULL;t->fn=fn;t->arg=arg;if(pthread_create(&t->id,NULL,host_start,t)){free(t);return NULL;}return t;}
extern int pthread_timedjoin_np(pthread_t,void **,const struct timespec *); /* glibc; hidden without _GNU_SOURCE */
/* Like libctru: 0 joined, nonzero timed out (the thread keeps running). */
static inline int threadJoin(Thread t,uint64_t n)
{
 if(n==U64_MAX){pthread_join(t->id,NULL);return 0;}
 struct timespec at;clock_gettime(CLOCK_REALTIME,&at);at.tv_sec+=(time_t)(n/1000000000ULL);at.tv_nsec+=(long)(n%1000000000ULL);
 if(at.tv_nsec>=1000000000L){at.tv_sec++;at.tv_nsec-=1000000000L;}
 return pthread_timedjoin_np(t->id,NULL,&at)?1:0;
}
static inline void threadFree(Thread t){free(t);}
