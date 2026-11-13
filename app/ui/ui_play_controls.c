/* Portable contact capture and retained-target invalidation. No system policy. */
#include "ui_play_controls.h"
#include <math.h>
#include "ui_style.h"
#include <string.h>
#define MENU_OWNER 6
#define STICK_OWNER 7
#define STATS_OWNER 8
static bool play_contains(ui_play_rect r,int x,int y)
{ return x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h; }
bool ui_play_visible(const ui_play_controls *p)
{
 if(p->layout.stick)return true;
 for(int i=0;i<UI_PLAY_SLOTS;i++)if(p->layout.slots[i].visible)return true;
 return false;
}
void ui_play_geometry(ui_play_layout *l)
{
 unsigned parts=0;
 for(int i=0;i<UI_PLAY_SLOTS;i++)if(l->slots[i].visible)parts|=1u<<i;
 for(int i=0;i<UI_PLAY_SLOTS;i++) {
  ui_play_rect b={UI_PLAY_CLUSTER_X+(i&1)*UI_PLAY_TILE_STEP,UI_PLAY_TOP+(i/2)*UI_PLAY_TILE_STEP,UI_PLAY_TILE_SIZE,UI_PLAY_TILE_SIZE,NULL};
  if(l->new_model && i<4) {
   b=(ui_play_rect){l->left?UI_PLAY_TOP:UI_PLAY_EDGE_X, i==2?UI_PLAY_TOP:UI_PLAY_SECOND_Y,UI_PLAY_EDGE_W,UI_PLAY_EDGE_H,NULL};
   if((parts&12)==4 || (parts&12)==8) {b.y=UI_PLAY_TOP;b.h=UI_PLAY_CLUSTER_SIZE;}
  } else if(i<4) {
   unsigned mask=parts&15;int count=__builtin_popcount(mask),rank=0;
   for(int k=0;k<4;k++)if((mask&(1u<<k)) && (((k&1)<(i&1)) || ((k&1)==(i&1)&&k<i)))rank++;
   if(l->stick) {
    bool column=count==2 && (mask==3 || mask==12);
    if(count==1) {b.x=UI_PLAY_CLUSTER_X;b.y=UI_PLAY_TOP;b.w=b.h=UI_PLAY_CLUSTER_SIZE;}
    else if(column) {b.y=UI_PLAY_TOP;b.h=UI_PLAY_CLUSTER_SIZE;}
    else if(!(mask&(1u<<(i^1)))) {b.x=UI_PLAY_CLUSTER_X;b.w=UI_PLAY_CLUSTER_SIZE;}
   } else if(count==1) {b.x=UI_PLAY_CLUSTER_X;b.y=UI_PLAY_TOP;b.w=b.h=UI_PLAY_CLUSTER_SIZE;}
   else if(count==2) {b.x=UI_PLAY_CLUSTER_X+UI_PLAY_TILE_STEP*rank;b.y=UI_PLAY_TOP;b.w=UI_PLAY_TILE_SIZE;b.h=UI_PLAY_CLUSTER_SIZE;}
   else if(count==3) {
    bool wide=(mask&3)==3?i>=2:i<2;
    b.x=UI_PLAY_CLUSTER_X+(wide?0:(i&1)*UI_PLAY_TILE_STEP);b.y=UI_PLAY_TOP+(i>=2?UI_PLAY_TILE_STEP:0);b.w=wide?UI_PLAY_CLUSTER_SIZE:UI_PLAY_TILE_SIZE;
   }
  } else {
   b=(ui_play_rect){l->left?UI_PLAY_RAIL_RIGHT:UI_PLAY_RAIL_LEFT,i==4?UI_PLAY_RAIL_TOP:UI_PLAY_RAIL_SECOND,UI_PLAY_RAIL_W,UI_PLAY_RAIL_H,NULL};
   if((parts&48)==16 || (parts&48)==32) {b.y=UI_PLAY_RAIL_TOP;b.h=UI_PLAY_RAIL_SINGLE_H;}
  }
  l->slots[i].rect=b;
 }
}
void ui_play_reset(ui_play_controls *p)
{ p->owner=p->active=-1;p->stick_x=p->stick_y=0;p->blocked=true;p->menu=p->stats=p->pressed=false;p->dirty=true; }
void ui_play_begin(ui_play_controls *p,const ui_play_layout *l)
{ p->layout=*l;p->ff_lit=false;ui_play_reset(p); }
static bool play_slot_contains(const ui_play_controls *p,int i,int x,int y)
{
 const ui_play_slot *s=&p->layout.slots[i];
 if(!s->visible || !play_contains(s->rect,x,y))return false;
 int dx=x-UI_PLAY_STICK_CENTER_X,dy=y-UI_PLAY_STICK_CENTER_Y;
 return !p->layout.stick || i>=4 || dx*dx+dy*dy>=UI_PLAY_STICK_HOLE_SQUARED;
}
void ui_play_poll(ui_play_controls *p,input_contact_t t)
{
 p->pressed=false;
 /* Idle is one flag check, with no layout/capability scans. */
 if(!t.held && !p->blocked && p->owner<0)return;
 int old=p->active,ox=p->stick_x,oy=p->stick_y;
 p->active=-1;p->stick_x=p->stick_y=0;
 if(!t.held) {p->owner=-1;p->blocked=false;}
 else if(!p->blocked) {
  if(t.down) {
   p->owner=-1;
   int edge=p->layout.left?UI_PLAY_STATS_MIRROR_X:UI_PLAY_STATS_X;
   bool stats=p->layout.fps_mode==2?t.y>=UI_PLAY_STATS_FOOTER_Y:p->layout.fps_mode==1 && play_contains((ui_play_rect){edge,UI_PLAY_STATS_Y,UI_PLAY_STATS_W,UI_PLAY_STATS_H,NULL},t.x,t.y);
   ui_play_rect menu=p->layout.fps_mode==2?(ui_play_rect){UI_PLAY_DETAIL_MENU_X,UI_PLAY_DETAIL_MENU_Y,UI_PLAY_DETAIL_MENU_W,UI_PLAY_DETAIL_MENU_H,NULL}:(ui_play_rect){p->layout.left?UI_PLAY_MENU_MIRROR_X:UI_PLAY_MENU_X,0,UI_PLAY_MENU_W,UI_PLAY_MENU_H,NULL};
   if(stats) {p->owner=STATS_OWNER;p->stats=true;}
   else if(play_contains(menu,t.x,t.y) || !ui_play_visible(p)) {p->owner=MENU_OWNER;p->menu=true;}
   else if(p->layout.fps_mode!=2) {
    for(int i=0;i<UI_PLAY_SLOTS;i++)if(play_slot_contains(p,i,t.x,t.y)) {p->owner=i;break;}
    int dx=t.x-UI_PLAY_STICK_CENTER_X,dy=t.y-UI_PLAY_STICK_CENTER_Y;
    if(p->layout.stick && dx*dx+dy*dy<=UI_PLAY_STICK_CAPTURE_SQUARED)p->owner=STICK_OWNER;
   }
  }
  if(p->owner>=0 && p->owner<UI_PLAY_SLOTS && play_slot_contains(p,p->owner,t.x,t.y))p->active=p->owner;
  if(p->owner==STICK_OWNER) {
   int x=t.x-UI_PLAY_STICK_CENTER_X,y=t.y-UI_PLAY_STICK_CENTER_Y,len2=x*x+y*y;
   if(len2>UI_PLAY_STICK_DEAD_SQUARED) {
    if(len2>UI_PLAY_STICK_RANGE_SQUARED) {float scale=UI_PLAY_STICK_RANGE/sqrtf(len2);x=(int)(x*scale);y=(int)(y*scale);}
    p->stick_x=x;p->stick_y=y;
   }
  }
 }
 p->pressed=t.down && p->active>=0;
 if(old!=p->active || ox!=p->stick_x || oy!=p->stick_y)p->dirty=true;
}
bool ui_play_take_menu(ui_play_controls *p) {bool v=p->menu;p->menu=false;return v;}
bool ui_play_take_stats(ui_play_controls *p) {bool v=p->stats;p->stats=false;return v;}
bool ui_play_redraw(ui_play_controls *p) {bool v=p->dirty;p->dirty=false;return v;}
void ui_play_speed(ui_play_controls *p,bool ff) {if(ff!=p->ff_lit){p->ff_lit=ff;p->dirty=true;}}
#ifndef UI_PLAY_HOST
#include "app_policy.h"
#include "theme.h"
#include "ui.h"
#include "libui.h"
#include "game_palette.h"
#include "components/play_controls.inc"
#endif
