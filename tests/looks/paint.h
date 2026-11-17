#pragma once
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <citro2d.h>
#include "shelf.h"
#include "libui.h"
#include "covers.h"
#include "cold_code.h"
typedef struct { C3D_Tex *tex; const Tex3DS_SubTexture *sub; } C2D_Image;
typedef struct { int unused; } C2D_ImageTint;
/* PSX3D's moving scene (list_scene.c): drawn or, without memory for it, not; the looks then paint their plain page. */
static int scene_draws,scene_releases;static bool scene_refused;
bool list_scene_draw(void){scene_draws++;return !scene_refused;}
void list_scene_release(void){scene_releases++;}
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
#include "icons.h"
static int place_pictures;   /* PSX3D's own pictures drawn (icons.c) */
bool icons_draw(int slot,int id,float cx,float cy,float size,bool lit,float alpha,float z)
{(void)slot;(void)cx;(void)cy;(void)size;(void)lit;(void)alpha;(void)z;if(id<0)return false;assert(id==ICON_TOGETHER||id==ICON_SETTINGS||id==ICON_SHARE);place_pictures++;return true;}
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
 assert(!n || (sel>=0&&sel<n&&!(heading && heading(sel,ud))));   /* an empty list draws no rows (ui.c) */
 for(int i=(int)f->scroll;i<n&&i<(int)f->scroll+rows;i++){record(label(i,ud));record(value(i,ud));}
}

void C2D_DrawRectangle(float x,float y,float z,float w,float h,u32 a,u32 b,u32 c,u32 d)
{(void)a;(void)b;(void)c;(void)d;C2D_DrawRectSolid(x,y,z,w,h,a);}
float ui_text_width(C2D_Font f,float size,const char *s){(void)f;return strlen(s)*size*15;}
#include "../../app/ui/look_style.c"
#include "../../app/ui/letter_strip.c"
#ifndef PAINT_HAVE_LETTERS
#include "../../app/ui/library_letters.c"
#endif
/* The shared bar and header (ui.c): the cells' bounds and words, the title. */
#ifndef PAINT_OWN_BAR
void ui_bar_draw(const ui_button_t *b,int n)
{for(int i=0;i<n;i++){rectangle(UI_BOTTOM_SCREEN_WIDTH*i/(float)n,BAR_TOP,UI_LOOK_PANEL_Z,UI_BOTTOM_SCREEN_WIDTH/(float)n,UI_SCREEN_HEIGHT-BAR_TOP);record(b[i].label);}}
#endif
void ui_menu_header(const char *title,const char *right){(void)right;record(title);}
