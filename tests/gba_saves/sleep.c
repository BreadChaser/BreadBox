/* Exercise pinned libctru dispatch with an event producer racing core frames.
 * This tests caller-thread ownership, not hardware APT scheduling. */
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
static _Atomic unsigned aptFlags;
#define FLAG_SHOULDSLEEP 1
#define FLAG_SLEEPING 2
#define APTHOOK_ONSLEEP 1
#define APTHOOK_ONWAKEUP 2
static int aptSleepEvent;
static pthread_t main_thread;
static bool core_running;
static unsigned saves;
static void aptCallHook(int hook) {
 assert(pthread_equal(main_thread,pthread_self()));assert(!core_running);
 if(hook==APTHOOK_ONSLEEP)++saves;
}
static int envGetAptAppId(void){return 0;}
static void APT_ReplySleepNotificationComplete(int n){(void)n;}
static void LightEvent_Wait(int *p){(void)p;}
static bool aptIsActive(void){return true;}
static void GSPGPU_SetLcdForceBlack(int n){(void)n;}
static void aptHandleJumpToHome(void){}
static bool aptShouldClose(void){return false;}
#include "apt-dispatch.inc"
static atomic_bool stop;
static void *events(void *p) {
 (void)p;while(!atomic_load(&stop))atomic_fetch_or(&aptFlags,FLAG_SHOULDSLEEP);return NULL;
}
int main(void) {
 main_thread=pthread_self();pthread_t event;assert(!pthread_create(&event,NULL,events,NULL));
 for(unsigned i=0;i<100000;i++) {
  core_running=true;for(volatile unsigned n=0;n<10;n++){}
  core_running=false;assert(aptMainLoop());
 }
 atomic_store(&stop,true);assert(!pthread_join(event,NULL));assert(saves);
 puts("Pinned libctru sleep hooks: main-thread single-writer during 100000 frames + concurrent APT notifications: PASS");
}
