#include "ui_terms.h"
#include "ui_trace_views.h"
#if defined(BB_UI_FIXTURES) || defined(BB_UI_TRACE_HOST)
#include <stdio.h>
#include <math.h>
#include "theme.h"
#include "ui_trace_postcards.h"
#include "cold_code.h"
PSX3D_COLD void ui_trace_home(const bb_home_picture *h,const ui_button_t *actions,int count)
{
 int n=bb_home_count(h->have_continue),focused=0;
 for(int i=0;i<n;i++)if(bb_home_at(i,h->have_continue)->action==h->action)focused=i;
 ui_trace_begin(h->at_place?h->title:"Home",n,focused,0);
 for(int i=0;i<n;i++) {
  const bb_home_place *p=bb_home_at(i,h->have_continue);ui_trace_row(i,p->name,"",0);
  if(h->at_place)continue;
  if(theme_is_room())ui_trace_target(i,320*i/n,BAR_TOP,320*(i+1)/n-320*i/n,240-BAR_TOP);
  else {int r[4];list_look_home_bounds(h,i,r);ui_trace_target(i,r[0],r[1],r[2],r[3]);}
 }
 if(h->at_place){for(int i=0;actions && i<count;i++)ui_trace_button(actions[i].key,actions[i].label,320*i/count,BAR_TOP,320/count,240-BAR_TOP);}
 else ui_trace_button(KEY_A,"Open",0,BAR_TOP,320,240-BAR_TOP);
}
PSX3D_COLD void ui_trace_library(const shelf_view *v,const ui_button_t *actions,int count)
{
 int n=v->level==SHELF_ALL?v->nrows:v->rows[v->row].n;
 int selected=v->level==SHELF_ALL?v->hsel:v->sel[v->row];
 ui_trace_begin(v->level==SHELF_ALL?"Systems":v->pto?"Taken out":"Games",n,selected,(int)roundf(v->level==SHELF_ALL?v->hcur:v->cur));
 for(int i=0;i<n;i++) {
  if(v->level==SHELF_ALL){char text[24];snprintf(text,sizeof(text),"%d",v->rows[i].n);ui_trace_row(i,v->rows[i].name,text,0);}
  else {const shelf_box *b=&v->rows[v->row].box[i];char value[64];snprintf(value,sizeof(value),"%d/%d/%d",v->sort,b->fav,b->discs);ui_trace_row(i,b->title,value,0);}
 }
 if(theme_is_room())shelf_scene_library_targets(v);else list_look_library_targets(v);
 for(int i=0;i<count;i++)ui_trace_button(actions[i].key,actions[i].label,320*i/count,BAR_TOP,320/count,240-BAR_TOP);
}
PSX3D_COLD void ui_trace_postcards(const pc_view *v,void (*bounds)(const pc_view *,int,float *,float *,float *,float *,float *))
{
 ui_trace_begin(v->saving?"Save postcard":"Load postcard",PC_SLOTS+v->has_continue,v->sel,0);
 for(int i=0;i<PC_CARDS;i++)if(i<PC_SLOTS || v->has_continue) {
  char name[24],value[64];snprintf(name,sizeof(name),"%d",i);
  snprintf(value,sizeof(value),"%d/%d/%d",v->card[i].used,v->confirm_replace,v->confirm_delete);ui_trace_row(i,name,value,0);
  float x,y,w,h,tilt;bounds(v,i,&x,&y,&w,&h,&tilt);ui_trace_target(i,x-w/2,y-h/2,w,h);
 }
}
#endif
