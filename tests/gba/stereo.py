#!/usr/bin/env python3
"""Exercise the actual frontend stereo policy with deterministic cost samples."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2]
s=(r/'systems/gba/gba.c').read_text()
a=s.index('static void gba_stereo_prepare(');b=s.index('static Tex3DS_SubTexture',a)
with tempfile.TemporaryDirectory(prefix='gba-stereo-policy-') as tmp:
 p=Path(tmp)/'check.c'
 p.write_text('#include <assert.h>\n'+(r/'systems/gba/gba_timing.h').read_text().replace('#include "../common/retro_clock.h"',(r/'systems/common/retro_clock.h').read_text())+r"""
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef uint64_t u64;typedef uint16_t u16;
#define SYSCLOCK_ARM11 1000000
#define GBA_FPS (16777216.0/280896.0)
#define GPU_RGB565 0
#define GPU_CLAMP_TO_EDGE 0
#define GPU_LINEAR 1
#define GPU_NEAREST 0
struct {void *data;unsigned size;}right_texture;
static u16 *right_pixels;
static bool right_ready=true,stereo_frame,ui_wants_3d,g_new3ds=true,skip_video,stereo_attempt,stereo_force;
static struct {bool stereo,core_color_correction,core_frame_blend,smoothing;} gba_settings={true};
static struct {bool stereo;} g_cfg={true};
static bool gba_3d_available(void){return g_cfg.stereo || !g_new3ds;}
static gba_costs mono_costs,extra_costs,present_costs,skipped_costs;
static u64 stereo_mono_cost,stereo_extra_cost,stereo_present_cost;
static unsigned stereo_mode,stereo_phase,stereo_debt,stereo_review,stereo_cooldown;
static float slider_get(void){return 1;}
static bool gspHasGpuRight(void){return true;}
static void gpsp_gba_stereo_config(u16*p,unsigned n){}
static void video_gpu_idle(void){}
static void C3D_TexDelete(void*p){}
static bool C3D_TexInit(void*p,unsigned a,unsigned b,int c){abort();}
static void C3D_TexSetWrap(void*p,int a,int b){}
static void C3D_TexSetFilter(void*p,int a,int b){}
static void gpsp_gba_stereo_admit(int n){}
"""+s[a:b]+r"""
int main(void){
 right_pixels=malloc(240*160*sizeof(*right_pixels));assert(right_pixels);
 for(unsigned i=0;i<60;i++){gba_cost_add(&mono_costs,8000);gba_cost_add(&present_costs,500);gba_cost_add(&extra_costs,1000);gba_cost_add(&skipped_costs,5000);}
 for(unsigned i=0;i<3600;i++){
  skip_video=false;gba_stereo_prepare(i%2==1);
  assert(stereo_mode==60 && stereo_attempt && !skip_video);
 }
 for(unsigned i=0;i<60;i++)gba_cost_add(&extra_costs,10000);
 unsigned pictures=0,held=0;
 for(unsigned i=0;i<540;i++){
  skip_video=false;gba_stereo_prepare(false);
  assert(stereo_attempt || skip_video);pictures+=stereo_attempt;held+=skip_video;
 }
 assert(stereo_mode==30 && held && pictures);
 for(unsigned i=0;i<60;i++)gba_cost_add(&extra_costs,0);
 for(unsigned i=0;i<540;i++){
  skip_video=false;gba_stereo_prepare(false);
  assert(stereo_mode==30 && (stereo_attempt || skip_video));
 }
 /* Off: preserve ordinary behind-only frame decisions on either console. */
 gba_settings.stereo=false;
 for(unsigned i=0;i<60;i++){
  g_new3ds=i%2;skip_video=i%3==0;bool expected=skip_video;
  gba_stereo_prepare(true);
  assert(!stereo_attempt && !stereo_mode && skip_video==expected);
 }
 puts("GBA real admission: late frames retain stereo; overload latches 30; off keeps skip decisions: PASS");
}
""")
 subprocess.run(['cc','-O2','-Wall','-Wno-unused-variable','-Wno-unused-function',str(p),'-o',tmp+'/check'],check=True)
 subprocess.run([tmp+'/check'],check=True)
