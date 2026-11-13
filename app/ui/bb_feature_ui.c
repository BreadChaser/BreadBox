#include "platform_video.h"
#include "app_input.h"
#include "ui_list.h"
#include "ui_pause.h"
#include "ui.h"
#include "uisound.h"
#include "libui.h"
#include "app_main.h"
#include "bb_feature_ui.h"
#include "ui_format.h"
#include "bb_pause.h"
#include "bb_settings.h"
#include "devtools.h"
#include <stdio.h>
bb_controls bb_active_controls;
void bb_controls_device_open(bb_controls *c,const char *sys,const char *key,bool shoulders) {
 const uint32_t keys[]={KEY_A,KEY_B,KEY_X,KEY_Y,KEY_L,KEY_R,g_new3ds?KEY_ZL:0,g_new3ds?KEY_ZR:0,KEY_START,KEY_SELECT,KEY_UP|KEY_CPAD_UP,KEY_DOWN|KEY_CPAD_DOWN,KEY_LEFT|KEY_CPAD_LEFT,KEY_RIGHT|KEY_CPAD_RIGHT};
 bb_controls_open(c,sys,key,g_new3ds?BB_NEW:BB_OLD,keys,shoulders);
}
static flist_t feature_list;
static int feature_row;
static bb_controls *controls;
static const bb_cheat_ops *cheat_ops;
static const char *feature_notice;
static ui_button_t feature_bar[]={
 {KEY_B,"Back",UI_ICON_BACK,false},{KEY_A,"Change",UI_ICON_OPEN,false},{KEY_X,"Reset / Add",UI_ICON_RESET,false}
};
static bool feature_game;   /* opened from the pause card: the paused game stays on the top screen */
static const char *feature_label(int row,void *unused) {
 (void)unused;
 if(controls)return controls->profile.schema[bb_controls_row_at(controls,(unsigned)row)].label;
 if(row>=cheat_ops->count())return "Add cheat";
 /* a long start shared with the row above is folded to "... ": the part that differs is what fits; the
  * focused cheat's whole name is on the top screen */
 const char *name=cheat_ops->label(row);
 size_t fold=row?ui_shared_words(name,cheat_ops->label(row-1),UI_FOLD_MIN):0;
 if(!fold)return name;
 static char folded[2][160];static int which;
 char *out=folded[which^=1];
 snprintf(out,sizeof(folded[0]),UI_FOLD_MARK "%s",name+fold);
 return out;
}
/* the focused row's whole name, for the top screen's tag (NULL: nothing to add) */
static const char *feature_full(void) {
 if(controls || !cheat_ops || feature_row>=cheat_ops->count())return NULL;
 return cheat_ops->label(feature_row);
}
static const char *feature_value(int row,void *unused) {
 (void)unused;
 if(controls)return bb_controls_value(controls,bb_controls_row_at(controls,(unsigned)row));
 return row<cheat_ops->count()?cheat_ops->value(row):">";
}
static void feature_top(void *);
static void feature_bottom(void *);
static bool run(bool game) {
 feature_row=0;feature_list.init=false;feature_notice=NULL;feature_game=game;ui_swap_enter();
 /* the bar says what A and X do here: a cheat is switched on or off; X adds one (or resets a mapping) */
 feature_bar[1].label=controls?"Change":"On / Off";feature_bar[1].icon=controls?UI_ICON_CHANGE:UI_ICON_TOGGLE;
 feature_bar[2].label=controls?"Use default":"Add code";feature_bar[2].icon=controls?UI_ICON_RESET:UI_ICON_PLUS;
 while(aptMainLoop()) {
  if(dev_tick(game))return true;
  if(dev_ui_pending())return false;
  input_poll();int n=controls?(int)bb_controls_rows_shown(controls):cheat_ops->count()+1;
  u32 k=ui_card_input(&feature_list,n,&feature_row,ui_bar_input(feature_bar,3,input_keys_down()));
  k=ui_swap_keys(k,UI_SWAP_MENU);
  if(k&KEY_B)return false;
  /* one sound per press: a mapping changes (value), a cheat flips (toggle), adding opens the keyboard (select) */
  if(k&KEY_X) {
   if(controls){if(!bb_profile_clear(&controls->profile,bb_controls_row_at(controls,(unsigned)feature_row))){feature_notice="Could not save controls";sfx_play(SFX_ERROR);}else sfx_play(SFX_VALUE);}
   else if(cheat_ops->add){sfx_play(SFX_SELECT);feature_notice=cheat_ops->add();}
  } else if(n && (k&(KEY_A|KEY_LEFT|KEY_RIGHT))) {
   if(controls){if(!bb_controls_change(controls,bb_controls_row_at(controls,(unsigned)feature_row),k&KEY_LEFT?-1:1)){feature_notice="Could not save controls";sfx_play(SFX_ERROR);}else sfx_play(SFX_VALUE);}
   else if(feature_row<cheat_ops->count()){cheat_ops->toggle(feature_row);sfx_play(SFX_TOGGLE);}
   else if((k&KEY_A) && cheat_ops->add){sfx_play(SFX_SELECT);feature_notice=cheat_ops->add();}
  }
  if(controls && bb_controls_warning(controls,bb_controls_row_at(controls,(unsigned)feature_row)))feature_notice=bb_controls_warning(controls,bb_controls_row_at(controls,(unsigned)feature_row));
  if(!gspHasGpuRight())return true;
  n=controls?(int)bb_controls_rows_shown(controls):cheat_ops->count()+1;flist_update(&feature_list,feature_row,n,UI_CARD_ROWS);
  if(!game)music_update();
  ui_begin();video_present_vsync(false,feature_top,feature_bottom,NULL);
 }
 return true;
}
bool bb_controls_ui(bb_controls *c,bool game){controls=c;cheat_ops=NULL;bool r=run(game);controls=NULL;ui_swap_enter();return r;}
bool bb_cheats_ui(const bb_cheat_ops *ops,bool game){controls=NULL;cheat_ops=ops;bool r=ops?run(game):false;ui_swap_enter();return r;}
const char *bb_cheats_keyboard(bb_cheats *c) {
 char name[129]="",code[1024]="";SwkbdState kb;
 swkbdInit(&kb,SWKBD_TYPE_QWERTY,2,128);swkbdSetHintText(&kb,"Cheat name");
 if(swkbdInputText(&kb,name,sizeof(name))!=SWKBD_BUTTON_CONFIRM)return NULL;
 swkbdInit(&kb,SWKBD_TYPE_QWERTY,2,1023);swkbdSetHintText(&kb,"Core cheat code (use + between codes)");
 if(swkbdInputText(&kb,code,sizeof(code))!=SWKBD_BUTTON_CONFIRM)return NULL;
 return bb_cheats_add(c,name,code)?"Code added. Press A on it to turn it on.":"Could not add the code. Check the name, the code and the SD card.";
}
#include "components/features.inc"
