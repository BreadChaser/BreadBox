#!/usr/bin/env python3
"""Compare worker TV/card pixels against the immutable pre-fix implementation."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
new=(root/'app/ui/room_scene.c').read_text()
old=(root/'tests/room/tv-reference.c').read_text()
def function(source,name):
 import re
 m=re.search(r'(?:static )?(?:bool|void|u16) '+name+r'\([^;{}]*\)\s*(?://[^\n]*\n\s*)?\{',source);assert m,name
 start=m.start();a=source.index('{',start);i=a+1;depth=1
 while depth:
  depth+=(source[i]=='{')-(source[i]=='}');i+=1
 return source[start:i]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef uint32_t u32;typedef uint16_t u16;typedef uint8_t u8;typedef uint64_t u64;
#define SYSCLOCK_ARM11 1000000ull
static u64 svcGetSystemTick(void){return 0;}
static void log_printf(const char *fmt,...){(void)fmt;}
typedef struct {void *data;int width,height,fmt;} C3D_Tex;
typedef struct {int width,height;float left,top,right,bottom;} Tex3DS_SubTexture;
static u16 *tv_map;static u8 *tv_k;
static struct {bool ok,tv_static,tv_ok;C3D_Tex tv;} S;
#define GPU_RGB565 0
#define GPU_LINEAR 0
#define GPU_REPEAT 0
static bool C3D_TexInit(C3D_Tex *t,int w,int h,int fmt){*t=(C3D_Tex){calloc(w*h,2),w,h,fmt};return t->data!=NULL;}
static void C3D_TexFlush(C3D_Tex *t){(void)t;}
static void C3D_TexSetFilter(C3D_Tex *t,int a,int b){(void)t;(void)a;(void)b;}
static void C3D_TexSetWrap(C3D_Tex *t,int a,int b){(void)t;(void)a;(void)b;}
'''
body='\n'.join(function(old,n) for n in ['tile16','rgb565i'])
body+='\n'+function(new,'tv_maps')
body+='\n'+function(old,'room_scene_tv').replace('room_scene_tv(', 'reference(')
body+='\n'+function(new,'room_scene_tv_bake')
body+='\n'+function(new,'tv_resample')
life=new.index('typedef struct tv_job');body+='\n'+new[life:new.index('static void tv_resample',life)]
# Original texture fitting body, extracted unchanged from the former GPU entrypoint.
f=function(old,'room_scene_tv_tex');a=f.index('\tconst u16 *d');b=f.index('\troom_scene_tv(px);')
body+='\nstatic void reference_fit(const C3D_Tex *t,const Tex3DS_SubTexture *sub,u16 *px){\n'+f[a:b]+'}\n'
main=r'''
int main(void){
 assert(tv_maps());S.ok=true;
 u16 *px=malloc(256*192*2),*out=malloc(256*256*2),*card=malloc(256*256*2),*expected=calloc(256*256,2);
 uint32_t seed=7;
 for(int test=0;test<9;test++){
  for(int i=0;i<256*192;i++){seed=seed*1664525u+1013904223u;px[i]=test==1?0:test==2?65535:(u16)(seed>>8);}
  reference(test?px:NULL);assert(room_scene_tv_bake(test?px:NULL,out,card));assert(!memcmp(S.tv.data,out,256*256*2));
  if(test){memset(expected,0,256*256*2);memcpy(expected,px,256*192*2);tile16(out,expected,256,256);assert(!memcmp(out,card,256*256*2));}
 }
 // PS1 auto-thumbnail: the prepared card tile is identical to states upload,
 // and fitting its 256x192 subregion returns exactly the original pixels.
 C3D_Tex thumbnail={card,256,256,GPU_RGB565};
 Tex3DS_SubTexture thumb={256,192,0,1,1,.25f};
 tv_resample(&thumbnail,&thumb,out);assert(!memcmp(out,px,256*192*2));
 reference(out);assert(room_scene_tv_bake(px,out,NULL));assert(!memcmp(S.tv.data,out,256*256*2));
 C3D_Tex tex={px,256,192,GPU_RGB565};
 for(int test=0;test<3;test++) {
  Tex3DS_SubTexture sub={test==0?128:256, test==0?192:128,0,1,1,0};
  tv_resample(&tex,&sub,out);reference_fit(&tex,&sub,card);assert(!memcmp(out,card,256*192*2));
 }
 free(px);free(out);free(card);free(expected);free(S.tv.data);
 tv_work=calloc(1,sizeof(*tv_work));assert(tv_work);
 room_scene_tv_maps_release();assert(tv_work && tv_map && tv_k); // live callback keeps readers alive
 tv_work->done=1;room_scene_tv_maps_release();assert(!tv_work && !tv_map && !tv_k);
 assert(tv_maps());room_scene_tv_maps_release();assert(!tv_map && !tv_k); // safe reentry/release
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(prefix+body+main)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('TV, snow, card tiling and cover fitting: bit-identical to a5337fd8 PASS')
