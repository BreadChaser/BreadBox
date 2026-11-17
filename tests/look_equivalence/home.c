/* Real home navigation and touch dispatch; the scene is an external boundary. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_config.h"
#include "ui.h"
#include "uisound.h"
#include "home_model.h"
#include "list_look.h"
#include "home_view.h"
#include "ui_motion.h"
#include "../looks/paint.h"
void ui_trace_viewport(int first,int count){(void)first;(void)count;}
void ui_trace_target(int id,int x,int y,int w,int h){(void)id;(void)x;(void)y;(void)w;(void)h;}
psx3d_config g_cfg;
#include "../../app/ui/theme.c"
#include "../../app/ui/look_theme.c"
#include "../../app/ui/home_model.c"
#include "../../app/ui/list_look.c"
static bool have_games=true,cont_ok=true,room_quit_on;
static int sel_id=E_LIBRARY,ntiles;
typedef struct {int id;} tile_t;
static tile_t tiles[6];
static room_file file;
static room_view room;
#define HOME_PLAY_TOGETHER 0
static unsigned sound;
void sfx_play(int n){sound=(sound^(unsigned)(n+1))*16777619u;}
static int tab_hit=-1;static touchPosition touch;
void hidTouchRead(touchPosition *p){*p=touch;}
int ui_tabs_hit(int n,u32 keys){(void)n;return keys&KEY_TOUCH?tab_hit:-1;}
u32 ui_bar_input(const ui_button_t *b,int n,u32 k){(void)b;(void)n;return k;}
static bb_home_picture home_list_picture(void)
{
 const bb_home_place *p=bb_home_place_at(room.focus);
 return (bb_home_picture){.have_continue=have_games && cont_ok,.action=room_quit_on?E_QUIT:p->action};
}
#include "home-controller.inc"
static unsigned step;
static void key(u32 keys)
{
 int action=room_input(keys);
 printf("%u key=%u mode=%d focus=%d place=%d quit=%d selected=%d action=%d sound=%u\n",step++,keys,room.mode,room.focus,room.place,room_quit_on,sel_id,action,sound);
 if(room.mode==ROOM_GO)room.mode=ROOM_AT;
 if(room.mode==ROOM_BACK)room.mode=ROOM_HOME;
}
int main(int argc,char **argv)
{
 assert(argc==2);g_cfg.theme=theme_from_saved_id(argv[1]);
 for(int available=0;available<2;available++) {
  have_games=cont_ok=available;layout();room=(room_view){.file=&file,.mode=ROOM_HOME,.focus=ROOM_LIBRARY};room_quit_on=false;sel_id=E_LIBRARY;
  const u32 dirs[]={KEY_LEFT,KEY_RIGHT,KEY_UP,KEY_DOWN,KEY_L,KEY_R};
  for(int start=0;start<bb_home_count(available);start++)for(int d=0;d<6;d++) {
   const bb_home_place *p=bb_home_at(start,available);room.mode=ROOM_HOME;room.focus=p->place<0?ROOM_SHARE:p->place;room_quit_on=p->action==E_QUIT;sel_id=p->action;
   key(dirs[d]);key(KEY_A);key(KEY_B);
  }
  room.mode=ROOM_HOME;room_quit_on=false;room.focus=ROOM_LIBRARY;
  for(int i=0;i<bb_home_count(available);i++) {
   const bb_home_place *p=bb_home_at(i,available);room.mode=ROOM_HOME;
   if(theme_is_room()){tab_hit=i;touch=(touchPosition){320*i/bb_home_count(available)+1,220};}
   else {bb_home_picture h=home_list_picture();int r[4];list_look_home_bounds(&h,i,r);touch=(touchPosition){r[0]+r[2]/2,r[1]+r[3]/2};}
   /* The physical touch maps to this place; observations compare logical input. */
   int action=room_input(KEY_TOUCH);printf("%u touch=%d mode=%d focus=%d quit=%d action=%d sound=%u\n",step++,p->action,room.mode,room.focus,room_quit_on,action,sound);
   action=room_input(KEY_TOUCH);printf("%u touch=%d mode=%d focus=%d quit=%d action=%d sound=%u\n",step++,p->action,room.mode,room.focus,room_quit_on,action,sound);
   room.mode=ROOM_HOME;
  }
 }
 return 0;
}
