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
static int captures,tones;
bool audio_capture_start(void){captures++;return true;}bool audio_test_tone_toggle(void){tones++;return true;}
void breadbox_import_start(bool b){(void)b;}
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
#include "../../systems/ps1/ps1_settings_ui.c"
#define HAVE_NO_LANGEXTRA
#define GPU_PICA
#define _3DS
#define USE_ASYNC_CDROM
#include "../../core/frontend/libretro_core_options.h"
int main(void){
 assert(!FT_Init_FreeType(&ft));
 assert(!FT_New_Face(ft,getenv("PSX3D_TITLE_FONT"),0,&title_face));
 assert(!FT_New_Face(ft,getenv("PSX3D_BODY_FONT"),0,&body_face));
 mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir(PSX3D_DIR,0700);
 game_presets_init(PSX3D_TEST_PRESETS,PSX3D_DIR "/presets");
 defaults_init(PSX3D_DEFAULTS);
 static struct retro_core_option_definition defs[MAX_OPTIONS];
 for(int i=0;option_defs_us[i].key&&i<MAX_OPTIONS-1;i++){
  defs[i].key=option_defs_us[i].key;defs[i].desc=option_defs_us[i].desc;defs[i].info=option_defs_us[i].info;defs[i].default_value=option_defs_us[i].default_value;
  memcpy(defs[i].values,option_defs_us[i].values,sizeof(defs[i].values));
 }
 options_register_v1(defs);
 for(int newer=0;newer<2;newer++){
  g_new3ds=newer;config_reset_defaults();
  bb_settings_context ctx={0};assert(model_open(&ctx));
  g_cfg.theme=theme_count()-1;g_cfg.hand_skin=3;g_cfg.hand_glove=4;
  strcpy(g_cfg.games_dir,"sdmc:/kept");input_hotkey_set(0,5);input_hotkey_set(1,7);
  config_user_changed("theme");config_user_changed(input_hotkey_key_name(0));
  int maps=input_map_raw(0);input_map_set(0,INPUT_TURBO_A);
  model_reset(BB_RESET_CONTROLS);assert(input_map_raw(0)==maps);
  assert(input_hotkey_get(0)==5&&input_hotkey_get(1)==7);
  g_cfg.smooth=1;config_user_changed("smooth");model_reset(BB_RESET_SCOPE);
  assert(!strcmp(g_cfg.games_dir,"sdmc:/kept")&&g_cfg.hand_skin==3&&g_cfg.hand_glove==4);
  assert(g_cfg.theme==theme_count()-1&&config_setting_changed("theme"));
  assert(input_hotkey_get(0)==5&&input_hotkey_get(1)==7);
  model_close();
  ctx=(bb_settings_context){.game=true,.path="fixture.cue",.key="Fixture",.title="Fixture"};assert(model_open(&ctx));
  mkdir(PSX3D_DIR "/cheats",0700);mkdir(PSX3D_DIR "/states",0700);mkdir(PSX3D_DIR "/memcards",0700);
  const char *kept[]={PSX3D_DIR "/memcards/Fixture.1.mcd",PSX3D_DIR "/states/Fixture.st9",PSX3D_DIR "/cheats/Fixture.on",PSX3D_DIR "/cheats/Fixture.cht"};
  for(unsigned i=0;i<sizeof(kept)/sizeof(*kept);i++){FILE *f=fopen(kept[i],"w");assert(f);fputs("preserved",f);fclose(f);}
  model_change(F_FPS,1);assert(gamecfg_int("show_fps",-1)==g_cfg.show_fps);
  if(newer){model_change(F_STEREO,1);assert(gamecfg_int("stereo_style",-1)==g_cfg.stereo_screen);}
  host_menu_frames=1;host_present_count=0;host_presented_game=true;
  model_change(F_RECOMMENDED,1);assert(host_present_count && !host_presented_game);host_menu_frames=-1;
  model_change(F_RECORD,1);model_change(F_TONE,1);assert(!model_resume_requested()&&!captures&&!tones);
  int smooth=-1,advanced=-1,advanced_id=-1;
  for(unsigned j=0;j<model_count;j++){
   bb_settings_row row;assert(model_read(j,&row));
   if(row.id==F_SMOOTH)smooth=j;
   if(row.id>=CORE_ROW&&row.id<CONTROL_ROW&&!strcmp(options_key(row.id-CORE_ROW),"pcsx_rearmed_nostalls")){advanced=j;advanced_id=row.id;assert(row.source==BB_SOURCE_RECOMMENDED);}
  }
  assert(smooth>=0&&advanced>=0);
  gamecfg_set(input_hotkey_key_name(0),input_hotkey_combo_name(input_hotkey_get(0)));
  char exception[64];snprintf(exception,sizeof(exception),"%s",gamecfg_get(input_hotkey_key_name(0)));
  model_reset(BB_RESET_CONTROLS);
  assert(gamecfg_get(input_hotkey_key_name(0)) && !strcmp(gamecfg_get(input_hotkey_key_name(0)),exception));
  bb_settings_hotkey_cycle(0,1);
  assert(!strcmp(gamecfg_get(input_hotkey_key_name(0)),exception));
  /* the shared shortcuts and the button side have one home, Settings > Customize: not in PS1's list or Button mappings */
  for(unsigned i=0;i<model_count;i++){bb_settings_row row;model_read(i,&row);assert(strcmp(row.label,"FF keys") && strcmp(row.label,"Rewind keys") && strcmp(row.label,"Bottom-screen buttons"));}
  {settings_ui_t b={.game=true};build_page(&b,CAT_BINDINGS);for(int i=0;i<b.count;i++)assert(shared_hotkey_row(b.items[i])<0);}
  model_change(F_SMOOTH,1);bb_settings_row row;model_read(smooth,&row);assert(row.source==BB_SOURCE_CHANGED);
  model_change(F_SMOOTH,1);model_read(smooth,&row);assert(row.source==BB_SOURCE_CHANGED);
  assert(g_cfg.smooth==default_fields[0]);
  model_close();assert(model_open(&ctx));model_read(smooth,&row);assert(row.source==BB_SOURCE_CHANGED);
  model_clear(F_SMOOTH);model_read(smooth,&row);assert(row.source==BB_SOURCE_SYSTEM);
  model_change(advanced_id,1);model_read(advanced,&row);assert(row.source==BB_SOURCE_CHANGED);
  model_clear(advanced_id);model_read(advanced,&row);assert(row.source==BB_SOURCE_RECOMMENDED);
  model_reset(BB_RESET_SCOPE);
  for(unsigned i=0;i<sizeof(kept)/sizeof(*kept);i++){char data[32];FILE *f=fopen(kept[i],"r");assert(f&&fgets(data,sizeof(data),f));fclose(f);assert(!strcmp(data,"preserved"));}
  model_close();assert(!config_in_game());
  config_game_begin("Running");ctx=(bb_settings_context){.game=true,.running=true,.title="Running"};assert(model_open(&ctx));
  g_cfg.debug=1;model_rows();bool record=false,tone=false;
  for(unsigned i=0;i<model_count;i++){bb_settings_row row;model_read(i,&row);record|=row.id==F_RECORD;tone|=row.id==F_TONE;
   assert(row.id!=CATEGORY_ROW+CAT_CHEATS);   /* a running game's Cheats are on its pause card */
   assert(row.advanced==(row.id==F_RECORD || row.id==F_TONE || row.id==F_RECOMMENDED || (row.id>=CORE_ROW && row.id<CONTROL_ROW && !strstr(options_key(row.id-CORE_ROW),"memcard") && !strstr(options_key(row.id-CORE_ROW),"analog"))));}
  assert(record&&tone);int old_captures=captures,old_tones=tones;
  model_change(F_RECORD,1);assert(captures==old_captures+1&&bb_ps1_settings.resume_requested());
  resume_requested=false;model_change(F_TONE,1);assert(tones==old_tones+1&&bb_ps1_settings.resume_requested());
  model_close();config_game_end();g_cfg.debug=0;captures=tones=0;
 }
 puts("PS1 production adapter complete row ledger, scoped resets, prelaunch identity and provenance PASS");
 FT_Done_Face(body_face);FT_Done_Face(title_face);FT_Done_FreeType(ft);
 return 0;
}
