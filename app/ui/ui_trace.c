#include "ui_trace.h"
#if defined(BB_UI_FIXTURES) || defined(BB_UI_TRACE_HOST)
#include <stdio.h>
#include <string.h>
#include "cold_code.h"
#include "ui_trace_card.h"
#include "ui.h"
#include <math.h>
static struct {
 char screen[96],label[128],value[160],buttons[768],targets[1024];
 int count,focus,top,nb,nt,first,visible;uint32_t rows,sounds,requests;
} trace_state __attribute__((section(".bss.ui_trace")));
static char json[6144] __attribute__((section(".bss.ui_trace")));
static PSX3D_COLD uint32_t trace_hash(uint32_t h,const char *s)
{for(;s && *s;s++)h=(h^(unsigned char)*s)*16777619u;return (h^255)*16777619u;}
static PSX3D_COLD void trace_quote(char *out,size_t cap,const char *s)
{
 size_t at=0;for(;s && *s && at+7<cap;s++) {
  unsigned char c=*s;
  if(c=='"' || c=='\\'){out[at++]='\\';out[at++]=c;}
  else if(c<32)at+=(size_t)snprintf(out+at,cap-at,"\\u%04x",c);
  else out[at++]=c;
 }
 out[at]=0;
}
PSX3D_COLD void ui_trace_begin(const char *screen,int count,int focus,int top)
{
 snprintf(trace_state.screen,sizeof(trace_state.screen),"%s",screen?screen:"");
 trace_state.first=0;trace_state.visible=count;trace_state.count=count;trace_state.focus=focus;trace_state.top=top;trace_state.rows=2166136261u;
 trace_state.label[0]=trace_state.value[0]=trace_state.buttons[0]=trace_state.targets[0]=0;trace_state.nb=trace_state.nt=0;
}
PSX3D_COLD void ui_trace_row(int i,const char *label,const char *value,int heading)
{
 trace_state.rows=trace_hash(trace_hash(trace_state.rows,label),label && !strcmp(label,"Look")?"Appearance":value);trace_state.rows=(trace_state.rows^(unsigned)heading)*16777619u;
 if(i==trace_state.focus){snprintf(trace_state.label,sizeof(trace_state.label),"%s",label?label:"");snprintf(trace_state.value,sizeof(trace_state.value),"%s",value?value:"");}
}
PSX3D_COLD void ui_trace_target(int id,int x,int y,int w,int h)
{
 size_t at=strlen(trace_state.targets);if(at+96>=sizeof(trace_state.targets))return;
 snprintf(trace_state.targets+at,sizeof(trace_state.targets)-at,"%s[%d,%d,%d,%d,%d]",trace_state.nt++?",":"",id,x,y,w,h);
}
PSX3D_COLD void ui_trace_button(uint32_t key,const char *label,int x,int y,int w,int h)
{
 char escaped[128];trace_quote(escaped,sizeof(escaped),label);
 size_t at=strlen(trace_state.buttons);if(at+180>=sizeof(trace_state.buttons))return;
 snprintf(trace_state.buttons+at,sizeof(trace_state.buttons)-at,"%s[%u,\"%s\"]",trace_state.nb++?",":"",(unsigned)key,escaped);
 ui_trace_target(-(int)key,x,y,w,h);
}
PSX3D_COLD void ui_trace_reset(void){trace_state.sounds=trace_state.requests=0;}
PSX3D_COLD void ui_trace_viewport(int first,int count){trace_state.first=first;trace_state.visible=count;}
PSX3D_COLD void ui_trace_sound(int event)
{trace_state.sounds=(trace_state.sounds^(uint32_t)(event+1))*16777619u;}
PSX3D_COLD void ui_trace_request(int event)
{trace_state.requests=(trace_state.requests^(uint32_t)(event+1))*16777619u;}
PSX3D_COLD const char *ui_trace_json(void)
{
 char screen[576],label[768],value[960];trace_quote(screen,sizeof(screen),trace_state.screen);trace_quote(label,sizeof(label),trace_state.label);trace_quote(value,sizeof(value),trace_state.value);
 snprintf(json,sizeof(json),"{\"screen\":\"%s\",\"count\":%d,\"focus\":%d,\"top\":%d,\"label\":\"%s\",\"value\":\"%s\",\"rows\":%u,\"sounds\":%u,\"requests\":%u,\"viewport\":[%d,%d],\"buttons\":[%s],\"targets\":[%s]}",screen,trace_state.count,trace_state.focus,trace_state.top,label,value,(unsigned)trace_state.rows,(unsigned)trace_state.sounds,(unsigned)trace_state.requests,trace_state.first,trace_state.visible,trace_state.buttons,trace_state.targets);
 return json;
}
#endif

#if defined(BB_UI_FIXTURES) || defined(BB_UI_TRACE_HOST)
PSX3D_COLD void ui_trace_browser(const flist_t *list,int entries,int focus,const char *(*label)(int,void *),const char *(*value)(int,void *))
{
 ui_trace_begin("Folders",entries+1,focus,(int)roundf(list->scroll_target));
 for(int i=0;i<=entries;i++) {
  ui_trace_row(i,label(i,NULL),value(i,NULL),0);
  int y=UI_LIST_TOP+(int)roundf((i-list->scroll)*UI_LIST_ROW);
  if(y>=UI_LIST_TOP && y+UI_LIST_ROW<=BAR_TOP)ui_trace_target(i,0,y,320,UI_LIST_ROW);
 }
}
PSX3D_COLD void ui_trace_choices(const char *title,const ui_button_t *tabs,int count,int focus)
{ui_trace_begin(title,count,focus,0);for(int i=0;i<count;i++)ui_trace_row(i,tabs[i].label,"",0);}
PSX3D_COLD void ui_trace_tab(const ui_button_t *b,int index,float x,float w)
{
 if(b->key)ui_trace_button(b->key,b->label,(int)x,BAR_TOP,(int)w,UI_SCREEN_HEIGHT-BAR_TOP);
 else ui_trace_target(index,(int)x,BAR_TOP,(int)w,UI_SCREEN_HEIGHT-BAR_TOP);
}
PSX3D_COLD void ui_trace_card(const flist_t *f,int n,int sel,void (*row)(int,void *,ui_card_row_t *),void *ud,const ui_card_t *card)
{
 ui_trace_begin(card->title?card->title:card->eyebrow,n,sel,(int)roundf(f->scroll_target));
 for(int i=0;i<n;i++){ui_card_row_t r={0};row(i,ud,&r);ui_trace_row(i,r.label,r.value,r.heading);}
 for(int i=(int)roundf(f->scroll);i<n && i<(int)roundf(f->scroll)+UI_CARD_ROWS;i++)
  ui_trace_target(i,UI_CARD_X,UI_CARD_ROWS_Y+(i-(int)roundf(f->scroll))*UI_CARD_ROW,UI_CARD_W,UI_CARD_ROW);
}
#endif

#if defined(BB_UI_FIXTURES) || defined(BB_UI_TRACE_HOST)
PSX3D_COLD void ui_trace_continue(const char *title,bool failed)
{ui_trace_begin("Continue?",1,0,0);ui_trace_row(0,title,failed?"Start over failed":"",0);}
PSX3D_COLD void ui_trace_qr(const char *label)
{ui_trace_begin("QR code",1,0,0);ui_trace_row(0,label,"",0);}
PSX3D_COLD void ui_trace_dialog(const char *title,const char *text,int page)
{ui_trace_begin(title,ui_dialog_pages(text),page,page);ui_trace_row(page,text,"",0);}
PSX3D_COLD void ui_trace_pick(const char *title,int selected,const char *yes,const char *no)
{ui_trace_begin(title,2,selected,0);ui_trace_row(0,yes,"",0);ui_trace_row(1,no,"",0);}
#endif

#if defined(BB_UI_FIXTURES) || defined(BB_UI_TRACE_HOST)
PSX3D_COLD void ui_trace_picker(const flist_t *list,int count,int selected,bool look,const char *(*name)(int,void *))
{
 ui_trace_begin(look?"Look":"Room style",count,selected,(int)list->scroll_target);
 for(int i=0;i<count;i++){ui_trace_row(i,name(i,look?(void *)list:NULL),"",0);
  int y=UI_LIST_TOP+(int)roundf((i-list->scroll)*UI_LIST_ROW);
  if(y>=UI_LIST_TOP && y+UI_LIST_ROW<=BAR_TOP)ui_trace_target(i,0,y,320,UI_LIST_ROW);
 }
}
PSX3D_COLD void ui_trace_welcome(const char *title,int page,int count)
{ui_trace_begin(title,count,page,0);ui_trace_row(page,title,"",0);}
#endif
