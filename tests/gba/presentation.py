#!/usr/bin/env python3
"""Execute actual upload callback with a persistent Citro2D texture binding.

GPU cache invalidation follows a dirty texture bind, independently of ARM cache
cleaning. A blank bottom screen does not change Citro2D's cached texture object.
"""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2]
s=(r/'systems/gba/gba.c').read_text()
a=s.index('static unsigned morton(');b=s.index('static void audio_sink',a)
with tempfile.TemporaryDirectory(prefix='gba-present-') as td:
 p=Path(td)/'check.c'
 p.write_text('#include "'+str(r/'systems/gba/gba_upload.h')+'"\n'+r'''
#define DIAG_START(name) ((void)0)
#define DIAG_END(name,n) ((void)0)
static int gba_remote_cpuopt;
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t u64;typedef uint32_t u32;typedef uint16_t u16;typedef uint8_t u8;
static u16 left[256*256],right[256*256],pixels[240*160],eyes[240*160];
static struct {void *data;unsigned size;}texture={left,sizeof(left)},right_texture={right,sizeof(right)};
static u16 *right_pixels=eyes;
static bool indexed_ready,indexed_picture;
static bool gba_index_gpu_render(const void *p,size_t pitch,const uint16_t *right){return false;}
static bool texture_ready=true,dev_remote=true,right_ready=true,stereo_frame,ui_wants_3d;
static bool probe_frame;static unsigned tracked;
static void gba_track_alternation(const void *p,size_t pitch){(void)p;(void)pitch;tracked++;}
static struct {bool core_color_correction,core_frame_blend;}gba_settings;
static unsigned begin,waited,flushed,prepared,drawn,invalidations;static bool dirty;
static u64 svcGetSystemTick(void){return 1;}
static void video_gpu_frame_begin(void){begin++;}
static void video_gpu_wait_previous(void){assert(begin>waited);waited++;}
static void GSPGPU_FlushDataCache(void *p,unsigned n){assert(waited==begin);flushed++;}
static bool layer_permission;
static void bb_session_set_stereo(bool enabled){layer_permission=enabled;}
static bool gpsp_gba_stereo_valid(void){return true;}
static void C2D_Prepare(void){assert(flushed>=begin);prepared++;dirty=true;}
'''+s[a:b]+r'''
static void draw(void){drawn++;if(dirty){invalidations++;dirty=false;}}
int main(void){
 for(unsigned frame=0;frame<90;frame++){
  gba_remote_cpuopt=frame&1;
  picture_ready=false;
  if(frame%3==1){
   video_callback(NULL,240,160,480);
   assert(!picture_ready && begin==drawn);continue;
  }
  ui_wants_3d=frame>=45;
  for(unsigned i=0;i<240*160;i++){pixels[i]=(u16)(frame+i);eyes[i]=(u16)(frame+i+3);}
  video_callback(pixels,240,160,480);assert(layer_permission==stereo_frame);
  assert(picture_ready);draw();
  /* Even mono with an unchanged texture object requires a fresh bind. */
  assert(invalidations==drawn && prepared==drawn);
  for(unsigned y=0;y<160;y++)for(unsigned x=0;x<240;x++){
   unsigned i=((y>>3)*32+(x>>3))*64+morton(x,y);
   assert(left[i]==pixels[y*240+x]);
   if(ui_wants_3d)assert(right[i]==eyes[y*240+x]);
  }
 }
 assert(picture_null==30 && begin==60 && waited==60);
 /* Mono pictures feed the alternation tracker; stereo ones do not. */
 assert(tracked==30);
 /* A probe frame is rendered for the tracker only: no GPU frame, no upload. */
 probe_frame=true;ui_wants_3d=false;unsigned b0=begin,f0=flushed;picture_ready=false;
 video_callback(pixels,240,160,480);
 assert(tracked==31 && begin==b0 && flushed==f0 && !picture_ready);
 return 0;
}
''')
 subprocess.run(['cc','-O2','-Wall','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined',str(p),'-o',str(Path(td)/'check')],check=True)
 subprocess.run([str(Path(td)/'check')],check=True)
print('GBA upload ordering, persistent mono texture cache, paired eyes, NULL holds: PASS')
