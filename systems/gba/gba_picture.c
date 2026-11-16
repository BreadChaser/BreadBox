/* GPL-2.0-or-later. One lookup during upload; LCD mixing stays on PICA. */
#include <stdlib.h>
#include <string.h>
#include "gba_picture_host.h"
#include "gba_color.h"
#include "gba_settings.h"
#include "gba_upload.h"
#include "platform_video.h"
#include "app_log.h"
#include "bb_save.h"
#include "cold_code.h"
#include "gba_picture_mix.h"
extern int gpsp_gba_stereo_valid(void);
extern bool ui_wants_3d;
extern bool dev_remote;
static uint16_t *table;
static int look=-1,filter=-1;
static C3D_Tex history[2][2];
static C3D_Tex scratch;
static C3D_RenderTarget *target;
static unsigned next[2];
static bool previous[2],last_stereo;
static u64 upload_ticks,mix_ticks;
static unsigned samples,gpu_samples;
static double gpu_ms;
static bool mixing;
static const char *error;
static PSX3D_COLD void release_mix(void)
{
 if(gspHasGpuRight()){video_gpu_idle();gba_gpu_detach_textures();}
 if(target)C3D_RenderTargetDelete(target);
 target=NULL;
 if(scratch.data)C3D_TexDelete(&scratch);
 memset(&scratch,0,sizeof(scratch));
 for(unsigned eye=0;eye<2;eye++) {
  for(unsigned i=0;i<2;i++) {
   if(history[eye][i].data)C3D_TexDelete(&history[eye][i]);
   memset(&history[eye][i],0,sizeof(history[eye][i]));
  }
  previous[eye]=false;next[eye]=0;
 }
 mixing=false;
 if(gspHasGpuRight())C2D_Prepare();
}
static PSX3D_COLD void configure(void)
{
 error=NULL;
 if(look!=gba_settings.color_correction || (gba_settings.color_correction && !table)) {
  if(gba_settings.color_correction) {
   if(!table)table=malloc(65536*sizeof(*table));
   if(table)gba_color_table(table,gba_settings.color_correction);
   else error="Not enough memory for GBA colors.";
  }
  look=gba_settings.color_correction;
  previous[0]=previous[1]=false;
 }
 if(!gba_settings.frame_blend) {
  if(mixing)release_mix();
  return;
 }
 *gba_picture_bridge.lock_hold=0;
 if(mixing || !gspHasGpuRight() || !*gba_picture_bridge.texture_ready)return;
 if(!gba_gpu_ui_init()){error="Not enough memory for LCD ghosting.";return;}
 for(unsigned eye=0;eye<2;eye++)for(unsigned i=0;i<2;i++) {
  if(!C3D_TexInit(&history[eye][i],256,256,GPU_RGB565)){release_mix();error="Not enough memory for LCD ghosting.";return;}
  C3D_TexSetFilter(&history[eye][i],GPU_NEAREST,GPU_NEAREST);
 }
 if(!C3D_TexInitVRAM(&scratch,256,256,GPU_RGB565) ||
    !(target=C3D_RenderTargetCreateFromTex(&scratch,GPU_TEXFACE_2D,0,-1))) {
  release_mix();error="Not enough memory for LCD ghosting.";return;
 }
 mixing=true;
}
/* Keep every history texture unblended: a recursive blend would leave trails.
 * The first picture and a newly admitted stereo pair have no older picture.
 * Copies and draws are queued after the previous-frame fence, without readback. */
static void blend_eye(unsigned eye,C3D_Tex *texture)
{
 if(!mixing)return;
 unsigned current=next[eye];C3D_Tex *now=&history[eye][current],*before=&history[eye][current^1];
 C3D_FrameSplit(0);GX_TextureCopy(texture->data,0,now->data,0,texture->size,8);
 if(previous[eye]) {
  gba_picture_mix(target,before,now);
  C3D_FrameSplit(0);GX_TextureCopy(scratch.data,0,texture->data,0,texture->size,8);
 }
 previous[eye]=true;next[eye]^=1;
}
static void account(const void *data,size_t pitch)
{
 const gba_picture_host *h=&gba_picture_bridge;
 if(*h->boot_hold)h->sample(data,pitch);
 if(!dev_remote)return;
 u32 hash=2166136261u;bool black=true,white=true;
 for(unsigned y=0;y<160;y+=20)for(unsigned x=0;x<240;x+=30) {
  u16 c;memcpy(&c,(const u8*)data+y*pitch+x*2,2);
  hash=(hash^c)*16777619u;black&=c==0;white&=c==0xffff;
 }
 *h->blank+=black||white;*h->repeat+=hash==*h->hash;*h->hash=hash;
}
/* The default callback is retained verbatim. The optional path substitutes a
 * lookup for each upload store, rather than correcting a second CPU image.
 * Core post-processing stays disabled so layer rendering supplies both eyes. */
void gba_picture_video(const void *data,unsigned w,unsigned height,size_t pitch)
{
 const gba_picture_host *h=&gba_picture_bridge;
 if(!gba_settings.color_correction && !gba_settings.frame_blend) {
  if(*h->texture_ready && filter!=gba_settings.smoothing) {
   for(unsigned eye=0;eye<2;eye++)C3D_TexSetFilter(h->texture[eye],gba_settings.smoothing?GPU_LINEAR:GPU_NEAREST,gba_settings.smoothing?GPU_LINEAR:GPU_NEAREST);
   filter=gba_settings.smoothing;
  }
  h->original(data,w,height,pitch);
  if(dev_remote && data && w==240 && height==160 && *h->texture_ready && !*h->probe) {
   gpu_ms+=C3D_GetDrawingTime();gpu_samples++;
  }
  return;
 }
 if(gba_settings.frame_blend && !mixing && !error)configure();
 if(error){*h->failed=true;return;}
 if(!data||w!=240||height!=160||!*h->texture_ready){++*h->nulls;return;}
 *h->last_video=svcGetSystemTick();
 if(gba_settings.frame_blend)*h->lock_hold=0;
 else if(!ui_wants_3d)h->track(data,pitch);
 if(*h->probe)return;
 if(dev_remote)++*h->drawn;
 video_gpu_frame_begin();video_gpu_wait_previous();
 if(dev_remote){gpu_ms+=C3D_GetDrawingTime();gpu_samples++;}
 u64 start=svcGetSystemTick();
 bool stereo=ui_wants_3d && gpsp_gba_stereo_valid();
 if(stereo!=last_stereo){previous[0]=previous[1]=false;last_stereo=stereo;}
 *h->stereo_frame=stereo;ui_wants_3d=stereo;bb_session_set_stereo(stereo);*h->indexed_picture=false;
 for(unsigned eye=0;eye<(stereo?2u:1u);eye++) {
  C3D_Tex *tex=h->texture[eye];const void *pixels=eye?*h->right_pixels:data;
  if(table && gba_settings.color_correction)gba_color_upload(tex->data,pixels,eye?480:pitch,table);
  else gba_upload_tiles_local(tex->data,pixels,eye?480:pitch);
  GSPGPU_FlushDataCache(tex->data,tex->size);
  C3D_TexSetFilter(tex,GPU_NEAREST,GPU_NEAREST);
 }
 upload_ticks+=svcGetSystemTick()-start;start=svcGetSystemTick();
 for(unsigned eye=0;eye<(stereo?2u:1u);eye++) {
  C3D_Tex *tex=h->texture[eye];blend_eye(eye,tex);
  C3D_TexSetFilter(tex,gba_settings.smoothing?GPU_LINEAR:GPU_NEAREST,gba_settings.smoothing?GPU_LINEAR:GPU_NEAREST);
 }
 filter=gba_settings.smoothing;
 mix_ticks+=svcGetSystemTick()-start;samples++;
 C2D_Prepare();*h->picture_ready=true;account(data,pitch);
}
PSX3D_COLD void gba_picture_begin(void)
{
 error=NULL;look=filter=-1;last_stereo=false;upload_ticks=mix_ticks=0;samples=gpu_samples=0;gpu_ms=0;
 gba_settings_changed=configure;
}
void gba_picture_reset(void){previous[0]=previous[1]=false;}
const char *gba_picture_error(void){return error;}
PSX3D_COLD void gba_picture_end(void)
{
 gba_settings_changed=NULL;
 if(mixing)release_mix();
 free(table);table=NULL;look=-1;
 if(samples || gpu_samples)log_printf("gba-picture: n=%u upload_ms=%.6f mix_submit_ms=%.6f gpu_ms=%.6f gpu_n=%u color=%d ghosting=%d\n",samples,
  samples?upload_ticks*1000.0/(SYSCLOCK_ARM11*(double)samples):0,samples?mix_ticks*1000.0/(SYSCLOCK_ARM11*(double)samples):0,
  gpu_samples?gpu_ms/gpu_samples:0,gpu_samples,gba_settings.color_correction,gba_settings.frame_blend);
}
