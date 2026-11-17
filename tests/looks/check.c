// Execute both list palettes through the production composition and hit testing.
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <citro2d.h>
#include "app_config.h"
#include "ui.h"
#include "ui_list.h"
#include "ui_style.h"
#include "paint.h"
psx3d_config g_cfg;
#include "../../app/ui/theme.c"
#include "../../app/ui/look_theme.c"
#include "../../app/ui/home_model.c"
#include "../../app/ui/list_look.c"
#include "../../app/ui/library_actions.c"
static unsigned hit_hash;
static void hit_record(int action){hit_hash=(hit_hash^(unsigned)(action+1))*16777619u;}
static void home_checks(void)
{
 for(int available=0;available<2;available++) {
  bb_home_picture h={.have_continue=available,.title="Game",.continue_title="Game"};
  int n=bb_home_count(available);ndrawn=0;list_look_home_bottom(&h);
  for(int i=0;i<n;i++) {
   const bb_home_place *p=bb_home_at(i,available);ndrawn=0;list_look_home_bottom(&h);assert(saw(p->name));
   if(p->place>=0) {
    int sequence[ROOM_PLACES],count=bb_home_room_places(sequence,ROOM_PLACES,available),at=0;
    while(at<count && sequence[at]!=p->place)at++;
    assert(at<count);
    assert(bb_home_neighbor(p->place,1,available)==(at+1<count?sequence[at+1]:-1));
    assert(bb_home_neighbor(p->place,-1,available)==(at?sequence[at-1]:-1));
   }
   bool found=false;
   for(int y=0;y<BAR_TOP;y++)for(int x=0;x<320;x++){int hit=list_look_home_hit(&h,x,y);hit_record(hit);if(hit==p->action)found=true;}
   assert(found);
   h.action=p->action;ndrawn=0;check_top=true;list_look_home_top(&h);check_top=false;assert(saw(h.title));
  }
 }
}
static void library_checks(void)
{
 shelf_box boxes[12]={0};shelf_row row={.name="PlayStation",.n=12,.box=boxes};
 for(int i=0;i<12;i++){snprintf(boxes[i].title,sizeof(boxes[i].title),"Game %d",i);boxes[i].cover=100+i;}
 boxes[8].discs=3;boxes[8].fav=true;
 shelf_view v={.rows=&row,.nrows=1};
 ui_button_t actions[BB_LIBRARY_ACTIONS];
 assert(bb_library_actions(BB_LIBRARY_SYSTEMS,false,NULL,actions,BB_LIBRARY_ACTIONS)==BB_LIBRARY_ACTIONS);
 ndrawn=0;list_look_library_bottom(&v,actions,4);assert(saw(row.name));
 v.level=SHELF_ONE;
 assert(bb_library_actions(BB_LIBRARY_GAMES,true,NULL,actions,BB_LIBRARY_ACTIONS)==BB_LIBRARY_ACTIONS);
 assert(!strcmp(actions[0].label,"Back"));
 for(int sort=0;sort<SHELF_SORTS;sort++)for(int selected=0;selected<12;selected++) {
  v.sort=sort;v.sel[0]=selected;ndrawn=0;list_look_library_bottom(&v,actions,4);
  assert(saw(boxes[selected].title));
  ncover_ids=0;check_top=true;list_look_library_top(&v);check_top=false;
  int at=0;
  assert(cover_ids[at++]==100+selected);
  if(selected)assert(cover_ids[at++]==99+selected);
  if(selected<11)assert(cover_ids[at++]==101+selected);
  assert(cover_ids[at++]==100+selected && ncover_ids==at);
  for(int i=0;i<4;i++)assert(saw(actions[i].label));
  if(selected==8)assert(saw("★ 3  >"));
  bool found=false;for(int y=UI_LIST_TOP;y<BAR_TOP;y++){int hit=list_look_library_hit(&v,50,y);hit_record(hit);if(hit==selected)found=true;}
  assert(found);
 }
 v.notice="Most played first";ndrawn=0;list_look_library_bottom(&v,actions,4);
 assert(drawn_rows==UI_LIST_ROWS-1 && saw(boxes[11].title) && saw(v.notice));
 assert(list_look_library_hit(&v,50,UI_LOOK_NOTICE_Y)==-1);
 v.t=UI_LOOK_NOTICE_TIME;ndrawn=0;list_look_library_bottom(&v,actions,4);
 assert(drawn_rows==UI_LIST_ROWS && !saw(v.notice));v.notice=NULL;
 v.pto=1;
 assert(list_look_library_hit(&v,160,100)==-1);
 assert(list_look_library_hit(&v,(int)shelf_scene_strip_position(&row,boxes[8].x),10)>=0);
 assert(bb_library_actions(BB_LIBRARY_HELD,true,NULL,actions,BB_LIBRARY_ACTIONS)==BB_LIBRARY_ACTIONS);
 ndrawn=0;list_look_library_bottom(&v,actions,4);assert(saw(boxes[11].title));
 v.level=SHELF_ALL;row.n=1;v.sel[0]=0;ndrawn=0;list_look_library_top(&v);assert(saw("1 game"));
 row.n=0;ndrawn=0;list_look_library_top(&v);assert(saw("0 games"));
}
static void geometry_checks(void)
{
 static float expected[8192][5];int count=0;unsigned expected_hits=0;
 for(int look=BB_LOOK_PSX3D;look<=BB_LOOK_SIMPLE;look++) {
  theme_select_look(look);geometry_n=0;geometry_on=true;hit_hash=2166136261u;
  home_checks();library_checks();geometry_on=false;
  if(look==BB_LOOK_PSX3D){count=geometry_n;expected_hits=hit_hash;memcpy(expected,geometry,sizeof(expected));}
  else {assert(expected_hits==hit_hash);assert(count==geometry_n);for(int i=0;i<count;i++)for(int k=0;k<5;k++)if(expected[i][k]!=geometry[i][k]){fprintf(stderr,"rectangle %d coordinate %d differs\n",i,k);assert(0);}}
 }
 printf("PASS: identical PSX3D/Simple geometry (%d rectangles), rows, tiles, footer slots and every logical touch target\n",count);
}
static const char *edge_label(int i,void *ud)
{return i==*(int *)ud?"Focused edge row":"Other row";}
static void scroll_edge_checks(void)
{
 for(int look=BB_LOOK_PSX3D;look<=BB_LOOK_SIMPLE;look++) {
  theme_select_look(look);
  for(int edge=0;edge<2;edge++) {
   int first=5,selected=edge?first:first+UI_LIST_ROWS-1;
   flist_t f={.scroll=nextafterf(first,edge?INFINITY:-INFINITY)};
   ndrawn=0;
   production_look_list_draw(&f,first+UI_LIST_ROWS,selected,UI_LOOK_LIST_X,UI_LIST_TOP,
    UI_LOOK_LIST_W,UI_LIST_ROW,UI_LIST_ROWS,edge_label,NULL,&selected,NULL);
   assert(saw("Focused edge row"));
  }
 }
 puts("PASS: fractional scroll settling keeps both focused edge rows visible in both list looks");
}
int main(void)
{
 geometry_checks();
 scroll_edge_checks();
 for(int look=BB_LOOK_PSX3D;look<BB_LOOK_COUNT;look++) {
  theme_select_look(look);home_checks();library_checks();
  printf("PASS: %s draws and selects every shared place, game, group and action\n",theme_look_get(look)->name);
 }
}
