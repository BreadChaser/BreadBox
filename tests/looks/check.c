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
void ui_trace_viewport(int first,int count){(void)first;(void)count;}
void ui_trace_target(int id,int x,int y,int w,int h){(void)id;(void)x;(void)y;(void)w;(void)h;}
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
   for(int y=0;y<UI_SCREEN_HEIGHT;y++)for(int x=0;x<320;x++){int hit=list_look_home_hit(&h,x,y);hit_record(hit);if(hit==p->action)found=true;}
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
 assert(drawn_rows==UI_LIST_ROWS-2 && saw(boxes[11].title) && saw(v.notice));
 assert(list_look_library_hit(&v,50,UI_LOOK_NOTICE_Y)==-1);
 v.t=UI_LOOK_NOTICE_TIME;ndrawn=0;list_look_library_bottom(&v,actions,4);
 assert(drawn_rows==UI_LIST_ROWS-1 && !saw(v.notice));v.notice=NULL;
 /* A to Z: the letters' own full-width row under "All games": all 27, G's cell goes to its first game, a letter
    without games goes nowhere and the row never touches the list; another order has no letters' row. */
 v.sort=SHELF_SORT_AZ;v.sel[0]=5;ndrawn=0;list_look_library_bottom(&v,actions,4);
 assert(saw("All games") && saw("#") && saw("A") && saw("G") && saw("Z") && drawn_rows==UI_LIST_ROWS-2);
 float cell=UI_LOOK_LIST_W/(float)BB_LETTERS;
 int letters_y=UI_LIST_TOP+UI_LIST_ROW+4;
 assert(list_look_library_hit(&v,(int)(UI_LOOK_LIST_X+cell*7.5f),letters_y)==0);
 assert(list_look_library_hit(&v,(int)(UI_LOOK_LIST_X+cell*1.5f),letters_y)==LETTER_STRIP_EMPTY);
 assert(list_look_library_hit(&v,50,UI_LIST_TOP+4)==LETTER_STRIP_EMPTY);
 assert(list_look_library_hit(&v,50,UI_LIST_TOP+UI_LIST_ROW*2+4)==0);
 v.sort=SHELF_SORT_RECENT;ndrawn=0;list_look_library_bottom(&v,actions,4);assert(!saw("G") && drawn_rows==UI_LIST_ROWS-1);
 assert(list_look_library_hit(&v,(int)(UI_LOOK_LIST_X+cell*7.5f),letters_y)==0);v.sort=SHELF_SORT_AZ;v.sel[0]=11;
 /* The list's own bar: A plays the chosen game and X is its Options; no box is taken out, so no game page. */
 assert(bb_library_actions(BB_LIBRARY_LIST,true,NULL,actions,BB_LIBRARY_ACTIONS)==BB_LIBRARY_ACTIONS);
 assert(actions[0].key==KEY_B && !strcmp(actions[0].label,"Back") && actions[1].key==KEY_Y && actions[1].on);
 assert(actions[2].key==KEY_X && !strcmp(actions[2].label,"Options") && actions[3].key==KEY_A && !strcmp(actions[3].label,"Play"));
 assert(!bb_library_actions(BB_LIBRARY_LIST+1,true,NULL,actions,BB_LIBRARY_ACTIONS));
 assert(list_look_library_in_rows(&v,UI_LIST_TOP+UI_LIST_ROW*2+4) && !list_look_library_in_rows(&v,letters_y) && !list_look_library_in_rows(&v,UI_LIST_TOP+4));
 ndrawn=0;list_look_library_bottom(&v,actions,4);assert(saw("Play") && saw("Options") && saw(boxes[11].title));
 v.level=SHELF_ALL;row.n=1;v.sel[0]=0;ndrawn=0;list_look_library_top(&v);assert(saw("1 game"));
 row.n=0;ndrawn=0;list_look_library_top(&v);assert(saw("0 games"));
}
static void geometry_checks(void)
{
 static float expected[8192][5];int count=0;unsigned expected_hits=0;
 scene_refused=true;   /* both on the plain page: PSX3D's scene is its own picture, the screens over it are compared */
 for(int look=BB_LOOK_PSX3D;look<=BB_LOOK_SIMPLE;look++) {
  theme_select_look(look);geometry_n=0;geometry_on=true;hit_hash=2166136261u;
  home_checks();library_checks();geometry_on=false;
  if(look==BB_LOOK_PSX3D){count=geometry_n;expected_hits=hit_hash;memcpy(expected,geometry,sizeof(expected));}
  else {assert(expected_hits==hit_hash);assert(count==geometry_n);for(int i=0;i<count;i++)for(int k=0;k<5;k++)if(expected[i][k]!=geometry[i][k]){fprintf(stderr,"rectangle %d coordinate %d differs\n",i,k);assert(0);}}
 }
 printf("PASS: identical PSX3D/Simple geometry (%d rectangles), rows, tiles, footer slots and every logical touch target\n",count);
}
/* The list looks' home: the D-pad goes to the tile that stands that way (up, down, left, right), nothing past an edge. */
static void home_move_checks(void)
{
 enum { N=-1 };
 static const struct { bool cont; int from, to[4]; } map[]={
  {true,E_CONTINUE,{N,E_LIBRARY,N,N}},{true,E_LIBRARY,{E_CONTINUE,E_TOGETHER,N,N}},
  {true,E_TOGETHER,{E_LIBRARY,E_SHARE,N,E_SETTINGS}},{true,E_SETTINGS,{E_LIBRARY,E_QUIT,E_TOGETHER,N}},
  {true,E_SHARE,{E_TOGETHER,N,N,E_QUIT}},{true,E_QUIT,{E_SETTINGS,N,E_SHARE,N}},
  {false,E_LIBRARY,{N,E_TOGETHER,N,N}},{false,E_TOGETHER,{E_LIBRARY,E_SHARE,N,E_SETTINGS}},
  {false,E_SETTINGS,{E_LIBRARY,E_QUIT,E_TOGETHER,N}},{false,E_SHARE,{E_TOGETHER,N,N,E_QUIT}},
  {false,E_QUIT,{E_SETTINGS,N,E_SHARE,N}},
 };
 for(int look=BB_LOOK_PSX3D;look<=BB_LOOK_SIMPLE;look++) {
  theme_select_look(look);
  for(unsigned i=0;i<sizeof(map)/sizeof(*map);i++)for(int d=LIST_UP;d<=LIST_RIGHT;d++) {
   bb_home_picture h={.have_continue=map[i].cont,.action=map[i].from};
   assert(list_look_home_move(&h,d)==map[i].to[d]);
   if(map[i].to[d]<0)continue;
   /* the move goes the way it is pressed on screen */
   int a[4],b[4];
   for(int k=0;k<bb_home_count(map[i].cont);k++) {
    if(bb_home_at(k,map[i].cont)->action==map[i].from)list_look_home_bounds(&h,k,a);
    if(bb_home_at(k,map[i].cont)->action==map[i].to[d])list_look_home_bounds(&h,k,b);
   }
   if(d==LIST_UP)assert(b[1]+b[3]<=a[1]);
   if(d==LIST_DOWN)assert(b[1]>=a[1]+a[3]);
   if(d==LIST_LEFT)assert(b[0]+b[2]<=a[0] && b[1]==a[1]);
   if(d==LIST_RIGHT)assert(b[0]>=a[0]+a[2] && b[1]==a[1]);
  }
 }
 puts("PASS: list looks' home: every D-pad move lands on the tile that way on screen, none past an edge");
}
/* PSX3D's top screen is its moving scene; Simple, both bottom screens and a scene without memory get the plain page. */
static void backdrop_checks(void)
{
 theme_select_look(BB_LOOK_PSX3D);
 int before=scene_draws;list_look_backdrop(false);assert(scene_draws==before+1);
 list_look_backdrop(true);assert(scene_draws==before+1);
 geometry_n=0;geometry_on=true;scene_refused=true;list_look_backdrop(false);assert(scene_draws==before+2);scene_refused=false;
 geometry_on=false;assert(geometry_n==1 && geometry[0][0]==0 && geometry[0][1]==0 && geometry[0][3]==UI_TOP_SCREEN_WIDTH && geometry[0][4]==UI_SCREEN_HEIGHT);   /* the plain page, whole screen */
 theme_select_look(BB_LOOK_SIMPLE);list_look_backdrop(false);list_look_backdrop(true);assert(scene_draws==before+2);
 int released=scene_releases;list_look_release();assert(scene_releases==released+1);
 puts("PASS: PSX3D draws its moving scene on top only, falls back to the plain page, and releases it with the menu");
}
/* PSX3D shows its own pictures of Together, Settings and Share (PSX3D v0.5.2's); Simple the shared icons. */
static void picture_checks(void)
{
 bb_home_picture h={.have_continue=false,.title="Game",.continue_title="Game"};
 theme_select_look(BB_LOOK_PSX3D);place_pictures=0;list_look_home_bottom(&h);assert(place_pictures==3);
 place_pictures=0;h.action=E_SETTINGS;list_look_home_top(&h);assert(place_pictures==1);
 place_pictures=0;h.action=E_QUIT;list_look_home_top(&h);assert(place_pictures==0);
 theme_select_look(BB_LOOK_SIMPLE);place_pictures=0;h.action=E_SETTINGS;list_look_home_bottom(&h);list_look_home_top(&h);assert(place_pictures==0);
 puts("PASS: PSX3D draws its own place pictures, Simple the shared icons");
}
int main(void)
{
 backdrop_checks();picture_checks();
 home_move_checks();
 geometry_checks();scene_refused=false;
 for(int look=BB_LOOK_PSX3D;look<BB_LOOK_COUNT;look++) {
  theme_select_look(look);home_checks();library_checks();
  printf("PASS: %s draws and selects every shared place, game, group and action\n",theme_look_get(look)->name);
 }
}
