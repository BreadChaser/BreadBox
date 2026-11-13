// Exercise the actual cover lifecycle and curl driver with small SDK substitutes.
#define BB_VERSION "test"
#define PSX3D_DIR "."
#define PATH_LEN 512
#define CUR_THREAD_HANDLE 0
#define RESET_ONESHOT 0
#define R_SUCCEEDED(x) ((x)>=0)
#define GPU_RGB565 0
#define GPU_RGBA8 1
#define GPU_LINEAR 0
#define GPU_CLAMP_TO_EDGE 0
#define SYSCLOCK_ARM11 268111856ULL
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <curl/curl.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef unsigned long long u64;
typedef int32_t s32;
typedef void *Thread;
#ifdef COVERS_THREADED
#include <pthread.h>
typedef pthread_mutex_t LightLock;
#else
typedef int LightLock;
#endif
typedef int LightEvent;
typedef int aptHookCookie;
typedef enum { APTHOOK_ONSUSPEND, APTHOOK_ONSLEEP, APTHOOK_ONRESTORE, APTHOOK_ONWAKEUP, APTHOOK_ONEXIT } APT_HookType;
typedef struct { void *data; } C3D_Tex;
typedef struct { unsigned short width, height; float left, top, right, bottom; } Tex3DS_SubTexture;
#ifdef COVERS_THREADED
typedef struct { char *cover_key, *title, *group; int ndiscs; char *discs[8]; } game_t;
#else
typedef struct { char *cover_key, *title, *group; int ndiscs; char *discs[8]; } game_t;
#endif
static char game_key[256]="cover",game_title[64]="Cover Title";
static game_t game = { .cover_key = game_key, .title = game_title };
static bool sw_nocovers,sw_nonet;
bool dev_remote = true;
// The daily preset check (presetupdate.c) has its own host test.
bool presets_update_due(void) { return false; }
void presets_update_work(int (*fetch)(const char *, const char *, size_t, long *), bool (*stop)(void)) { (void)fetch; (void)stop; }
static bool blocked, g_new3ds;
bool dev_covers_blocked(void) { return blocked; }
static void (*video_pre_draw)(void);
static int cancel_after, sleeps;
u64 osGetTime(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (u64)t.tv_sec*1000+t.tv_nsec/1000000; }
static void svcSleepThread(long long ns);
#ifndef COVERS_THREADED
static void LightLock_Init(LightLock *l) {*l=0;}
static void LightLock_Lock(LightLock *l) {assert(!*l);*l=1;}
static void LightLock_Unlock(LightLock *l) {assert(*l);*l=0;}
static int LightLock_TryLock(LightLock *l) {if(*l)return 1;*l=1;return 0;}
#else
static void LightLock_Init(LightLock *l) {assert(!pthread_mutex_init(l,NULL));}
static void LightLock_Lock(LightLock *l) {assert(!pthread_mutex_lock(l));}
static void LightLock_Unlock(LightLock *l) {assert(!pthread_mutex_unlock(l));}
static int LightLock_TryLock(LightLock *l) {return pthread_mutex_trylock(l);}
#endif
static void LightEvent_Init(LightEvent *e,int mode) {(void)e;(void)mode;}
static void LightEvent_Signal(LightEvent *e) {(void)e;}
static void LightEvent_WaitTimeout(LightEvent *e,long long ns) {(void)e;(void)ns;usleep(1000);}
static int socInit(void *p,int n) {(void)p;(void)n;return 0;}
static void socExit(void) {}
static void aptHook(aptHookCookie *c,void (*f)(APT_HookType,void*),void *p) {(void)c;(void)f;(void)p;}
static void aptUnhook(aptHookCookie *c) {(void)c;}
/* HOME: what covers_home_frame allows, a rejected press (one-shot), jumps to the HOME Menu */
static bool home_allowed=true,home_rejected;static int home_jumps;
static void aptSetHomeAllowed(bool on) {home_allowed=on;}
static bool aptCheckHomePressRejected(void) {bool r=home_rejected;home_rejected=false;return r;}
static void aptJumpToHomeMenu(void) {assert(home_allowed);home_jumps++;}
static void svcGetThreadPriority(s32 *p,int h) {(void)h;*p=0x30;}
static Thread threadCreate(void (*f)(void*),void *p,int n,int prio,int core,bool detached) {(void)f;(void)p;(void)n;(void)detached;assert(prio==(core==2?0x34:0x30));return NULL;}
static int threadJoin(Thread t,long long ns) {(void)t;(void)ns;return 0;}
static void threadFree(Thread t) {(void)t;}
#ifdef COVERS_THREADED
// Two queue entries may point at the same owned metadata for this fixture.
static const game_t *library_get(int i) {assert(i==0 || i==1);return &game;}
static int library_count(void) {return 2;}
#else
static const game_t *library_get(int i) {assert(i==0);return &game;}
static int library_count(void) {return 1;}
#endif
#ifdef COVERS_THREADED
static void log_printf(const char *fmt,...);
#else
static void log_printf(const char *fmt,...) {(void)fmt;}
#endif
static void C3D_TexDelete(C3D_Tex *t) {free(t->data);}
static bool C3D_TexInit(C3D_Tex *t,int w,int h,int fmt) {(void)fmt;t->data=malloc(w*h*2);return !!t->data;}
static void C3D_TexSetFilter(C3D_Tex *t,int a,int b) {(void)t;(void)a;(void)b;}
static void C3D_TexSetWrap(C3D_Tex *t,int a,int b) {(void)t;(void)a;(void)b;}
static unsigned long long svcGetSystemTick(void) {return 0;}
static void GSPGPU_FlushDataCache(void *p,int n) {(void)p;(void)n;}
CURLMcode __real_curl_multi_poll(CURLM*,struct curl_waitfd*,unsigned,int,int*);
CURLMcode __wrap_curl_multi_poll(CURLM *m,struct curl_waitfd *f,unsigned n,int ms,int *num)
{assert(ms==0);return __real_curl_multi_poll(m,f,n,ms,num);}
void room_pace_diagnostic(void) {}
