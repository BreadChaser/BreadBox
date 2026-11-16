/* Run the production pause/drain, menu release and session entry with a clock port. */
#define COVERS_FAKE_TIME
#define COVERS_LOG_CAPTURE
#include "sdk.h"
#include "bb_registry.h"
static u64 now, settle_at, event_at;
static int held, event, releases, core_entries, game_frames;
static char wait_log[4096];
static void log_printf(const char *fmt,...)
{va_list ap;va_start(ap,fmt);vsnprintf(wait_log+strlen(wait_log),sizeof(wait_log)-strlen(wait_log),fmt,ap);va_end(ap);}
static bool import_live, update_live;
u64 osGetTime(void) {return now;}
#include "../../../app/ui/covers.c"
#include "../../../app/ui/covers_wait.c"
#include "../../../app/ui/covers_match.c"
#include "../../../app/services/covers_fetch.c"
bool breadbox_import_pause(void){return !import_live;}
void breadbox_import_resume(void){}
bool bb_update_installing(void){return false;}
bool bb_update_committed(void){return false;}
bool bb_update_net_busy(void){return update_live;}
bool bb_update_net_stop(void){return !update_live;}
static void svcSleepThread(long long ns)
{
 now+=ns/1000000;
 if(settle_at && now>=settle_at){import_live=update_live=false;covers_busy_workers=0;}
 if(event_at && now>=event_at){
  event_at=0;
  switch(event){
  case 1:keys_down=KEY_B;break;
  case 2:closing=true;break;
  case 3:gpu_right=false;break;
  case 4:home_rejected=true;break;
  case 5:on_apt(APTHOOK_ONSLEEP,NULL);break;
  }
 }
}
static bool cart_cover(int index,bb_cover_item *out){(void)index;(void)out;return false;}
static void shell_release(void){assert(!held || settle_at);releases++;}
static bb_run_result run_core(const bb_launch *l,char *error,size_t cap)
{(void)l;(void)error;(void)cap;game_frames++;return BB_RUN_QUIT;}
static const char *const extensions[]={"gba",NULL};
static const bb_shell_ops shell={.release_menu=shell_release};
const bb_system_t bb_system_gba={.abi=BB_SYSTEM_ABI,.id="gba",.extensions=extensions,.models=BB_MODEL_ALL,.run=run_core,.shell=&shell,.cover_item=cart_cover};
const bb_system_t *const bb_systems[]={&bb_system_gba};
const unsigned bb_system_count=1;
const bb_system_t *bb_system_by_id(const char *id){(void)id;return &bb_system_gba;}
bool bb_index_get(const bb_system_t *s,int i,char *p,size_t cap,bb_entry *e){(void)s;(void)i;(void)p;(void)cap;(void)e;return false;}
int bb_platform_count(const bb_system_t *s){(void)s;return 0;}
bool bb_platform_budget(const bb_system_t *s){(void)s;return true;}
bool bb_platform_claim(const bb_system_t *s){(void)s;core_entries++;return true;}
void bb_platform_drop(void){}
void bb_platform_restore_menu(void){}
#define RELEASE(name) void name(void){releases++;}
RELEASE(bb_save_settle)
RELEASE(bb_last_picture_release)
RELEASE(bb_index_shutdown)
RELEASE(video_gpu_idle)
RELEASE(music_unload)
RELEASE(icons_release)
RELEASE(shelf_release)
RELEASE(room_scene_release)
RELEASE(room_hands_release)
RELEASE(room_scene_tv_maps_release)
bool home_release_cache(void){releases++;return true;}
void bb_mem_probe(const char *s,const bb_system_t *sys){(void)s;(void)sys;}
void room_pace_return_start(void){}
/* Actual menu, prepared core and adapter entries are compiled from their translation units. */
#define APP_NAME "BreadBox"
enum {BB_SAVES_START,BB_SAVES_GO,BB_SAVES_CLOSE=-1};
int bb_remote_slot,bb_remote_stereo;
static int bb_platform_saves(int moment){(void)moment;return BB_SAVES_GO;}
bool dev_remote_game(void){return false;}
void dev_game_ended(bool ok){(void)ok;}
static void breadbox_return_system(const bb_system_t *sys,const char *path){(void)sys;(void)path;}
static bool bb_continue_went_back_take(void){return true;}
static bool bb_pause_last_picture(const char *path){(void)path;return false;}
static void bb_last_picture_from_screen(const char *path){(void)path;}
static void room_walls_game_ended(const bb_system_t *sys,const char *p,const char *t){(void)sys;(void)p;(void)t;}
static char launch_error[384];
static int sw_state;
const bb_system_t *bb_session_system(void);
static bool bb_exec_held(void){return true;}
static bool run_game(const char *path,const char *save,const char *title)
{(void)path;(void)save;(void)title;game_frames++;return true;}
static const char *emu_load_error(void){return "test disc failure";}
static void cards_end(void){}
#define ADAPTER
#define bb_session_system() (&bb_system_gba)
#include "launch-entry.inc"
#undef bb_session_system
bool ps1_adapter_attempt(char *error,size_t cap)
{bb_launch l={.path="test.cue",.slot=-1};return run(&l,error,cap)!=BB_RUN_FAILED;}

int main(void)
{
 covers_init();
 for(held=1;held<=5;held++){
  now=0;releases=core_entries=game_frames=0;wait_log[0]=0;
  import_live=held==1;update_live=held==2;covers_busy_workers=held==3;
  if(held==4)LightLock_Lock(&covers_list_lock);
  if(held==5)LightLock_Lock(&covers_net_lock);
  covers_slots[0].pixels=malloc(4);u8 *pixels=covers_slots[0].pixels;
  assert(!start_game("test.gba",NULL,NULL));
  assert(now==COVERS_STOP_DEADLINE_MS && !releases && !core_entries);
  assert(!bb_session_active() && covers_slots[0].pixels==pixels && covers_wait_error() && !game_frames);
  static const char *const named[]={"", "importer", "update network", "cover loader", "cover list", "cover network"};
  assert(strstr(wait_log,named[held]) && strstr(launch_error,"Try again"));
  now=0;
  /* Enter a prepared session without releasing the fixture's live objects. */
  extern bool ps1_adapter_attempt(char *,size_t);
  assert(!ps1_adapter_attempt(launch_error,sizeof(launch_error)));
  assert(now==COVERS_STOP_DEADLINE_MS && !game_frames && !releases && strstr(launch_error,"Try again"));
  assert(!covers_library_begin() && !library_changes && covers_slots[0].pixels==pixels);
  if(held==4)LightLock_Unlock(&covers_list_lock);
  if(held==5)LightLock_Unlock(&covers_net_lock);
  free(pixels);covers_slots[0].pixels=NULL;
 }
 held=0;
 for(event=1;event<=5;event++){
  now=0;releases=core_entries=0;import_live=false;update_live=false;covers_busy_workers=1;
  event_at=30;closing=false;gpu_right=true;settle_at=event>=4?100:0;
  assert(!start_game("test.gba",NULL,NULL));
  assert(!releases && !core_entries && now<COVERS_STOP_DEADLINE_MS);
  if(event==4)assert(home_jumps>0 && !home_pending);
  closing=false;gpu_right=true;home_pending=false;
 }
 covers_busy_workers=0;gpu_right=false;closing=true;now=0;
 assert(covers_stop_wait_apt() && now==0); /* APT already owns the GPU. */
 gpu_right=true;closing=false;event=5;event_at=30;settle_at=0;now=0;covers_busy_workers=1;
 assert(!start_game("test.gba",NULL,NULL) && now==COVERS_STOP_DEADLINE_MS);
 for(held=1;held<=3;held++){
  event=0;settle_at=12000;now=0;core_entries=releases=0;
  import_live=held==1;update_live=held==2;covers_busy_workers=held==3;
  game_frames=0;
  assert(start_game("test.gba",NULL,NULL));
  assert(now==12000 && core_entries==1 && game_frames==1 && releases>0 && !covers_wait_error());
  now=0;import_live=held==1;update_live=held==2;covers_busy_workers=held==3;
  assert(ps1_adapter_attempt(launch_error,sizeof(launch_error)) && now==12000 && game_frames==2);
 }
 puts("launch deadlines, locked resources, B/HOME/sleep/close, and slow successful drains PASS");
}
