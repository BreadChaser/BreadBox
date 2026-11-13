/* Execute the production queue with real pthread races and a blocked disk. */
#include "save_writer.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <sched.h>
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond=PTHREAD_COND_INITIALIZER;
static bool entered,release_disk,fail;
static unsigned seen[1024],count;
static unsigned char expected[128*1024];
static bool disk(const void *data) {
 pthread_mutex_lock(&lock);
 entered=true;pthread_cond_broadcast(&cond);
 while(!release_disk)pthread_cond_wait(&cond,&lock);
 const unsigned char *p=data;for(unsigned i=1;i<sizeof(expected);i++)assert(p[i]==p[0]);
 assert(count<1024);seen[count++]=p[0];bool ok=!fail;
 pthread_mutex_unlock(&lock);return ok;
}
static atomic_bool drained;
static save_writer *writer;
static void *drain(void *p){(void)p;assert(save_writer_flush(writer));atomic_store(&drained,true);return NULL;}
int main(void) {
 writer=save_writer_start(sizeof(expected),disk,1);assert(writer);
 memset(expected,1,sizeof(expected));save_writer_submit(writer,expected);
 pthread_mutex_lock(&lock);while(!entered)pthread_cond_wait(&cond,&lock);pthread_mutex_unlock(&lock);
 /* Active buffer stays immutable while pending is replaced 199 times. */
 for(unsigned i=2;i<=200;i++){memset(expected,i,sizeof(expected));save_writer_submit(writer,expected);}
 memset(expected,0,sizeof(expected)); // producer may immediately mutate core RAM
 pthread_t waiter;assert(!pthread_create(&waiter,NULL,drain,NULL));
 for(unsigned i=0;i<10000;i++)sched_yield();assert(!atomic_load(&drained));
 pthread_mutex_lock(&lock);release_disk=true;pthread_cond_broadcast(&cond);pthread_mutex_unlock(&lock);
 pthread_join(waiter,NULL);assert(count==2 && seen[0]==1 && seen[1]==200);
 memset(expected,200,sizeof(expected));save_writer_submit(writer,expected);assert(save_writer_flush(writer));assert(count==2);
 fail=true;memset(expected,201,sizeof(expected));save_writer_submit(writer,expected);assert(!save_writer_flush(writer));assert(save_writer_failed(writer));
 fail=false;save_writer_submit(writer,expected);assert(save_writer_flush(writer));assert(!save_writer_failed(writer));assert(count==4);
 /* Repeated wake/flush and same-byte failure retry must never lose a wake. */
 for(unsigned i=0;i<500;i++){memset(expected,i&255,sizeof(expected));save_writer_submit(writer,expected);assert(save_writer_flush(writer));}
 save_writer_stop(writer);
 puts("GBA production writer: immutable snapshots, latest-wins, blocked flush, retry, 500 wake/drain races PASS");
}
