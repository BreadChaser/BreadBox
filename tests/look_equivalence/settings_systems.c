/* Production system adapters and Settings shell, including scoped file writes. */
#define APP_BREADBOX
#define HOST_CUSTOM_SESSION
#define SETTINGS_NAV_TEST
#define SETTINGS_B_MODEL_TEST
#define BB_UI_TRACE_HOST
#define main host_contract_main
#include "../app/host/check.c"
#undef main
#include "../../app/ui/ui_trace.c"
#include "list-scroll.inc"
#include "bb_registry.h"
bool cheats_widescreen(bool on){(void)on;return ws_available();}
bool ws_load(const char *p,const char *s,uint32_t h,int c){(void)p;(void)s;(void)h;(void)c;return false;}
bool rewind_active(void){return false;}bool rewind_start(void){return false;}void rewind_stop(void){}
int cheats_count(void){return 0;}const char *cheats_desc(int i){(void)i;return "";}
bool cheats_on(int i){(void)i;return false;}bool cheats_forced(int i){(void)i;return false;}
void cheats_toggle(int i){(void)i;}const char *cheats_add_dialog(void){return NULL;}
void music_play(bool b){(void)b;}void music_update(void){}
bool audio_capture_start(void){return false;}bool audio_test_tone_toggle(void){return false;}
void breadbox_import_start(bool b){(void)b;}
void sfx_play(int n){ui_trace_sound(n);}void sfx_now(int n){ui_trace_sound(n);}void sfx_default(int n){ui_trace_sound(n);}
void sfx_cancel(void){}void sfx_flush(void){}void sfx_card_closes(void){}bool sfx_close_said(void){return false;}
void bb_credits_run(void){}void bb_install_help_run(void){}bool bb_welcome_open(void){return false;}
const char *bb_update_preference(void){return "Automatic";}void bb_update_settings(void){}
bool room_show_hands,sw_framelog;bool ps1_3d_available(void){return g_new3ds;}
bool bb_session_active(void){return false;}
void ps1_settings_cheats_begin(const char *key){(void)key;}void ps1_settings_cheats_end(void){}
bool ps1_settings_identity(const char *p,const char *b,char *n,size_t c,char s[10]){(void)p;snprintf(n,c,"%s",b);strcpy(s,"SLUS99901");return true;}
const bb_cheat_ops bb_legacy_cheats={0};
static unsigned children;
bool bb_cheats_ui(const bb_cheat_ops *o,bool b){(void)o;(void)b;children++;return false;}
#include "../../app/ui/theme_picker.c"
#include "../../systems/ps1/ps1_settings_ui.c"
#include "../../app/settings/settings_ui.c"
static const bb_system_t ps1={.id="ps1",.name="PlayStation",.short_name="PS1",.settings_model=&bb_ps1_settings};
extern const bb_settings_ops bb_gba_settings;
static const bb_system_t gba={.id="gba",.name="Game Boy Advance",.short_name="GBA",.settings_model=&bb_gba_settings};
const bb_system_t *const bb_systems[]={&ps1,&gba};const unsigned bb_system_count=2;
const bb_system_t *bb_session_system(void){return &ps1;}
u32 dev_ui_keys_take(void){return 0;}
#define HAVE_NO_LANGEXTRA
#define GPU_PICA
#define _3DS
#define USE_ASYNC_CDROM
#include "../../core/frontend/libretro_core_options.h"
static unsigned step;
static void observe(const char *event)
{
 draw_bottom(&state);
 printf("%u %s %s children=%u\n",step++,event,ui_trace_json(),children);
}
static void key(u32 k,const char *event)
{
 fake_down=k;fake_held=host_menu_key=0;input_poll();bool resume=false;
 assert(settings_act(&state,settings_keys(&state),NULL,&resume));assert(!resume);
 ui_card_update(&state.list,level(&state)->selected,state.count);flist_snap(&state.list,level(&state)->selected,state.count,UI_CARD_ROWS);observe(event);
}
static void page(void)
{
 for(int i=0;i<state.count;i++) {
  if(state.rows[i].row.kind==BB_ROW_HEADING)continue;
  level(&state)->selected=i;observe("focus");
  if(state.rows[i].row.kind!=BB_ROW_ACTION && state.rows[i].row.id!=ACTION_HELP) {
   key(KEY_RIGHT,"next-value");key(KEY_LEFT,"previous-value");
   if(state.rows[i].row.can_clear)key(KEY_X,"default");
   fake_touch=(touchPosition){UI_CARD_X+20,UI_CARD_ROWS_Y+(i-(int)state.list.scroll)*UI_CARD_ROW+UI_CARD_ROW/2};
   if(fake_touch.py>=UI_CARD_ROWS_Y && fake_touch.py<UI_CARD_ROWS_Y+UI_CARD_ROWS*UI_CARD_ROW)key(KEY_TOUCH,"touch-value");
  }
 }
 key(KEY_L,"previous-section");key(KEY_R,"next-section");
}
static void files(const char *path)
{
 FILE *f=fopen(path,"rb");if(!f){printf("FILE %s absent\n",path);return;}
 uint32_t hash=2166136261u;char line[1024];while(fgets(line,sizeof(line),f))if(strncmp(line,"theme=",6))for(char *p=line;*p;p++)hash=(hash^(unsigned char)*p)*16777619u;
 fclose(f);printf("FILE %s %u\n",path,hash);
}
int main(int argc,char **argv)
{
 assert(argc==2&&!FT_Init_FreeType(&ft));assert(!FT_New_Face(ft,getenv("PSX3D_TITLE_FONT"),0,&title_face));assert(!FT_New_Face(ft,getenv("PSX3D_BODY_FONT"),0,&body_face));
 mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir(PSX3D_DIR,0700);mkdir(PSX3D_DIR "/games",0700);mkdir(PSX3D_DIR "/config",0700);
 game_presets_init(PSX3D_TEST_PRESETS,PSX3D_DIR "/presets");defaults_init(PSX3D_DEFAULTS);
 static struct retro_core_option_definition defs[MAX_OPTIONS];
 for(int i=0;option_defs_us[i].key&&i<MAX_OPTIONS-1;i++){defs[i].key=option_defs_us[i].key;defs[i].desc=option_defs_us[i].desc;defs[i].info=option_defs_us[i].info;defs[i].default_value=option_defs_us[i].default_value;memcpy(defs[i].values,option_defs_us[i].values,sizeof(defs[i].values));}options_register_v1(defs);
 host_menu_frames=100000;
 for(int notice=0;notice<2;notice++)for(int selected=1;selected<=20;selected++) {
  flist_t list={0};int visible=UI_LIST_ROWS-notice;
  list_scroll(&list,selected,21,visible);
  assert(selected<list.scroll_target+visible);
  printf("LIST selected=%d first=%d rows=%d\n",selected,(int)list.scroll_target,visible);
 }
 for(int model=0;model<2;model++)for(int scope=0;scope<3;scope++)for(unsigned system=0;system<bb_system_count;system++) {
  g_new3ds=model;config_reset_defaults();g_cfg.theme=theme_from_saved_id(argv[1]);
  state=(settings_state){.context={.game=scope!=0,.running=scope==2,.path="fixture.cue",.key="Fixture",.title="Fixture"}};state.rows=calloc(SETTINGS_ROWS,sizeof(*state.rows));assert(state.rows);active=true;
  assert(push(&state,PAGE_SYSTEM,bb_systems[system]));observe("open");page();if(!system){assert(push(&state,PAGE_ADVANCED,&ps1));page();assert(pop(&state));}
  system_open(&state,NULL);free(state.rows);state.rows=NULL;active=false;config_save();files(PSX3D_CONFIG);files(PSX3D_DIR "/games/Fixture.ini");files(PSX3D_DIR "/config/gba.ini");files(PSX3D_DIR "/config/gbs-U3IE-01.ini");
 }
 return 0;
}
