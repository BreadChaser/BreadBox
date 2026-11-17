#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <citro2d.h>
#include "list_look.h"
#include "theme.h"
#include "ui.h"
#include "ui_style.h"
#include "libui.h"
#include "look_style.h"
#include "covers.h"
#include "scene.h"
#include "ui_format.h"
#include "cold_code.h"
#include "ui_trace.h"
#include "letter_strip.h"
#include "list_scene.h"
#include "icons.h"

static flist_t games_list, systems_list;
static PSX3D_COLD void list_home_rect(const bb_home_picture *, int, float *, float *, float *, float *);
static PSX3D_COLD int home_count(const bb_home_picture *h) { return bb_home_count(h->have_continue); }
static PSX3D_COLD const bb_home_place *home_place(const bb_home_picture *h, int i) { return bb_home_at(i,h->have_continue); }

/* PSX3D's own pictures of the places (as in PSX3D v0.5.2); -1: the place has none, its shared icon is drawn. */
static PSX3D_COLD int place_picture(const bb_home_place *p)
{
 if(!theme_look_get(theme_look())->artwork)return -1;
 return p->icon==UI_ICON_TOGETHER ? ICON_TOGETHER : p->icon==UI_ICON_SCOPE ? ICON_SETTINGS : p->icon==UI_ICON_QR ? ICON_SHARE : -1;
}
PSX3D_COLD void list_look_release(void) { list_scene_release(); }   /* before a game: the scene's textures and tables */
/* The list's section name stays in the first row; the systems or games scroll under it. */
static PSX3D_COLD const char *section_label(const shelf_view *v)
{
 return v->level==SHELF_ALL ? "Systems" : v->sort==SHELF_SORT_RECENT ? "Recently played"
  : v->sort==SHELF_SORT_MOST ? "Most played" : "All games";
}
static PSX3D_COLD const char *row_label(int i, void *ud)
{
 const shelf_view *v=ud;
 return v->level==SHELF_ALL ? v->rows[i].name : v->rows[v->row].box[i].title;
}
static PSX3D_COLD const char *row_value(int i, void *ud)
{
 static char value[32];
 const shelf_view *v=ud;
 if(v->level==SHELF_ALL)ui_format_games(value,sizeof(value),v->rows[i].n);
 else {
  const shelf_box *b=&v->rows[v->row].box[i];
  if(b->discs>1)snprintf(value,sizeof(value),"%s%u  >",b->fav ? "★ " : "",b->discs);
  else snprintf(value,sizeof(value),"%s",b->fav ? "★" : "");
 }
 return value;
}
static PSX3D_COLD const char *list_key(unsigned key)
{
 switch(key) {
 case KEY_A:return "\xee\x80\x80";
 case KEY_B:return "\xee\x80\x81";
 case KEY_X:return "\xee\x80\x82";
 case KEY_Y:return "\xee\x80\x83";
 case KEY_L:return UI_GLYPH_L;
 case KEY_R:return UI_GLYPH_R;
 case KEY_SELECT:return "Select";
 case KEY_START:return "Start";
 default:return "";
 }
}
static PSX3D_COLD void played_line(const shelf_box *b, char *out, size_t cap)
{
 if(!b || !b->last){snprintf(out,cap,"Not played yet");return;}
 time_t when=b->last;
 char date[48];
 ui_format_when(date,sizeof(date),when);
 if(b->seconds>=3600)snprintf(out,cap,"Played %s · %luh %lum",date,(unsigned long)b->seconds/3600,(unsigned long)b->seconds/60%60);
 else snprintf(out,cap,"Played %s · %lum",date,(unsigned long)b->seconds/60);
}
static PSX3D_COLD const shelf_box *focus_game(const shelf_view *v)
{
 int r=v->level==SHELF_ALL ? v->hsel : v->row;
 if(r<0 || r>=v->nrows || !v->rows[r].n)return NULL;
 int i=v->sel[r];
 return &v->rows[r].box[i>=0 && i<v->rows[r].n ? i : 0];
}
static PSX3D_COLD bool list_notice(const shelf_view *v)
{
 return v->notice && v->t-v->notice_t<UI_LOOK_NOTICE_TIME;
}
static PSX3D_COLD void list_scroll(flist_t *list,int selected,int count,int visible)
{
 flist_update(list,selected,count,visible);
 /* A temporary notice uses one row; keep the focused item above it. */
 int first=selected-visible+1;
 if(first>list->scroll_target){list->scroll=list->scroll_target=first;list->scroll_v=0;}
}
/* The letter strip of an A-Z game list, beside the section name. */
static PSX3D_COLD bool list_letters(const shelf_view *v, letter_strip_place *at)
{
 const theme_t *t=theme();
 *at=(letter_strip_place){UI_LOOK_LIST_X,UI_LIST_TOP+UI_LIST_ROW,UI_LOOK_LIST_W,UI_LIST_ROW,UI_LIST_FOCUS_Z,
  UI_LIST_TOP+UI_LIST_ROW+(UI_LIST_ROW-UI_LOOK_TEXT_EM*UI_LIST_TEXT)/UI_LOOK_TWO,UI_LIST_TEXT,t->text_bright,
  theme_alpha(t->text_muted,UI_LETTERS_DIM_ALPHA),true,BB_LETTERS,ui_text_ex};
 return v->level==SHELF_ONE && v->sort==SHELF_SORT_AZ && v->rows[v->row].n>0;
}
PSX3D_COLD int list_look_library_hit(const shelf_view *v, int x, int y)
{
 const flist_t *f=v->level==SHELF_ALL ? &systems_list : &games_list;
 letter_strip_place at;
 bool letters=list_letters(v,&at);
 int top=UI_LIST_TOP+UI_LIST_ROW*(UI_LOOK_ONE+letters);
 if(y>=UI_LIST_TOP && y<top) {   /* the section row and the letters' row: a letter, never the list */
  int hit=letters ? letter_strip_hit(&v->rows[v->row],v->sel[v->row],&at,x,y) : -1;
  return hit==-1 ? LETTER_STRIP_EMPTY : hit;
 }
 if(y<UI_LIST_TOP || y>=BAR_TOP)return -1;
 if(list_notice(v) && y>=UI_LIST_TOP+UI_LIST_ROW*(UI_LIST_ROWS-1))return -1;
 int row=flist_row_at(f,top,UI_LIST_ROW,y);
 int n=v->level==SHELF_ALL ? v->nrows : v->rows[v->row].n;
 return row>=0 && row<n ? row : -1;
}
/* A touch at y is on the rows themselves (not the section name or the letters): a tap on the chosen row acts. */
PSX3D_COLD bool list_look_library_in_rows(const shelf_view *v, int y)
{
 letter_strip_place at;
 return y>=UI_LIST_TOP+UI_LIST_ROW*(UI_LOOK_ONE+list_letters(v,&at)) && y<BAR_TOP;
}
PSX3D_COLD int list_look_home_hit(const bb_home_picture *h, int x, int y)
{
 int n=home_count(h);
 for(int i=0;i<n;i++) {
  float xx,yy,ww,hh;
  list_home_rect(h,i,&xx,&yy,&ww,&hh);
  if(x>=xx && x<xx+ww && y>=yy && y<yy+hh)return home_place(h,i)->action;
 }
 return -1;
}
/* The D-pad moves between the tiles as they stand (PSX3D v0.5.2): up / down to the nearest tile in another
   row, left / right within the row. Returns the place's action there, -1 at an edge. */
PSX3D_COLD int list_look_home_move(const bb_home_picture *h, int direction)
{
 int n=home_count(h),current=-1,best=-1;
 for(int i=0;i<n;i++)if(home_place(h,i)->action==h->action)current=i;
 if(current<0)return -1;
 float cx,cy,cw,ch,best_d=UI_LOOK_FAR;
 list_home_rect(h,current,&cx,&cy,&cw,&ch);
 for(int i=0;i<n;i++) {
  float x,y,w,height;
  if(i==current)continue;
  list_home_rect(h,i,&x,&y,&w,&height);
  float dx=x+w/2-(cx+cw/2),dy=y+height/2-(cy+ch/2);
  bool row=y<cy+ch && cy<y+height;
  bool ok=direction==LIST_UP ? dy<0 && !row : direction==LIST_DOWN ? dy>0 && !row : direction==LIST_LEFT ? dx<0 && row : dx>0 && row;
  float d=direction<=LIST_DOWN ? fabsf(dy)*UI_LOOK_ROW_WEIGHT+fabsf(dx) : fabsf(dx);
  if(ok && d<best_d){best_d=d;best=i;}
 }
 return best<0 ? -1 : home_place(h,best)->action;
}

#include "components/list_look.inc"

#if defined(BB_UI_FIXTURES) || defined(BB_UI_TRACE_HOST)
PSX3D_COLD void list_look_library_targets(const shelf_view *v)
{
 const flist_t *f=v->level==SHELF_ALL?&systems_list:&games_list;
 int n=v->level==SHELF_ALL?v->nrows:v->rows[v->row].n;
 letter_strip_place at;
 int letters=list_letters(v,&at);
 int rows=UI_LIST_ROWS-UI_LOOK_ONE-letters-(list_notice(v)?1:0),first=(int)roundf(f->scroll),top=UI_LIST_TOP+UI_LIST_ROW*(UI_LOOK_ONE+letters);
 ui_trace_viewport(first,rows);
 for(int i=0;i<n;i++) {
  int y=top+(int)roundf((i-f->scroll)*UI_LIST_ROW);
  if(y>=top && y+UI_LIST_ROW<=top+rows*UI_LIST_ROW)
   ui_trace_target(i,UI_LOOK_LIST_X,y,UI_LOOK_LIST_W,UI_LIST_ROW);
 }
}
PSX3D_COLD void list_look_home_bounds(const bb_home_picture *h,int i,int r[4])
{float x,y,w,height;list_home_rect(h,i,&x,&y,&w,&height);r[0]=x;r[1]=y;r[2]=w;r[3]=height;}
#endif
