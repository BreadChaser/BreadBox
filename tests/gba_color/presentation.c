/* Execute the optional presenter. The fake GPU checks ownership and order;
 * actual PICA rounding and picture orientation are emulator/device checks. */
#include "gba_picture_host.h"
#include "gba_settings.h"
#include "gba_color.h"
#include "gba_upload.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
GbaSettings gba_settings;
void (*gba_settings_changed)(void);
bool ui_wants_3d,dev_remote=true;
static bool ready=true,probe,picture,stereo,indexed,boot,failed,valid=true;
static u64 last,tick;
static unsigned nulls,blank,repeat,hold,drawn,originals,allocs,freed,draws,tracked,targets;
static u32 hash;
static int fail_alloc=-1;
static bool fenced,ui_ready,frame_open,fail_target;
static double logged_upload;
static uint16_t pixels[240*160],right_pixels[240*160],*right=right_pixels;
static C3D_Tex texture[2];
static C3D_RenderTarget *active;
bool C3D_TexInit(C3D_Tex *t,unsigned w,unsigned h,int fmt)
{
 (void)fmt;if(fail_alloc==0)return false;if(fail_alloc>0)fail_alloc--;
 t->size=w*h*2;t->data=calloc(1,t->size);allocs++;return t->data!=NULL;
}
bool C3D_TexInitVRAM(C3D_Tex *t,unsigned w,unsigned h,int fmt)
{assert(!frame_open);bool ok=C3D_TexInit(t,w,h,fmt);t->vram=ok;return ok;}
void C3D_TexDelete(C3D_Tex *t){free(t->data);t->data=NULL;freed++;}
void C3D_TexSetFilter(C3D_Tex *t,unsigned a,unsigned b){assert(a==b);t->filter=a;}
C3D_RenderTarget *C3D_RenderTargetCreateFromTex(C3D_Tex *t,int face,unsigned level,int depth)
{
 (void)face;(void)level;(void)depth;assert(!frame_open && t->vram);
 if(fail_target)return NULL;
 C3D_RenderTarget *r=malloc(sizeof(*r));assert(r);r->frameBuf.colorBuf=t->data;targets++;return r;
}
void C3D_RenderTargetDelete(C3D_RenderTarget *t){assert(!frame_open);targets--;free(t);}
void C3D_FrameSplit(unsigned x){(void)x;assert(fenced);}
float C3D_GetDrawingTime(void){return 0;}
void C3D_DepthTest(bool enabled,unsigned function,unsigned write)
{assert(!enabled && function==GPU_ALWAYS && write==GPU_WRITE_COLOR);}
void GX_TextureCopy(const void *a,unsigned b,void *c,unsigned d,unsigned n,unsigned f)
{(void)b;(void)d;(void)f;assert(fenced);memcpy(c,a,n);}
void GSPGPU_FlushDataCache(const void *a,unsigned b){(void)a;(void)b;assert(fenced);}
void C2D_Prepare(void){ui_ready=true;}
void C2D_SceneBegin(C3D_RenderTarget *t){active=t;}
void C2D_DrawImageAt(C2D_Image image,float x,float y,float z,const C2D_ImageTint *t,float sx,float sy)
{
 (void)x;(void)y;(void)z;assert(sx==1 && sy==1 && fenced && active);
 assert(image.subtex->top>image.subtex->bottom);
 assert(active->frameBuf.colorBuf!=image.tex->data);
 uint16_t *out=active->frameBuf.colorBuf,*in=image.tex->data;
 if(!t)memcpy(out,in,image.tex->size);
 else {
  assert(t->alpha==0.5f);
  for(unsigned i=0;i<image.tex->size/2;i++)out[i]=((((out[i]>>11)+(in[i]>>11)+1)/2)<<11)|((((out[i]>>5&63)+(in[i]>>5&63)+1)/2)<<5)|(((out[i]&31)+(in[i]&31)+1)/2);
 }
 draws++;
}
void C2D_AlphaImageTint(C2D_ImageTint *t,float alpha){t->alpha=alpha;}
void C2D_Flush(void){}
bool gba_gpu_ui_init(void){return true;}
void gba_gpu_detach_textures(void){ui_ready=false;}
void gba_gpu_restore_ui(void){gba_gpu_detach_textures();C2D_Prepare();}
bool gspHasGpuRight(void){return true;}
u64 svcGetSystemTick(void){return ++tick;}
void video_gpu_idle(void){}
void video_gpu_frame_begin(void){assert(!frame_open);frame_open=true;fenced=false;}
void video_gpu_wait_previous(void){fenced=true;}
void log_printf(const char *s,...)
{
 assert(!strncmp(s,"gba-picture:",12));
 va_list ap;va_start(ap,s);(void)va_arg(ap,unsigned);logged_upload=va_arg(ap,double);va_end(ap);
}
void bb_session_set_stereo(bool s){assert(s==stereo);}
int gpsp_gba_stereo_valid(void){return valid;}
static void original(const void *p,unsigned w,unsigned h,size_t pitch)
{
 (void)w;(void)h;originals++;
 if(p){gba_upload_tiles_local(texture[0].data,p,pitch);picture=true;}
}
static void track(const void *p,size_t n){(void)p;(void)n;tracked++;}
static void sample(const void *p,size_t n){(void)p;(void)n;}
const gba_picture_host gba_picture_bridge={
 .original=original,.texture={&texture[0],&texture[1]},.right_pixels=&right,
 .texture_ready=&ready,.probe=&probe,.picture_ready=&picture,.stereo_frame=&stereo,
 .indexed_picture=&indexed,.boot_hold=&boot,.failed=&failed,.last_video=&last,
 .nulls=&nulls,.blank=&blank,.repeat=&repeat,.hash=&hash,.lock_hold=&hold,.drawn=&drawn,.track=track,.sample=sample
};
static void fill(unsigned l,unsigned r)
{for(unsigned i=0;i<240*160;i++){pixels[i]=l;right_pixels[i]=r;}}
static unsigned left(void){return *(uint16_t*)texture[0].data;}
static unsigned eye_right(void){return *(uint16_t*)texture[1].data;}
static void frame(unsigned l,unsigned r,bool eye)
{
 fill(l,r);picture=false;ui_wants_3d=eye;gba_picture_video(pixels,240,160,480);assert(picture && !failed);frame_open=false;
}
static void begin(int color,bool ghost)
{
 gba_settings=GBA_SETTINGS_DEFAULT;gba_settings.color_correction=color;gba_settings.frame_blend=ghost;
 gba_picture_begin();gba_settings_changed();assert(!gba_picture_error());
}
int main(void)
{
 assert(C3D_TexInit(&texture[0],256,256,0) && C3D_TexInit(&texture[1],256,256,0));
 begin(0,false);frame(0xf800,0x001f,false);assert(originals==1 && left()==0xf800);gba_picture_end();
 for(int color=1;color<GBA_COLOR_COUNT;color++) {
  uint16_t *lut=malloc(65536*2);assert(lut);gba_color_table(lut,color);
  begin(color,false);frame(0x63d1,0xbc28,true);assert(stereo && left()==lut[0x63d1] && eye_right()==lut[0xbc28]);
  free(lut);gba_picture_end();
 }
 begin(GBA_COLOR_SOFT,false);gba_settings.smoothing=1;frame(0xf800,0,false);
 assert(texture[0].filter==GPU_LINEAR);
 gba_settings.color_correction=gba_settings.smoothing=0;gba_settings_changed();frame(0xf800,0,false);
 assert(texture[0].filter==GPU_NEAREST && texture[1].filter==GPU_NEAREST);
 gba_settings.smoothing=1;gba_settings_changed();frame(0xf800,0,true);
 assert(texture[0].filter==GPU_LINEAR && texture[1].filter==GPU_LINEAR);gba_picture_end();
 begin(GBA_COLOR_SOFT,false);for(unsigned i=0;i<32;i++)frame(0xf800,0,false);gba_picture_end();
 assert(fabs(logged_upload-1000.0/SYSCLOCK_ARM11)<1e-12);   /* no 32-bit clock-times-count overflow */
 hold=12;begin(0,true);assert(!hold);
 hold=12;frame(0xf800,0x001f,true);assert(!hold && left()==0xf800 && eye_right()==0x001f);
 frame(0x07e0,0xf800,true);assert(left()==0x8400 && eye_right()==0x8010);
 unsigned used=allocs;
 frame(0x001f,0x07e0,true);assert(left()==0x0410 && eye_right()==0x8400 && allocs==used);
 frame(0xffff,0,false);assert(left()==0xffff);   /* stereo/mono transition: no stale phase */
 frame(0xf800,0x001f,true);assert(left()==0xf800 && eye_right()==0x001f);
 gba_picture_reset();frame(0xffff,0,true);assert(left()==0xffff && !eye_right());
 unsigned old=draws;probe=true;fill(0,0);gba_picture_video(pixels,240,160,480);assert(draws==old);probe=false;
 picture=false;gba_picture_video(NULL,240,160,480);assert(!picture && nulls==1);
 gba_settings.color_correction=GBA_COLOR_CORRECTED;gba_settings_changed();
 uint16_t *lut=malloc(65536*2);assert(lut);gba_color_table(lut,GBA_COLOR_CORRECTED);
 frame(0xf800,0x001f,true);assert(left()==lut[0xf800] && eye_right()==lut[0x001f]);free(lut);
 gba_settings.frame_blend=0;gba_settings_changed();assert(!targets && ui_ready);gba_picture_end();
 /* Startup defers VRAM until the primary picture exists and menus are gone. */
 ready=false;begin(0,true);assert(!targets);ready=true;frame(0xf800,0,false);
 assert(targets==1);gba_picture_end();
 /* Every history, scratch and target allocation can fail without leaking. */
 for(int failure=0;failure<6;failure++) {
  gba_settings=GBA_SETTINGS_DEFAULT;gba_settings.frame_blend=1;gba_picture_begin();
  fail_alloc=failure<5?failure:-1;fail_target=failure==5;gba_settings_changed();
  assert(gba_picture_error());failed=false;gba_picture_video(pixels,240,160,480);assert(failed);
  gba_picture_end();failed=false;assert(!targets);
 }
 fail_alloc=-1;fail_target=false;
 C3D_TexDelete(&texture[0]);C3D_TexDelete(&texture[1]);assert(allocs==freed && !targets);
 puts("GBA optional presentation: default delegation, both colored eyes, independent raw histories, phase-lock bypass, skipped/probe holds, reset, GPU ownership/order and allocation cleanup PASS");
}
