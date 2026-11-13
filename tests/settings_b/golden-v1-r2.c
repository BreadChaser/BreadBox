#define APP_BREADBOX
#define SETTINGS_NAV_TEST
#define SETTINGS_B_MODEL_TEST
#define main harness_main
#include "../app/host/check.c"
#undef main
bool cheats_widescreen(bool on){(void)on;return ws_available();}
bool ws_load(const char *p,const char *s,uint32_t h,int c){(void)p;(void)s;(void)h;(void)c;return false;}
bool rewind_active(void){return false;}bool rewind_start(void){return false;}void rewind_stop(void){}
int cheats_count(void){return 0;}const char *cheats_desc(int i){(void)i;return "";}
bool cheats_on(int i){(void)i;return false;}bool cheats_forced(int i){(void)i;return false;}
void cheats_toggle(int i){(void)i;}const char *cheats_add_dialog(void){return NULL;}
void music_play(bool b){(void)b;}void music_update(void){}
bool audio_capture_start(void){return false;}bool audio_test_tone_toggle(void){return false;}
void breadbox_import_offer(bool b){(void)b;}
void sfx_play(int n){(void)n;}
void sfx_now(int n){(void)n;}void sfx_default(int n){(void)n;}void sfx_cancel(void){}void sfx_flush(void){}void sfx_card_closes(void){}bool sfx_close_said(void){return false;}
void bb_credits_run(void){}
bool room_show_hands;
bool ps1_3d_available(void){return g_new3ds;}
bool bb_session_active(void){return false;}
bool bb_settings_child_top(void){return false;}
bool bb_settings_child_tag_visible(void){return true;}
bool settings_ui_active(void){return false;}bool settings_ui_debug_capture(void){return false;}
void ps1_settings_cheats_begin(const char *key){(void)key;}void ps1_settings_cheats_end(void){}
bool ps1_settings_identity(const char *path,const char *base,char *name,size_t cap,char serial[10]){
 (void)path;snprintf(name,cap,"%s",base);strcpy(serial,"SLUS99901");return true;
}
#include "bb_system.h"
const bb_cheat_ops bb_legacy_cheats={0};
bool bb_cheats_ui(const bb_cheat_ops *o,bool b){(void)o;(void)b;return false;}
#include "../../app/ui/theme_picker.c"
#include "../settings/fixtures/v1-r2-retired.h"
#include "../settings/fixtures/v1-r2/settings_ui.c"
#define HAVE_NO_LANGEXTRA
#define GPU_PICA
#define _3DS
#define USE_ASYNC_CDROM
#include "../../core/frontend/libretro_core_options.h"
int main(void){
 mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir(PSX3D_DIR,0700);
 game_presets_init(PSX3D_TEST_PRESETS,PSX3D_DIR "/presets");defaults_init(PSX3D_DEFAULTS);
 static struct retro_core_option_definition defs[MAX_OPTIONS];
 for(int i=0;option_defs_us[i].key&&i<MAX_OPTIONS-1;i++){
  defs[i].key=option_defs_us[i].key;defs[i].desc=option_defs_us[i].desc;defs[i].info=option_defs_us[i].info;defs[i].default_value=option_defs_us[i].default_value;
  memcpy(defs[i].values,option_defs_us[i].values,sizeof(defs[i].values));
 }options_register_v1(defs);
 for(int newer=0;newer<2;newer++)for(int scope=0;scope<3;scope++)for(int debug=0;debug<2;debug++) {
  g_new3ds=newer;config_reset_defaults();g_cfg.debug=debug;system_settings=true;
  if(scope){config_game_begin("Golden");if(scope==2)game_presets_disc_log("CD-ROM ID: SLUS99901\n");else game_presets_reset();}
  settings_ui_t model={.game=scope!=0};
  int pages[]={CAT_DISPLAY,CAT_CONTROLS,CAT_SAVES,CAT_ADVANCED,CAT_DEBUG,CAT_BINDINGS};
  for(unsigned p=0;p<sizeof(pages)/sizeof(*pages);p++) {
   if(pages[p]==CAT_DEBUG && !debug)continue;
   model.page=pages[p];build_page(&model,pages[p]);
   for(int i=0;i<model.count;i++){
    int item=model.items[i];if(item==F_THEME||item==F_HEADING||item==CATEGORY_ROW+CAT_DEBUG)continue;
    const char *label=item>=CONTROL_ROW && item<CHEAT_ROW?input_control_label(item-CONTROL_ROW,NULL):item>=CATEGORY_ROW?categories[item-CATEGORY_ROW]:item>=CORE_ROW?options_desc(item-CORE_ROW):labels[item];
    printf("%d\t%d\t%d\t%s\t%s\n",newer,scope,debug,pages[p]==CAT_BINDINGS?"binding":"model",label);
   }
  }
  if(scope)config_game_end();game_presets_reset();
 }
}
