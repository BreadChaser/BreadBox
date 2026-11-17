/* The shared mapping and cheat controllers keep the same state and files. */
#define BB_FEATURES_EQ
#include "dialogs.c"
#include "bb_feature_ui.h"
#include "bb_pause.h"
#include "bb_settings.h"
void music_update(void){}
bool bb_pause_scene_top(const char *tag){return false;}
bool bb_settings_child_top(void){return false;}
bool bb_settings_child_tag_visible(void){return false;}
#include "features-controller.inc"
#include "../../app/ui/components/features.inc"
static unsigned toggled,added;
static bool enabled[2];
static int count_codes(void){return 2;}
static const char *code_name(int i){return i?"Infinite health in the second stage":"Infinite health in the first stage";}
static const char *code_value(int i){return enabled[i]?"On":"Off";}
static void toggle_code(int i){enabled[i]=!enabled[i];toggled++;}
static const char *add_code(void){added++;return "Code added";}
static uint32_t file_hash(const char *path)
{FILE *f=fopen(path,"rb");if(!f)return 0;uint32_t h=2166136261u;int c;while((c=fgetc(f))!=EOF)h=(h^(unsigned)c)*16777619u;fclose(f);return h;}
int main(int argc,char **argv)
{
 assert(argc==2&&!FT_Init_FreeType(&ft));assert(!FT_New_Face(ft,getenv("PSX3D_TITLE_FONT"),0,&title_face));assert(!FT_New_Face(ft,getenv("PSX3D_BODY_FONT"),0,&body_face));
 config_reset_defaults();g_cfg.theme=theme_from_saved_id(argv[1]);
 mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir(PSX3D_DIR,0700);
 for(int model=0;model<2;model++)for(int game=0;game<2;game++) {
  g_new3ds=model;bb_controls c;bb_controls_device_open(&c,"gba",game?"test":NULL,true);
  setup("bindings","..r.l.a.x.d.r.d.r.d.d.d.d.d.d.d.d.d.d.d.d.d.d.d.x.b");int answer=bb_controls_ui(&c,game);
  printf("%u bindings result=%d file=%u overrides=%u",step++,answer,file_hash(game?c.profile.game_path:c.profile.system_path),c.profile.overrides);
  for(unsigned i=0;i<c.profile.count;i++)printf("/%d",c.profile.value[i]);puts("");
  setup("bindings-touch","..t.t.b");fake_touch=(touchPosition){100,55};answer=bb_controls_ui(&c,game);
  printf("%u touch-bindings result=%d file=%u\n",step++,answer,file_hash(game?c.profile.game_path:c.profile.system_path));
  bb_cheat_ops ops={count_codes,code_name,code_value,toggle_code,add_code};
  setup("cheats","..a.d.r.l.d.a.x.u.a.b");answer=bb_cheats_ui(&ops,game);
  printf("%u cheats result=%d toggle=%u add=%u enabled=%d/%d\n",step++,answer,toggled,added,enabled[0],enabled[1]);
  setup("cheats-touch","..t.t.b");fake_touch=(touchPosition){100,55};answer=bb_cheats_ui(&ops,game);
  printf("%u touch-cheats result=%d toggle=%u add=%u\n",step++,answer,toggled,added);
 }
 return 0;
}
