#define ADAPTER __attribute__((section(".sys.ps1.text")))
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "bb_settings.h"
#include "app_paths.h"
#include "app_config.h"
#include "dev_switches.h"
#include "memory_cards.h"
#include "emu.h"
#include "library.h"
#include "libui.h"
#include "home.h"
#include "app_main.h"
#include "bb_settings.h"
extern const bb_settings_ops bb_ps1_settings;
#include "bb_system.h"
extern const bb_cheat_ops bb_legacy_cheats;
#include "devtools.h"
#include "bb_arena.h"
#include "bb_index.h"
extern bool library_poll_adapter(bool refresh);
extern const bb_system_t bb_system_ps1;
#include <strings.h>
#include <stdlib.h>
static ADAPTER bool resolve(const char *title,char *path,size_t cap)
{
 for(int i=0;i<library_count();i++) {
  const game_t *g=library_get(i);
  if(!strcasecmp(title,g->title))return library_launch_path(g,path,cap);
 }
 return false;
}
static ADAPTER bb_run_result run(const bb_launch *l,char *error,size_t cap)
{
 if(!bb_exec_held() || bb_session_system()!=bb_system_by_id("ps1")) {
  snprintf(error,cap,"PlayStation does not own system memory");return BB_RUN_FAILED;
 }
 int previous=sw_state;if(l->slot>=0)sw_state=l->slot;
 bool ok=emu_run_game(l->path,l->save_name,l->title);
 sw_state=previous;
 if(!ok)snprintf(error,cap,"%s",emu_load_error());
 /* Closed from the HOME Menu (emu.c's run_game ended without the GPU right, which only
  * main-thread APT calls change): tearing the core down would wait for the GPU, so it and
  * its CD, dynarec, GPU, SPU and rewind threads still run. Nothing they use is freed from
  * here on, and the exit ends the process at once (bb_exit_abandon). */
 if(ok && !gspHasGpuRight())bb_exit_abandon();
 /* Session restores the menu. The game's cards are saved: drop their shadow
  * copies now, or they stay in the middle of the heap for the next system. */
 else cards_end();
 return ok?BB_RUN_QUIT:BB_RUN_FAILED;
}
static ADAPTER bool state(int slot,bool save) {return emu_debug_state(slot,save);}
#include "system_3d.h"
bool ps1_3d_available(void) {return system_ps1_3d_available(g_3d_screen,g_new3ds);}
static const bb_dev_ops dev={.state=state};
static ADAPTER void resume(bool enabled) {emu_continue_now=enabled;}
static bool catalog(int row,bb_catalog_item *out)
{
 const game_t *g=library_get(row);if(!g)return false;
 snprintf(out->title,sizeof(out->title),"%s",g->title);
 snprintf(out->key,sizeof(out->key),"%s",g->cover_key?g->cover_key:g->title);
 snprintf(out->path,sizeof(out->path),"%s",g->ndiscs?g->discs[0]:"");
 out->cover_id=row;
 out->discs=g->ndiscs;   /* the shelf's multi-disc case; a playlist is one too */
 {size_t n=strlen(out->path);if(out->discs<2 && n>4 && !strcasecmp(out->path+n-4,".m3u"))out->discs=2;}
 return true;
}
static bool cover_item(int row,bb_cover_item *out)
{
 const game_t *g=library_get(row);
 if(!g)return false;
 *out=(bb_cover_item){.title=g->title,.key=g->cover_key,.code=g->group,.path=g->ndiscs>0?g->discs[0]:NULL};
 return true;
}
static bool launch(int row,char *path,size_t cap) {const game_t *g=library_get(row);return g && library_launch_path(g,path,cap);}
static void continue_release(void *arg)
{
 game_t *g=arg;free(g->cover_key);
 for(int d=0;d<g->ndiscs;d++)free(g->discs[d]);
 free(g);
}
static bool continue_resolve(void *arg,char *path,size_t cap)
{
 return library_launch_path(arg,path,cap)!=NULL;
}
static bool continue_capture(const char *title,bb_path_snapshot *out)
{
 for(int i=0;title && *title && i<library_count();i++) {
  const game_t *g=library_get(i);if(strcmp(g->title,title))continue;
  game_t *copy=calloc(1,sizeof(*copy));if(!copy)return false;
  copy->ndiscs=g->ndiscs;copy->cover_key=strdup(g->cover_key?g->cover_key:g->title);
  bool ok=copy->cover_key!=NULL;
  for(int d=0;d<copy->ndiscs;d++) {
   copy->discs[d]=strdup(g->discs[d]);copy->disc_no[d]=g->disc_no[d];
   ok=ok && copy->discs[d]!=NULL;
  }
  if(!ok){continue_release(copy);return false;}
  *out=(bb_path_snapshot){.data=copy,.resolve=continue_resolve,.release=continue_release};return true;
 }
 return false;
}
static const bb_shell_ops shell={.catalog=catalog,.launch=launch,.init=emu_init,.deinit=emu_deinit,.resume=resume,.title=emu_game_title,.legacy_continue=true,.count=library_count,.resolve=resolve,
 .poll_library=library_poll_adapter,.continue_capture=continue_capture,
};
static int setting_value(unsigned row)
{
 const int *fields[]={&g_cfg.smooth,&g_cfg.stretch,&g_cfg.controller};return row<3?*fields[row]:0;
}
static const bb_setting_desc settings[]={
 {.key="smooth",.label="Smoothing",.kind=BB_SET_TOGGLE,.category=BB_CAT_DISPLAY,.models=BB_MODEL_ALL},
 {.key="stretch",.label="Stretch",.kind=BB_SET_TOGGLE,.category=BB_CAT_DISPLAY,.models=BB_MODEL_ALL},
 {.key="controller",.label="Analog controller",.kind=BB_SET_TOGGLE,.category=BB_CAT_CONTROLS,.models=BB_MODEL_ALL}
};
static bool continue_available(const bb_content_id *id,uint32_t *saved)
{
 (void)id;char prefix[PATH_LEN];
 extern bool states_auto_available(const char *,u32 *);
 if(!g_cfg.last_game_state[0])return false;
 snprintf(prefix,sizeof(prefix),"%s/%.400s",PSX3D_STATE_DIR,g_cfg.last_game_state);
 return states_auto_available(prefix,saved);
}
static const char *const extensions[]={".cue",".chd",".pbp",".iso",".img",".mdf",".m3u",".bin",NULL};
static const char *const hints[]={"playstation","sony playstation","ps1","psx","1994",NULL};
extern bb_probe_result bb_ps1_probe(const bb_probe_input *,bb_entry *);
extern bool bb_ps1_identify(const char *,const bb_entry *,bb_content_id *,char *,size_t);
/* A jewel case on the shelf: 142 x 125 x 10.5 mm (North America / Japan, one disc); the shelf
 * makes PAL cases 17 mm and multi-disc cases 25 mm (shelf.c case_depth). */
static const bb_package package={.width=142,.height=125,.depth=10.5f,.style=BB_PACKAGE_CASE,
 .spine_head=0xff100d0d,.band=0xff1b1716};
const bb_system_t bb_system_ps1={.cheats=&bb_legacy_cheats,.settings_model=&bb_ps1_settings,.abi=BB_SYSTEM_ABI,.id="ps1",.name="PlayStation",.short_name="PS1",.release_year=1994,.release_month=12,
 .budget={{.exec_kib=8320},{.exec_kib=8320}},
 .models=BB_MODEL_ALL,.link=BB_LINK_STATIC,.stereo=BB_STEREO_LAYERS,
 .stereo_available=ps1_3d_available,
 .scan_priority=1,.probe_bytes=0,.probe=bb_ps1_probe,.identify=bb_ps1_identify,
 .settings=settings,.nsettings=sizeof(settings)/sizeof(*settings),.setting_value=setting_value,
 .continue_available=continue_available,
 .extensions=extensions,.folder_hints=hints,.run=run,.save=NULL,.shell=&shell,.dev=&dev,
 .cover_item=cover_item,.cover_db="Sony - PlayStation",.preset_dir="romfs:/presets",
 /* The PlayStation logo is not in the app: the cover downloader fetches it once (Wikimedia Commons, 120 px). */
 .logo_url="https://upload.wikimedia.org/wikipedia/commons/thumb/4/4e/Playstation_logo_colour.svg/120px-Playstation_logo_colour.svg.png",
 .package=&package,
};
