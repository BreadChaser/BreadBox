#pragma once
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <citro2d.h>
#include "shelf.h"
#include "libui.h"
#include "covers.h"
#include "cold_code.h"
typedef void *C2D_SpriteSheet;
typedef struct { C3D_Tex *tex; const Tex3DS_SubTexture *sub; } C2D_Image;
typedef struct { int unused; } C2D_ImageTint;
static C2D_SpriteSheet C2D_SpriteSheetLoad(const char *p){(void)p;return NULL;}
static void C2D_SpriteSheetFree(C2D_SpriteSheet p){(void)p;}
static C2D_Image C2D_SpriteSheetGetImage(C2D_SpriteSheet p,int n){(void)p;(void)n;return (C2D_Image){0};}
static void C2D_PlainImageTint(C2D_ImageTint *p,u32 c,float a){(void)p;(void)c;(void)a;}
static void C2D_DrawImageAt(C2D_Image i,float x,float y,float z,const C2D_ImageTint *t,float sx,float sy)
{(void)i;(void)x;(void)y;(void)z;(void)t;(void)sx;(void)sy;}
#ifdef SHELF_HIT_TEST
#include SHELF_HIT_TEST
#elif !defined(SHELF_HIT_EXTERNAL)
float shelf_scene_strip_position(const shelf_row *r,float x){return 12+(x-40)/fmaxf(r->end-40,400)*296;}
int shelf_scene_strip_hit(const shelf_view *v,float x,float y){return -1;}
#endif
static bool check_top,geometry_on;
static float geometry[8192][5];static int geometry_n;
static void rectangle(float x,float y,float z,float w,float h)
{if(geometry_on){assert(geometry_n<8192);float *r=geometry[geometry_n++];r[0]=x;r[1]=y;r[2]=z;r[3]=w;r[4]=h;}}
static char drawn[64][256];static int ndrawn,drawn_rows;
static void record(const char *s){snprintf(drawn[ndrawn++%64],256,"%s",s?s:"");}
static bool saw(const char *s){for(int i=0;i<ndrawn && i<64;i++)if(!strcmp(s,drawn[i]))return true;return false;}
void C2D_DrawRectSolid(float x,float y,float z,float w,float h,u32 c)
{(void)c;if(w==UI_LOOK_RULE_WIDTH && h==UI_LOOK_RULE_HEIGHT)assert(z>UI_Z_TEXT);rectangle(x,y,z,w,h);}
void ui_text_ex(C2D_Font f,float x,float y,float size,u32 c,u32 flags,const char *s)
{(void)f;(void)x;(void)c;(void)flags;if(check_top)assert(y+size*30<=UI_NOTICE_TOP);record(s);}
const char *ui_fit_ex(C2D_Font f,const char *s,float size,float min,float max,float *out)
{(void)f;(void)min;(void)max;*out=size;return s;}
const char *ui_fit(const char *s,float size,float width){(void)size;(void)width;return s;}
int ui_fit_lines(C2D_Font font,const char *s,float size,float min,float w,int max,ui_fit_line *out,float *got)
{(void)font;(void)min;(void)w;(void)max;snprintf(out[0],sizeof(out[0]),"%s",s);*got=size;return 1;}
C2D_Font libui_font(void){return NULL;}
static int cover_ids[16],ncover_ids;
C3D_Tex *covers_get(int id,Tex3DS_SubTexture *sub){(void)sub;cover_ids[ncover_ids++%16]=id;return NULL;}
void ui_icon_draw(int icon,float x,float y,float size,u32 c,u32 bg)
{(void)icon;(void)x;(void)y;(void)size;(void)c;(void)bg;}
void shelf_scene_card(const shelf_card *c){record(c->title);}
void flist_update(flist_t *f,int sel,int n,int rows)
{(void)n;f->scroll=f->scroll_target=sel>=rows?sel-rows+1:0;}
int flist_row_at(const flist_t *f,float y,float h,float py){return (int)((py-y)/h+f->scroll);}
void flist_draw_sections(const flist_t *f,int n,int sel,float x,float y,float w,float h,int rows,
 const char *(*label)(int,void *),const char *(*value)(int,void *),void *ud,u32 accent,bool (*heading)(int,void *))
{
 (void)x;(void)y;(void)w;(void)h;(void)accent;
 drawn_rows=rows;
 assert(sel>=0&&sel<n&&!heading(sel,ud));
 for(int i=(int)f->scroll;i<n&&i<(int)f->scroll+rows;i++){record(label(i,ud));record(value(i,ud));}
}

void C2D_DrawRectangle(float x,float y,float z,float w,float h,u32 a,u32 b,u32 c,u32 d)
{(void)a;(void)b;(void)c;(void)d;C2D_DrawRectSolid(x,y,z,w,h,a);}
float ui_text_width(C2D_Font f,float size,const char *s){(void)f;return strlen(s)*size*15;}
#include "../../app/ui/look_style.c"
#define look_list_draw production_look_list_draw
#include "../../app/ui/look_list.c"
#undef look_list_draw
void look_list_draw(const flist_t *f,int n,int sel,float x,float y,float w,float rh,int rows,
 const char *(*label)(int,void *),const char *(*value)(int,void *),void *ud,bool (*heading)(int,void *))
{drawn_rows=rows;production_look_list_draw(f,n,sel,x,y,w,rh,rows,label,value,ud,heading);}
