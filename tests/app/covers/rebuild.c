// Run the actual shared cover loop against heap-owned library entries.
#define COVERS_THREADED
#include "sdk.h"
#include <stdatomic.h>
#include "../../../app/ui/covers.c"
#include "../../../app/ui/covers_wait.c"
#include "../../../app/ui/covers_match.c"
#include "../../../app/services/covers_fetch.c"
bool breadbox_import_pause(void){return true;}
void breadbox_import_resume(void){}
static bool cart_cover(int index,bb_cover_item *out) {assert(index==0 || index==1);*out=(bb_cover_item){.title=game.title,.key=game.cover_key,.code=game.group,.path=game.ndiscs?game.discs[0]:NULL};return true;}
static const bb_system_t cart={.id="gba",.cover_item=cart_cover};
const bb_system_t *const bb_systems[]={&cart};
const unsigned bb_system_count=1;
bool bb_index_get(const bb_system_t *s,int i,char *p,size_t cap,bb_entry *e) {(void)s;(void)i;(void)p;(void)cap;(void)e;return false;}
const bb_system_t *bb_system_by_id(const char *id) {return !strcmp(id,"gba")?&cart:NULL;}
#define TEST_BASE 65536
static atomic_bool hold_load;
static atomic_uint entered;
static atomic_uint loads;
static void svcSleepThread(long long ns) { usleep(ns/1000); }
static void log_printf(const char *fmt,...)
{
 if (!strcmp(fmt,"covers: start #%d\n")) {
  atomic_fetch_add(&loads,1);
  atomic_fetch_add(&entered,1);
  while(atomic_load(&hold_load)) usleep(100);
 }
}
static void *run_worker(void *arg) { covers_worker_main(arg);return NULL; }
static void *release_worker(void *arg)
{
 (void)arg;
 // Release only AFTER the rebuilding UI has paused claims. Its drain must
 // still be waiting: the old key and path remain owned until busy reaches 0.
 for (;;) {
  LightLock_Lock(&covers_list_lock);bool stopped=covers_paused;LightLock_Unlock(&covers_list_lock);
  if(stopped) break;
  usleep(100);
 }
 assert(game.cover_key && game.title && game.discs[0]);
 assert(covers_busy_workers==WORKERS);
 atomic_store(&hold_load,false);
 return NULL;
}
int main(void)
{
 covers_init();sw_nonet=true;
 covers_thumbs_loaded=covers_own_pngs_ok=true;
 game.cover_key=strdup("key0");game.title=strdup("Title 0");
 game.ndiscs=1;game.discs[0]=strdup("disc0.cue");
 cover_t c={.data=calloc(1,COVER_PACK_BYTES),.w=32,.h=32};assert(c.data);
 write_cv2("covers/key0.cv2",&c);write_cv2("covers/key1.cv2",&c);free(c.data);
 pthread_t threads[WORKERS];
 for(int i=0;i<WORKERS;i++) assert(!pthread_create(&threads[i],NULL,run_worker,NULL));
 for(int i=0;i<200;i++) {
  atomic_store(&entered,0);atomic_store(&hold_load,true);
  covers_get(TEST_BASE,NULL);covers_get(TEST_BASE+1,NULL);covers_resume();
  u64 deadline=osGetTime()+5000;
  while(atomic_load(&entered)<WORKERS) {assert(osGetTime()<deadline);usleep(100);}
  pthread_t release;assert(!pthread_create(&release,NULL,release_worker,NULL));
  covers_library_begin(); // must wait for the worker holding &game
  assert(!pthread_join(release,NULL));
  assert(covers_paused && !covers_busy_workers && library_changes==1);
  for(int j=0;j<SLOTS;j++) assert(covers_slots[j].game==-1 && !covers_slots[j].pixels);
  free(game.cover_key);free(game.title);free(game.discs[0]);
  memset(&game,0,sizeof(game));
  // Same index and same count, different identity. No partial-list requests
  // may survive and display the previous game's pixels after publication.
  assert(!covers_get(TEST_BASE,NULL));
  covers_resume();assert(covers_paused);
  covers_library_begin();assert(library_changes==2);
  covers_library_end();assert(covers_paused);
  game.cover_key=strdup(i%2?"key0":"key1");game.title=strdup("Replacement");
  game.ndiscs=1;game.discs[0]=strdup("replacement.cue");
  covers_library_end();assert(!covers_paused && !library_changes);
 }
 covers_stop_wait();
 covers_library_begin();covers_library_end();assert(covers_paused); // retain gameplay pause
 covers_quit=true;covers_paused=false;
 for(int i=0;i<WORKERS;i++) assert(!pthread_join(threads[i],NULL));
 assert(atomic_load(&loads)>=200*WORKERS);
 covers_exit();free(game.cover_key);free(game.title);free(game.discs[0]);
 puts("PASS 200 list replacements during claimed cover loads, shared real worker, nested publication and paused state");
}

bool bb_update_installing(void){return false;}
bool bb_update_committed(void){return false;}
bool bb_update_net_busy(void){return false;}
bool bb_update_net_stop(void){return true;}
