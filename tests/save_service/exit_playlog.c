/* The exit's play-log retry, with the real choke point (bb_platform_saves,
 * taken from bb_platform.c by run.sh), the real later thread, play log and safe files: a play-log
 * write is on the later thread when the player leaves and fails there. The exit settles first,
 * then gives that write one more try and waits for it: two attempts, the file on the card.
 * Then (P3 2) the same write fails while the menu thread queues new migration markers and the
 * list cannot grow: no marker is lost, the next job writes all 32. */
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "bb_save.h"
#include "bb_final.h"
bool breadbox_import_busy(void){return false;}
#include "safefile.h"
#include <sys/types.h>
#include "app_log.h"
#include "bb_platform.h"
#include "ui.h"
#include "playlog.h"
bool bb_index_defer(void (*fn)(void *),void *arg) {bb_later(fn,arg);return true;}
bool bb_index_defer_low(void (*fn)(void *),void *arg){return bb_index_defer(fn,arg);}
bool bb_index_cancelled(void){return false;}
u64 svcGetSystemTick(void) { return 0; }
void log_printf(const char *fmt, ...) { (void)fmt; }
bool aptShouldClose(void) { return false; }
bool gspHasGpuRight(void) { return true; }
static bool dialog_test,discard_files;
static unsigned notices,retries;
void ui_confirm_ok(const char *title, const char *text, bool dflt)
{
 (void)dflt;assert(dialog_test && !notices);   /* one answer: no second OK tab (heavy test N1) */
 assert(!strcmp(title,"Save not made") && !strstr(text,"SD card"));notices++;
}
bool ui_confirm_choice(const char *title, const char *text, const char *yes, const char *no, bool dflt)
{
 (void)dflt;(void)no;assert(dialog_test && notices);
 if(!strcmp(title,"Not saved")){
  assert(strstr(text,"SD card") && strstr(yes,"Try again"));
  assert(++retries==1);return !discard_files;
 }
 assert(discard_files && !strcmp(title,"Lose the progress?"));return true;
}
#include "choke.inc"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int entered, released, calls, fail_favs, fail_alloc;
void *__real_realloc(void *p, size_t n);
void *__wrap_realloc(void *p, size_t n) { return fail_alloc ? NULL : __real_realloc(p, n); }
FILE *__real_safe_begin(const char *path);
FILE *__wrap_safe_begin(const char *path)
{
	if (strstr(path, "playlog.ini") && !fail_favs) {   // the first one waits for the exit, then fails
		pthread_mutex_lock(&lock);
		int first = ++calls == 1;
		if (first) {
			entered = 1;
			pthread_cond_broadcast(&cond);
			while (!released) pthread_cond_wait(&cond, &lock);
		}
		pthread_mutex_unlock(&lock);
		if (first) return NULL;
	}
	if (fail_favs && strstr(path, "favourites.ini")) {
		/* while this job writes, the menu migrates 16 more games, and memory runs out */
		fail_favs = 0;
		for (int i = 16; i < 32; i++) {
			char key[40];
			snprintf(key, sizeof(key), "gba:OOM_%02d", i);
			playlog_adopt_key("LEGACY", key);
		}
		fail_alloc = 1;
		return NULL;
	}
	return __real_safe_begin(path);
}
static void *leave(void *unused)
{
	(void)unused;
	bb_platform_saves(BB_SAVES_EXIT);
	return NULL;
}
static bool exists(const char *p) { return !access(p, F_OK); }
static char marker[64];
static const char *marker_of(int i)
{
	snprintf(marker, sizeof(marker), "sdmc:/3ds/BreadBox/saves/gba_OOM_%02d.playlog-id", i);
	return marker;
}

int main(int argc, char **argv)
{
	assert((argc == 2 || argc == 3) && !chdir(argv[1]));
 if(argc==3){
  dialog_test=true;
  for(unsigned scenario=0;scenario<3;scenario++){
   notices=retries=0;discard_files=scenario==2;
   bb_final_capture_failed();
   if(scenario){
    void *data=malloc(4);assert(data);memcpy(data,"new!",4);
    safe_format_t fmt={.size=4};int t=bb_final_take("retained.sav",&fmt,data,0,0);assert(t>=0);
    bb_final_defer_failed(t);
   }
   assert(bb_platform_saves(BB_SAVES_START)==BB_SAVES_GO);
   assert(notices==1 && retries==(scenario!=0) && !bb_save_final_failed());
   if(scenario==1){FILE *f=fopen("retained.sav","rb");char data[4];assert(f && fread(data,1,4,f)==4 && !memcmp(data,"new!",4));fclose(f);assert(!unlink("retained.sav"));}
   if(scenario==2)assert(access("retained.sav",F_OK));
  }
  puts("Capture notice: no SD/retry advice without bytes; mixed retained files require retry/discard PASS");return 0;
 }
	mkdir("sdmc:", 0700); mkdir("sdmc:/3ds", 0700); mkdir("sdmc:/3ds/BreadBox", 0700); mkdir("sdmc:/3ds/BreadBox/saves", 0700);
	playlog_begin("gba:EXIT");
	playlog_end();   // its write goes to the later thread and waits there
	pthread_mutex_lock(&lock);
	while (!entered) pthread_cond_wait(&cond, &lock);
	pthread_mutex_unlock(&lock);
	pthread_t t;
	assert(!pthread_create(&t, NULL, leave, NULL));   // the player leaves now
	usleep(20000);
	pthread_mutex_lock(&lock);
	released = 1;   // the write in flight fails
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&lock);
	pthread_join(t, NULL);
	assert(calls == 2 && exists("sdmc:/3ds/BreadBox/saves/playlog.ini"));
	puts("Exit: a play-log write failing on the later thread is tried once more after it settles, and waited for: PASS");

	/* P3 2: 16 migrations, their job fails while 16 more come and the list cannot grow */
	assert(playlog_toggle_favourite("LEGACY"));
	bb_save_settle();
	for (int i = 0; i < 16; i++) {
		char key[40];
		snprintf(key, sizeof(key), "gba:OOM_%02d", i);
		playlog_adopt_key("LEGACY", key);
	}
	fail_favs = 1;
	bb_save_settle();   // the failed job, then the one the new migrations asked for (still no memory)
	fail_alloc = 0;
	for (int i = 0; i < 32; i++) assert(exists(marker_of(i)));   // the first 16 too
	playlog_flush();
	bb_save_settle();
	assert(!playlog_toggle_favourite("gba:OOM_00"));   // unstarred now: stays so on the next adoption
	bb_save_settle();
	playlog_adopt_key("LEGACY", "gba:OOM_00");
	bb_save_settle();
	assert(!playlog_favourite("gba:OOM_00"));
	puts("Play log markers: a failed job's markers stay queued without new memory, all 32 written by the next job, unstar kept: PASS");
	return 0;
}
