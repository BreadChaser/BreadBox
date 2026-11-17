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

static C2D_SpriteSheet backdrop;
static flist_t games_list, systems_list;
static PSX3D_COLD void list_home_rect(const bb_home_picture *, int, float *, float *, float *, float *);
static PSX3D_COLD int home_count(const bb_home_picture *h) { return bb_home_count(h->have_continue); }
static PSX3D_COLD const bb_home_place *home_place(const bb_home_picture *h, int i) { return bb_home_at(i,h->have_continue); }

PSX3D_COLD void list_look_prepare(void)
{
 if(!backdrop)backdrop=C2D_SpriteSheetLoad("romfs:/gfx/psx3d_backdrop.t3x");
}
PSX3D_COLD void list_look_release(void)
{
 if(backdrop)C2D_SpriteSheetFree(backdrop);
 backdrop=NULL;
}
static PSX3D_COLD const char *row_label(int i, void *ud)
{
 const shelf_view *v=ud;
 if(i==0)return v->level==SHELF_ALL ? "Systems" : v->sort==SHELF_SORT_RECENT ? "Recently played"
  : v->sort==SHELF_SORT_MOST ? "Most played" : "All games";
 return v->level==SHELF_ALL ? v->rows[i-1].name : v->rows[v->row].box[i-1].title;
}
static PSX3D_COLD bool row_heading(int i, void *ud) { (void)ud;return i==0; }
static PSX3D_COLD const char *row_value(int i, void *ud)
{
 static char value[32];
 const shelf_view *v=ud;
 if(i==0)return "";
 i--;
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
PSX3D_COLD int list_look_library_hit(const shelf_view *v, int x, int y)
{
 if(v->level==SHELF_ONE && (v->pto==1 || v->pull>UI_LOOK_OUT_THRESHOLD))return shelf_scene_strip_hit(v,x,y);
 const flist_t *f=v->level==SHELF_ALL ? &systems_list : &games_list;
 if(y<UI_LIST_TOP || y>=BAR_TOP)return -1;
 if(list_notice(v) && y>=UI_LIST_TOP+UI_LIST_ROW*(UI_LIST_ROWS-1))return -1;
 int row=flist_row_at(f,UI_LIST_TOP,UI_LIST_ROW,y)-1;
 int n=v->level==SHELF_ALL ? v->nrows : v->rows[v->row].n;
 return row>=0 && row<n ? row : -1;
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

#include "components/list_look.inc"

#if defined(BB_UI_FIXTURES) || defined(BB_UI_TRACE_HOST)
PSX3D_COLD void list_look_library_targets(const shelf_view *v)
{
 const flist_t *f=v->level==SHELF_ALL?&systems_list:&games_list;
 int n=v->level==SHELF_ALL?v->nrows:v->rows[v->row].n;
 int rows=UI_LIST_ROWS-(list_notice(v)?1:0),first=(int)roundf(f->scroll);
 ui_trace_viewport(first,rows);
 for(int i=0;i<n;i++) {
  int y=UI_LIST_TOP+(int)roundf((i+1-f->scroll)*UI_LIST_ROW);
  if(y>=UI_LIST_TOP && y+UI_LIST_ROW<=UI_LIST_TOP+rows*UI_LIST_ROW)
   ui_trace_target(i,UI_LOOK_LIST_X,y,UI_LOOK_LIST_W,UI_LIST_ROW);
 }
}
PSX3D_COLD void list_look_home_bounds(const bb_home_picture *h,int i,int r[4])
{float x,y,w,height;list_home_rect(h,i,&x,&y,&w,&height);r[0]=x;r[1]=y;r[2]=w;r[3]=height;}
#endif
