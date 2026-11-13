// The room styles (theme.c) and the menu backdrop (scene.c), compiled with a counting GPU
// substitute: the style list, the migration of saved theme ids, the backdrop per state.
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "theme.h"
#include "../../core/deps/libretro-common/include/libretro.h"
static struct {int theme;} g_cfg;
static bool g_new3ds, playing, fail_tex, fail_text;
static unsigned textures, texts, draws;
static u64 now;
#define SYSCLOCK_ARM11 268123480ULL
#define CUR_PROCESS_HANDLE 0
#define GPU_A8 0
#define GPU_LINEAR 0
#define GPU_CLAMP_TO_EDGE 0
#define GPU_BLEND_ADD 0
#define GPU_SRC_ALPHA 0
#define GPU_ONE 0
#define GPU_ONE_MINUS_SRC_ALPHA 0
#define C2D_WithColor 0
typedef struct {void *data; unsigned size;} C3D_Tex;
typedef struct {unsigned short width,height;float left,top,right,bottom;} Tex3DS_SubTexture;
typedef struct {int value;} C2D_Text;
typedef void *C2D_TextBuf;
typedef struct {int value;} C2D_ImageTint;
typedef struct {C3D_Tex *tex;Tex3DS_SubTexture *sub;} C2D_Image;
static bool config_in_game(void){return playing;}
static bool dev_enabled(void){return false;}
static u64 svcGetSystemTick(void){return now;}
static void svcStoreProcessDataCache(int p,u32 a,unsigned n){(void)p;(void)a;(void)n;}
static float ui_depth(float x){return x*.75f;}
static void video_gpu_idle(void){}
#define log_printf(...) ((void)0)
#define C2D_Flush() ((void)0)
#define C3D_AlphaBlend(...) ((void)0)
#define C3D_TexSetFilter(...) ((void)0)
#define C3D_TexSetWrap(...) ((void)0)
static bool C3D_TexInit(C3D_Tex *t,unsigned w,unsigned h,int fmt){(void)fmt;if(fail_tex)return false;t->data=malloc(t->size=w*h);assert(t->data);textures++;return true;}
static void C3D_TexDelete(C3D_Tex *t){assert(t->data&&textures);free(t->data);t->data=NULL;textures--;}
static C2D_TextBuf C2D_TextBufNew(unsigned n){if(fail_text)return NULL;texts++;return malloc(n);}
static void C2D_TextBufDelete(C2D_TextBuf b){assert(b&&texts);free(b);texts--;}
#define C2D_TextParse(...) ((void)0)
#define C2D_TextOptimize(...) ((void)0)
#define C2D_PlainImageTint(...) ((void)0)
static void xy(float x,float y,float z){assert(isfinite(x)&&isfinite(y)&&isfinite(z));assert(fabsf(x)<100000&&fabsf(y)<100000);}
static void C2D_DrawTriangle(float x,float y,u32 a,float x1,float y1,u32 b,float x2,float y2,u32 c,float z){(void)a;(void)b;(void)c;xy(x,y,z);xy(x1,y1,z);xy(x2,y2,z);draws++;}
static void C2D_DrawRectSolid(float x,float y,float z,float w,float h,u32 c){(void)c;xy(x,y,z);assert(isfinite(w)&&isfinite(h));draws++;}
static void C2D_DrawRectangle(float x,float y,float z,float w,float h,u32 a,u32 b,u32 c,u32 d){(void)b;(void)c;(void)d;C2D_DrawRectSolid(x,y,z,w,h,a);}
static void C2D_DrawCircleSolid(float x,float y,float z,float r,u32 c){(void)c;xy(x,y,z);assert(isfinite(r)&&r>=0);draws++;}
static void C2D_DrawImageAt(C2D_Image im,float x,float y,float z,C2D_ImageTint *t,float sx,float sy){(void)t;assert(im.tex->data);xy(x,y,z);assert(isfinite(sx)&&isfinite(sy)&&sx>0&&sy>0);draws++;}
static void C2D_DrawText(C2D_Text *t,int f,float x,float y,float z,float sx,float sy,u32 c){(void)t;(void)f;(void)c;xy(x,y,z);assert(isfinite(sx)&&isfinite(sy));draws++;}
// Room themes paint the bookshelf room (shelf_scene.c, its own test); count the call.
static unsigned room_draws;
void shelf_scene_backdrop(bool bottom){(void)bottom;room_draws++;draws++;}
// the room styles' pictures (ui.h, left out with the SDK)
enum { UI_ICON_SUN = 31, UI_ICON_MOON = 33 };
#include "theme-body.c"
#include "scene-body.c"
int main(void){
 // the themes are the room styles: Day (the default) and Night, every one a room with its icon
 assert(theme_count()==2);
 assert(!strcmp(theme_get(0)->id,"day") && !strcmp(theme_get(1)->id,"night"));
 assert(!strcmp(theme_get(theme_default())->id,THEME_DEFAULT_ID) && theme_default()==0);
 for(int i=0;i<theme_count();i++)assert(theme_room(theme_get(i)) && theme_get(i)->icon && theme_get(i)->name[0]);
 assert(theme_get(0)->icon!=theme_get(1)->icon);
 // saved ids: the styles load as themselves; every retired theme (and nonsense) loads as Day
 assert(theme_from_saved_id("day")==0 && theme_from_saved_id("night")==1);
 static const char *const retired[]={"rustic-room","light","dark","original","midnight-teal","ps1-grey","sunset-magenta",
  "high-contrast","phosphor","eclipse","ember","solar","glacier","citron","sienna","","Night","nightx",NULL};
 for(int i=0;retired[i];i++)assert(theme_from_saved_id(retired[i])==0);
 assert(theme_from_saved_id(NULL)==0 && theme_from_id("rustic-room")<0);
 // the menus keep the rustic room's roles in every style
 assert(theme_get(0)->surface==theme_get(1)->surface && theme_get(0)->room==theme_get(1)->room);
 // the backdrop: the room's in every style, the plain page in a game's pause dialogs; never a resource
 for(int i=0;i<theme_count();i++){
  g_cfg.theme=i;draws=room_draws=0;scene_draw(false);scene_draw(true);
  assert(!textures&&!texts&&room_draws==2&&draws==2);
 }
 playing=true;draws=room_draws=0;scene_draw(false);scene_draw(true);assert(!textures&&!texts&&!room_draws&&draws==2);
 printf("room styles: %d (day default), retired ids load as Day, backdrop per state; PASS\n",theme_count());
 return 0;
}
