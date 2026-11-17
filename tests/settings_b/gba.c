#define APP_BREADBOX
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "bb_settings.h"
#include "app_config.h"
#include "app_input.h"
#include "app_main.h"
#include "bb_settings.h"
#include "gba_settings.h"
#include "gba_touch.h"
#include "game_palette.h"
game_palette_t g_game_palette;
#include "gba_saveid.h"
#include "bb_feature_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
static int probes,hotkeys[2]={39,35},side;
bool gba_touch_overridden(unsigned);   /* gba_settings.c (gba_touch.c declares it where it uses it) */
bool g_new3ds;
int control_side(void){return side;}void control_side_set(int v){side=!!v;}
int input_hotkey_get(int i){return hotkeys[i];}
int bb_settings_hotkey_get(int i){return hotkeys[i];}
void bb_settings_hotkey_cycle(int i,int d){hotkeys[i]+=d;}
void bb_settings_hotkey_default(int i){hotkeys[i]=i?35:39;}
const char *input_hotkey_combo_name(int n){return n==35?"L + Select":"R + Select";}
void input_hotkey_cycle(int i,int d){bb_settings_hotkey_cycle(i,d);}
void config_save(void){}
bool gba_saveid_copy(const char *a,const char *b){(void)a;(void)b;return true;}
bool gba_saveid_path(const char *path,char id[GBA_ID_LEN]){strcpy(id,strstr(path,"boktai")?"gbs-U3IE-01":"gbs-AXVE-01");return true;}
bool gba_cart_header(const char *path,unsigned char head[192]){probes++;memset(head,0,192);memcpy(head+0xac,strstr(path,"boktai")?"U3IE":"AXVE",4);return true;}
bool ps1_touch_descriptor(unsigned i,bool n,control_descriptor_t *d){(void)i;(void)n;(void)d;return false;}
bb_controls bb_active_controls;
void bb_controls_device_open(bb_controls *c,const char *sys,const char *key,bool shoulders){uint32_t keys[BB_CONTROL_BUTTONS]={0};bb_controls_open(c,sys,key,g_new3ds?BB_NEW:BB_OLD,keys,shoulders);}
bool bb_controls_ui(bb_controls *c,bool game){(void)c;(void)game;return false;}
bool bb_cheats_ui(const bb_cheat_ops *c,bool game){(void)c;(void)game;return false;}
const char *bb_cheats_keyboard(bb_cheats *c){(void)c;return NULL;}
const bb_cheat_ops bb_gba_cheats={0};
#include "../../systems/gba/gba_settings_model.c"
static int find(int id){for(unsigned i=0;i<rows();i++)if(fields[i]==id)return i;return -1;}
static void sentinel(const char *path){FILE *f=fopen(path,"wb");assert(f);fputs("untouched",f);fclose(f);}
static void kept(const char *path){char b[20]={0};FILE *f=fopen(path,"rb");assert(f);fread(b,1,19,f);fclose(f);assert(!strcmp(b,"untouched"));}
int main(void){
 mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir("sdmc:/3ds/BreadBox",0700);
 mkdir("sdmc:/3ds/BreadBox/config",0700);mkdir("sdmc:/3ds/BreadBox/saves",0700);
 const char *other="sdmc:/3ds/BreadBox/config/other.ini",*save="sdmc:/3ds/BreadBox/saves/kept.sav";sentinel(other);sentinel(save);
 for(int model=0;model<2;model++){
  g_new3ds=model;bb_settings_context c={0};assert(open_model(&c));assert(find(GBA_SET_SOLAR)<0);assert(find(GBA_SET_STEREO)<0);assert((find(GBA_SET_REWIND)>=0)==(bool)model);
  change_row(GBA_SET_COLOR,1);change_row(100,1);hotkeys[0]=9;side=1;
  reset_model(BB_RESET_SCOPE);assert(gba_settings.color_correction==GBA_SETTINGS_DEFAULT.color_correction);assert(hotkeys[0]==9&&side==1);close_model();
  c=(bb_settings_context){.game=true,.path="boktai.gba",.title="Boktai"};int before=probes;assert(open_model(&c));assert(probes==before+1);assert(find(GBA_SET_SOLAR)>=0);
  int r=find(GBA_SET_COLOR);bb_settings_row row;assert(read_row(r,&row)&&row.source==BB_SOURCE_SYSTEM);
  change_row(GBA_SET_COLOR,1);assert(read_row(r,&row)&&row.source==BB_SOURCE_CHANGED);
  /* An explicit game value equal to its default keeps Changed provenance. */
  for(int n=0;n<3;n++)change_row(GBA_SET_COLOR,1);
  assert(!gba_settings.color_correction && read_row(r,&row)&&row.source==BB_SOURCE_CHANGED);
  change_row(GBA_SET_SOLAR,1);clear_row(GBA_SET_COLOR);assert(read_row(r,&row)&&row.source==BB_SOURCE_SYSTEM);
  int sunlight=gba_settings.solar;reset_model(BB_RESET_CONTROLS);assert(gba_settings.solar==sunlight&&hotkeys[0]==9&&side==1);
  reset_model(BB_RESET_SCOPE);assert(gba_settings.solar==5);close_model();
  c.path="emerald.gba";assert(open_model(&c));assert(find(GBA_SET_SOLAR)<0);close_model();kept(other);kept(save);
 }
 /* The on-screen touch buttons are off in 1.0 (gba_touch.h GBA_TOUCH_BUTTONS 0): no rows on any page, a saved binding
    in config.ini does not bring a button back (read as Hide, dropped when saved), no key is bound to a slot, no intro;
    the FPS counter keeps its row and value. */
 assert(!GBA_TOUCH_BUTTONS);
 {
  FILE *f=fopen("sdmc:/3ds/BreadBox/config/gba.ini","wb");assert(f);
  fputs("touch_a=8\ntouch_b=0\ntouch_ff=18\ntouch_rewind=20\nshow_fps=1\n",f);fclose(f);
  gba_settings_load();
  for(unsigned i=0;i<4;i++)assert(gba_touch_settings.binding[i]==GBA_TOUCH_HIDE && !gba_touch_overridden(i));
  assert(gba_touch_settings.fps==1);
  gba_touch_setting_set(0,8);gba_touch_setting_set(2,GBA_TOUCH_FF_TOGGLE);
  assert(gba_touch_settings.binding[0]==GBA_TOUCH_HIDE && gba_touch_settings.binding[2]==GBA_TOUCH_HIDE);
  for(int model=0;model<2;model++){
   uint32_t keys[16]={0},ta=0,tb=0;gba_touch_begin(keys,&ta,&tb,model,true);
   for(int k=0;k<16;k++)assert(!(keys[k]&GBA_TOUCH_BITS));
   assert(!ta && !tb && !gba_touch_keys());
   for(int scope=0;scope<3;scope++){
    g_new3ds=model;bb_settings_context c={.game=scope!=0,.running=scope==2,.path="emerald.gba",.title="Touch"};assert(open_model(&c));
    for(int id=100;id<104;id++)assert(find(id)<0);
    assert(find(105)>=0);close_model();
   }
  }
  gba_settings_load();gba_settings_save();
  char buf[256]={0};f=fopen("sdmc:/3ds/BreadBox/config/gba.ini","rb");assert(f);fread(buf,1,sizeof(buf)-1,f);fclose(f);
  assert(!strstr(buf,"touch_a") && !strstr(buf,"touch_b") && !strstr(buf,"touch_ff") && !strstr(buf,"touch_rewind") && strstr(buf,"show_fps=1"));
  assert(!gba_touch_intro());
  remove("sdmc:/3ds/BreadBox/config/gba.ini");gba_settings_load();
  puts("GBA touch buttons off: no Touch A/B, Fast forward or Rewind rows (Old/New, every scope), saved bindings read as Hide and dropped on save, no key on a slot, no intro; FPS counter kept PASS");
 }
 for(int model=0;model<2;model++)for(int scope=0;scope<3;scope++) {
  g_new3ds=model;bb_settings_context c={.game=scope!=0,.path=scope==2?"boktai.gba":"emerald.gba",.title="Ledger"};assert(open_model(&c));
  for(unsigned i=0;i<rows();i++){bb_settings_row r;assert(read_row(i,&r));assert(r.short_help && strlen(r.short_help)<=36 && strchr(r.short_help,'.'));}
  for(int f=0;f<=GBA_SET_HOTKEY_REWIND;f++) {
   int i=find(f);if(i<0)continue;bb_settings_row r;assert(read_row(i,&r));assert(r.short_help && strlen(r.short_help)<=36 && strchr(r.short_help,'.'));
   /* Explicit rename: the shortcut rows are "FF keys" / "Rewind keys" (the touch slots keep
      "Fast forward" / "Rewind"); the frozen inventory keeps the old names */
   const char *frozen=r.label;
   if(f==GBA_SET_SMOOTH){assert(!strcmp(r.label,"Filter"));frozen="Smoothing";}
   if(f==GBA_SET_COLOR){assert(!strcmp(r.label,"Color"));frozen="Color correction";}
   if(f==GBA_SET_BLEND){assert(!strcmp(r.label,"LCD ghosting"));frozen="Frame blend";}
   if(f==GBA_SET_HOTKEY_FF){assert(!strcmp(r.label,"FF keys"));frozen="Fast forward";}
   if(f==GBA_SET_HOTKEY_REWIND){assert(!strcmp(r.label,"Rewind keys"));frozen="Rewind";}
   printf("GBALEDGER\t%d/%d\tmodel\t%s\n",model,scope,frozen);
  }
  assert(find(200)>=0);printf("GBALEDGER\t%d/%d\tmodel\tControls\n",model,scope);
  for(unsigned i=0;i<bb_controls_rows_shown(&bindings);i++)printf("GBALEDGER\t%d/%d\tbinding\t%s\n",model,scope,bindings.profile.schema[bb_controls_row_at(&bindings,i)].label);
  close_model();
 }
 /* a running game: the pause card owns Controls and Cheats; the shared shortcuts live in Settings > Customize */
 for(int model=0;model<2;model++){
  g_new3ds=model;bb_settings_context c={.game=true,.running=true,.path="emerald.gba",.title="Running"};assert(open_model(&c));
  assert(find(200)<0 && find(201)<0 && find(GBA_SET_HOTKEY_FF)<0 && find(GBA_SET_HOTKEY_REWIND)<0 && find(104)<0);
  for(unsigned i=0;i<rows();i++){bb_settings_row r;assert(read_row(i,&r) && !r.advanced && r.source!=BB_SOURCE_ALL);}
  close_model();
  c.running=false;assert(open_model(&c));assert(find(200)>=0 && find(201)>=0);close_model();
 }
 puts("GBA production adapter solar visibility, explicit sources, controls/game/system resets and retained files PASS");
}
