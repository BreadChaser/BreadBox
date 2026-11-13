/* GPL-2.0-or-later. Opt-in development experiment, no gameplay output. */
#ifdef BB_GBA_LUT_PROBE
#include <string.h>
#include <strings.h>
#include "app_log.h"
#include "platform_video.h"
#include "gba_lut_shbin.h"
#include "../../systems/gba/palette_lut.h"
#include "../../systems/gba/gpu_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct LPVertex {float x,y,z,w,u,v;};
static unsigned morton(unsigned x,unsigned y){return (x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);}
static unsigned at(unsigned x,unsigned y){return ((y/8)*4+x/8)*64+morton(x&7,y&7);}
static unsigned color(unsigned i,unsigned seed){unsigned c=(i*997+seed*2357+(i*i*13))&32767;return ((c&31)*8)<<24|(((c>>5)&31)*8)<<16|(((c>>10)&31)*8)<<8|255;}
static void make_luts(C3D_LightLut lut[3],unsigned bits,unsigned seed,unsigned variant,unsigned factor){
 for(int j=-128;j<128;j++){
  /* Constant plateaus. L4 nominal index positions are ~17 LUT entries apart.
   * L8 is deliberately tested with nearest-bin mapping, not assumed exact. */
  unsigned index=bits==4?(unsigned)((j+128)*15.0f/256.0f+0.5f):(unsigned)(j+128);
  unsigned c=color(index,seed);
  for(unsigned k=0;k<3;k++){
   unsigned v=(c>>(24-8*k))&255,unit=k==1?4:8;
   if(variant==1)v=((v/unit*factor)>>4)*unit;
   if(variant==2)v=((v/unit*factor)&15)*16;
   lut[k].data[j&255]=gba_palette_lut_entry(v);
  }
 }
}
static void bind_luts(C3D_LightEnv *e,C3D_LightLut lut[3]){
 C3D_LightEnvLut(e,GPU_LUT_RR,GPU_LUTINPUT_NV,true,&lut[0]);
 C3D_LightEnvLut(e,GPU_LUT_RG,GPU_LUTINPUT_NV,true,&lut[1]);
 C3D_LightEnvLut(e,GPU_LUT_RB,GPU_LUTINPUT_NV,true,&lut[2]);
}
/* Outside a frame: drain the render queue, perform and wait for the GX copy,
 * then discard CPU cache lines before inspecting GPU-owned output. */
static void readback(C3D_Tex *out,C3D_Tex *rb){
 C3D_SyncTextureCopy(out->data,0,rb->data,0,4096,8);
 C3D_FrameBegin(0);C3D_FrameEnd(0); /* wait custom queueDone, not a stale PPF event */
 GSPGPU_InvalidateDataCache(rb->data,4096);
}
static void save_bytes(const char *path,const void *data,unsigned size){
 FILE *f=fopen(path,"wb");if(f){fwrite(data,1,size,f);fclose(f);}
}
int gba_lut_probe(void){
 if(!gba_gpu_ui_init())return -1;
 C3D_Tex src={0},out={0},rb={0},peeled={0},blend[4]={{0}};C3D_RenderTarget *rt=NULL,*peel=NULL;DVLB_s *d=NULL;shaderProgram_s pg;bool pgok=false;
 struct LPVertex *v=NULL;FILE *f=NULL,*bf=NULL,*pf=NULL;int result=-1;C3D_LightEnv env;C3D_Light light;
 C3D_LightLut lut[3];video_gpu_idle();
 if(!C3D_TexInitVRAM(&out,32,32,GPU_RGBA8)||!C3D_TexInit(&rb,32,32,GPU_RGBA8))goto done;
 rt=C3D_RenderTargetCreateFromTex(&out,GPU_TEXFACE_2D,0,GPU_RB_DEPTH24_STENCIL8);if(!rt)goto done;
 d=DVLB_ParseFile((u32*)gba_lut_shbin,gba_lut_shbin_size);if(!d)goto done;
 shaderProgramInit(&pg);pgok=true;shaderProgramSetVsh(&pg,&d->DVLE[0]);
 v=linearAlloc(6*sizeof(*v));if(!v)goto done;
 const struct LPVertex q[6]={{0,0,0,1,0,1},{0,32,0,1,0,0},{32,0,0,1,1,1},{32,0,0,1,1,1},{0,32,0,1,0,0},{32,32,0,1,1,0}};
 memcpy(v,q,sizeof(q));GSPGPU_FlushDataCache(v,sizeof(q));
 FILE *info=fopen("sdmc:/3ds/BreadBox/lut-shader.txt","w");if(info){
  DVLE_s *dv=&d->DVLE[0];fprintf(info,"vertex_stride=%u attributes=float4,float2 permutation=0x10 primitive=TRIANGLES vertices=6\n",(unsigned)sizeof(*v));
  for(unsigned i=0;i<6;i++)fprintf(info,"v%u: %g %g %g %g %g %g\n",i,q[i].x,q[i].y,q[i].z,q[i].w,q[i].u,q[i].v);
  fprintf(info,"outmap_mask=%02x mode=%08lx clock=%08lx\n",dv->outmapMask,dv->outmapMode,dv->outmapClock);
  for(unsigned i=0;i<8;i++)fprintf(info,"outmap[%u]=%08lx\n",i,dv->outmapData[i]);
  for(unsigned i=0;i<dv->outTableSize;i++){DVLE_outEntry_s *o=&dv->outTableData[i];fprintf(info,"output type=%u register=%u mask=%02x\n",o->type,o->regID,o->mask);}
  fclose(info);
 }

 C3D_LightEnvInit(&env);C3D_LightEnvBind(&env);C3D_Material m={.specular1={1,1,1}};C3D_LightEnvMaterial(&env,&m);
 C3D_LightEnvBumpMode(&env,GPU_BUMP_AS_BUMP);C3D_LightEnvBumpSel(&env,0);C3D_LightEnvBumpNormalZ(&env,true);C3D_LightEnvClampHighlights(&env,false);
 C3D_LightInit(&light,&env);C3D_LightColor(&light,1,1,1);C3D_FVec pos=FVec4_New(0,0,1,0);C3D_LightPosition(&light,&pos);
 f=fopen("sdmc:/3ds/BreadBox/lut-probe.bin","wb");if(!f)goto done;
 for(unsigned k=0;k<4;k++)if(!C3D_TexInit(&blend[k],32,32,GPU_RGBA8))goto done;
 if(!C3D_TexInitVRAM(&peeled,32,32,GPU_RGBA8))goto done;
 peel=C3D_RenderTargetCreateFromTex(&peeled,GPU_TEXFACE_2D,0,-1);if(!peel)goto done;
 peel->frameBuf.depthBuf=rt->frameBuf.depthBuf;peel->frameBuf.depthFmt=rt->frameBuf.depthFmt;peel->frameBuf.depthMask=rt->frameBuf.depthMask;
 pf=fopen("sdmc:/3ds/BreadBox/lut-peel.bin","wb");if(!pf)goto done;
 bf=fopen("sdmc:/3ds/BreadBox/lut-blend.bin","wb");if(!bf)goto done;
 for(unsigned bits=4;bits<=8;bits+=4){
  if(!C3D_TexInit(&src,32,32,bits==4?GPU_L4:GPU_L8))goto done;
  memset(src.data,0,src.size);
  for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++){
   unsigned i=bits==4?x&15:(x+y*32)&255,a=at(x,31-y);
   if(bits==4)((u8*)src.data)[a/2]|=i<<(4*(a&1));else ((u8*)src.data)[a]=i;
  }
  C3D_TexFlush(&src);C3D_TexSetFilter(&src,GPU_NEAREST,GPU_NEAREST);
  for(unsigned variant=0;variant<3;variant++)for(unsigned seed=0;seed<16;seed++){
   make_luts(lut,bits,seed,variant,11);bind_luts(&env,lut);
   C3D_FrameBegin(0);C3D_FrameDrawOn(rt);C3D_SetViewport(0,0,32,32);C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0);C3D_BindProgram(&pg);
   C3D_AttrInfo *ai=C3D_GetAttrInfo();AttrInfo_Init(ai);AttrInfo_AddLoader(ai,0,GPU_FLOAT,4);AttrInfo_AddLoader(ai,1,GPU_FLOAT,2);
   C3D_BufInfo *bi=C3D_GetBufInfo();BufInfo_Init(bi);BufInfo_Add(bi,v,sizeof(*v),2,0x10);
   C3D_Mtx proj;Mtx_Ortho(&proj,0,32,32,0,0,1,true);C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,shaderInstanceGetUniformLocation(pg.vertexShader,"projection"),&proj);
   C3D_FVUnifSet(GPU_VERTEX_SHADER,shaderInstanceGetUniformLocation(pg.vertexShader,"depth"),0,0,0,0);
   C3D_TexBind(0,&src);C3D_CullFace(GPU_CULL_NONE);C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_StencilTest(false,GPU_ALWAYS,0,255,0);
   C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);C3D_TexEnvBufUpdate(C3D_Both,0);
   for(unsigned k=0;k<6;k++){C3D_TexEnv *e=C3D_GetTexEnv(k);C3D_TexEnvInit(e);C3D_TexEnvSrc(e,C3D_Both,k?GPU_PREVIOUS:GPU_FRAGMENT_SECONDARY_COLOR,0,0);C3D_TexEnvFunc(e,C3D_Both,GPU_REPLACE);}
   C3D_DrawArrays(GPU_TRIANGLES,0,6);C3D_FrameEnd(0);
   readback(&out,&rb);fwrite(rb.data,1,4096,f);
   if(bits==4&&seed==0&&variant==0){
    for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++)
     ((u32*)blend[0].data)[at(x,31-y)]=(x*8u<<24)|(y*8u<<16)|(((x^y)*8u)<<8)|255;
    C3D_TexFlush(&blend[0]);C3D_TexSetFilter(&blend[0],GPU_NEAREST,GPU_NEAREST);
    C3D_FrameBegin(0);C3D_FrameDrawOn(rt);C3D_LightEnvBind(NULL);C3D_TexBind(0,&blend[0]);
    C3D_TexEnv *control=C3D_GetTexEnv(0);C3D_TexEnvSrc(control,C3D_Both,GPU_TEXTURE0,0,0);
    C3D_DrawArrays(GPU_TRIANGLES,0,6);C3D_FrameEnd(0);readback(&out,&rb);
    save_bytes("sdmc:/3ds/BreadBox/lut-control-native.bin",rb.data,4096);
    C3D_SyncDisplayTransfer(out.data,GX_BUFFER_DIM(32,32),rb.data,GX_BUFFER_DIM(32,32),
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8));
    C3D_FrameBegin(0);C3D_FrameEnd(0);
    GSPGPU_InvalidateDataCache(rb.data,4096);
    save_bytes("sdmc:/3ds/BreadBox/lut-control-linear.bin",rb.data,4096);
    C3D_FrameBegin(0);C3D_FrameDrawOn(rt);
    control=C3D_GetTexEnv(0);C3D_TexEnvSrc(control,C3D_Both,GPU_CONSTANT,0,0);C3D_TexEnvColor(control,0xff0000ff);
    C3D_DrawArrays(GPU_TRIANGLES,0,3);
    control=C3D_GetTexEnv(0);C3D_TexEnvColor(control,0x00ff00ff);C3D_DrawArrays(GPU_TRIANGLES,3,3);
    C3D_FrameEnd(0);readback(&out,&rb);
    save_bytes("sdmc:/3ds/BreadBox/lut-triangles-solid.bin",rb.data,4096);
    C3D_FrameBegin(0);C3D_FrameDrawOn(rt);C3D_LightEnvBind(&env);C3D_TexBind(0,&src);
    control=C3D_GetTexEnv(0);C3D_TexEnvSrc(control,C3D_Both,GPU_FRAGMENT_SECONDARY_COLOR,0,0);
    make_luts(lut,4,0,0,11);bind_luts(&env,lut);C3D_DrawArrays(GPU_TRIANGLES,0,3);
    make_luts(lut,4,1,0,11);bind_luts(&env,lut);C3D_DrawArrays(GPU_TRIANGLES,3,3);
    C3D_FrameEnd(0);readback(&out,&rb);
    save_bytes("sdmc:/3ds/BreadBox/lut-triangles-lit.bin",rb.data,4096);
   }
   if(seed<2&&variant){unsigned k=seed*2+variant-1;memcpy(blend[k].data,rb.data,4096);C3D_TexFlush(&blend[k]);C3D_TexSetFilter(&blend[k],GPU_NEAREST,GPU_NEAREST);}
  }
  /* Resolve two LUT-produced palettes using the proven per-channel carry.
   * Staging is readback/copy only; no CPU palette decode or blend arithmetic. */
  C3D_FrameBegin(0);C3D_FrameDrawOn(rt);C3D_LightEnvBind(NULL);
  C3D_TexBind(0,&blend[0]);C3D_TexBind(1,&blend[2]);
  C3D_TexEnv *e=C3D_GetTexEnv(0);C3D_TexEnvInit(e);C3D_TexEnvSrc(e,C3D_Both,GPU_TEXTURE0,GPU_TEXTURE1,0);C3D_TexEnvFunc(e,C3D_Both,GPU_ADD);
  C3D_DrawArrays(GPU_TRIANGLES,0,6);
  C3D_TexBind(0,&blend[1]);C3D_TexBind(1,&blend[3]);
  for(unsigned channel=0;channel<3;channel++){
   e=C3D_GetTexEnv(0);
   C3D_TexEnvInit(e);C3D_TexEnvSrc(e,C3D_RGB,GPU_CONSTANT,0,0);C3D_TexEnvColor(e,channel==1?0x04040404:0x08080808);C3D_TexEnvFunc(e,C3D_RGB,GPU_REPLACE);
   C3D_TexEnvSrc(e,C3D_Alpha,GPU_TEXTURE0,GPU_TEXTURE1,0);C3D_TexEnvOpAlpha(e,(GPU_TEVOP_A)(GPU_TEVOP_A_SRC_R+channel*2),(GPU_TEVOP_A)(GPU_TEVOP_A_SRC_R+channel*2),GPU_TEVOP_A_SRC_ALPHA);C3D_TexEnvFunc(e,C3D_Alpha,GPU_ADD);
   C3D_AlphaTest(true,GPU_GREATER,254);C3D_DepthTest(false,GPU_ALWAYS,(GPU_WRITEMASK)(GPU_WRITE_RED<<channel));
   C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ONE,GPU_ONE,GPU_ZERO);C3D_DrawArrays(GPU_TRIANGLES,0,6);
  }
  C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);C3D_FrameEnd(0);
  readback(&out,&rb);fwrite(rb.data,1,4096,bf);
  C3D_LightEnvBind(&env);C3D_TexBind(0,&src);
  for(unsigned k=0;k<6;k++){C3D_TexEnv *te=C3D_GetTexEnv(k);C3D_TexEnvInit(te);C3D_TexEnvSrc(te,C3D_Both,k?GPU_PREVIOUS:GPU_FRAGMENT_SECONDARY_COLOR,0,0);C3D_TexEnvFunc(te,C3D_Both,GPU_REPLACE);}
  /* Shared stencil survives a depth-only reset and target change. Palette
   * texture/LUT path remains active throughout candidate replay. */
  int dz=shaderInstanceGetUniformLocation(pg.vertexShader,"depth");
  make_luts(lut,bits,2,0,11);bind_luts(&env,lut);
  C3D_FrameBegin(0);C3D_RenderTargetClear(rt,C3D_CLEAR_ALL,0,0);C3D_RenderTargetClear(peel,C3D_CLEAR_COLOR,0,0);C3D_FrameDrawOn(rt);
  e=C3D_GetTexEnv(0);C3D_TexEnvSrc(e,C3D_Alpha,GPU_TEXTURE0,0,0);C3D_TexEnvOpAlpha(e,GPU_TEVOP_A_SRC_R,GPU_TEVOP_A_SRC_ALPHA,GPU_TEVOP_A_SRC_ALPHA);C3D_AlphaTest(true,GPU_GREATER,0);
  C3D_DepthMap(true,-1,0);C3D_FVUnifSet(GPU_VERTEX_SHADER,dz,-0.9f,0,0,0);
  C3D_DepthTest(true,GPU_ALWAYS,GPU_WRITE_ALL);C3D_StencilTest(true,GPU_ALWAYS,2,255,255);C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_REPLACE);C3D_DrawArrays(GPU_TRIANGLES,0,6);
  C3D_AlphaTest(false,GPU_ALWAYS,0);
  e=C3D_GetTexEnv(0);C3D_TexEnvSrc(e,C3D_Alpha,GPU_FRAGMENT_SECONDARY_COLOR,0,0);C3D_TexEnvOpAlpha(e,GPU_TEVOP_A_SRC_ALPHA,GPU_TEVOP_A_SRC_ALPHA,GPU_TEVOP_A_SRC_ALPHA);
  C3D_StencilTest(true,GPU_ALWAYS,0,255,0);C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP);
  C3D_DepthTest(true,GPU_ALWAYS,GPU_WRITE_DEPTH);C3D_FVUnifSet(GPU_VERTEX_SHADER,dz,0,0,0,0);C3D_DrawArrays(GPU_TRIANGLES,0,6);
  C3D_FrameDrawOn(peel);C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
  for(unsigned rank=4;rank>=1;rank--){
   make_luts(lut,bits,rank,0,11);bind_luts(&env,lut);
   C3D_FVUnifSet(GPU_VERTEX_SHADER,dz,-(1.0f-rank*0.15f),0,0,0);
   C3D_StencilTest(true,GPU_GREATER,rank,255,0);C3D_DrawArrays(GPU_TRIANGLES,0,6);
  }
  C3D_FrameEnd(0);readback(&peeled,&rb);fwrite(rb.data,1,4096,pf);
  C3D_SyncTextureCopy(rt->frameBuf.depthBuf,0,rb.data,0,4096,8);C3D_FrameBegin(0);C3D_FrameEnd(0);GSPGPU_InvalidateDataCache(rb.data,4096);
  save_bytes(bits==4?"sdmc:/3ds/BreadBox/lut-depth4.bin":"sdmc:/3ds/BreadBox/lut-depth8.bin",rb.data,4096);
  C3D_StencilTest(false,GPU_ALWAYS,0,255,0);C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);C3D_FVUnifSet(GPU_VERTEX_SHADER,dz,0,0,0,0);
  if(bits==4){
   FILE *timing=fopen("sdmc:/3ds/BreadBox/lut-probe-times.txt","w");
   C3D_LightLut (*rows_lut)[3]=malloc(32*3*sizeof(C3D_LightLut));
   if(!rows_lut){if(timing)fclose(timing);goto done;}
   u64 build_start=svcGetSystemTick();
   for(unsigned row=0;row<32;row++)make_luts(rows_lut[row],4,row,0,11);
   if(timing)fprintf(timing,"build32_ms=%.6f\n",(svcGetSystemTick()-build_start)*1000.0/SYSCLOCK_ARM11);
   for(unsigned rows=1;rows<=32;rows=rows==1?16:rows*2){
    double cpu=0,gpu=0;unsigned samples=0;
    for(unsigned trial=0;trial<40;trial++){
     C3D_FrameBegin(0);C3D_FrameDrawOn(rt);
     u64 start=svcGetSystemTick();
     for(unsigned row=0;row<rows;row++){
      bind_luts(&env,rows_lut[row]);
      C3D_DrawArrays(GPU_TRIANGLES,0,6);
     }
     u64 end=svcGetSystemTick();C3D_FrameEnd(0);
     if(trial>=8){cpu+=(end-start)*1000.0/SYSCLOCK_ARM11;gpu+=C3D_GetDrawingTime();samples++;}
    }
    if(timing)fprintf(timing,"rows=%u samples=%u update_submit_ms=%.6f gpu_last_ms=%.6f\n",rows,samples,cpu/samples,gpu/samples);
   }
   free(rows_lut);if(timing)fclose(timing);
  }
  if(bits==8){
   FILE *sweep=fopen("sdmc:/3ds/BreadBox/lut-quantization.bin","wb");if(!sweep)goto done;
   C3D_LightEnvBind(&env);C3D_TexBind(0,&src);
   for(unsigned bias=0;bias<=16;bias++){
    for(int j=-128;j<128;j++)for(unsigned k=0;k<3;k++){
     unsigned value=j+128,q=value?(value*4096u+254u)/255u+bias:0;
     lut[k].data[j&255]=q>4095u?4095u:q;
    }
    bind_luts(&env,lut);C3D_FrameBegin(0);C3D_FrameDrawOn(rt);
    for(unsigned k=0;k<6;k++){C3D_TexEnv *te=C3D_GetTexEnv(k);C3D_TexEnvInit(te);C3D_TexEnvSrc(te,C3D_Both,k?GPU_PREVIOUS:GPU_FRAGMENT_SECONDARY_COLOR,0,0);C3D_TexEnvFunc(te,C3D_Both,GPU_REPLACE);}
    C3D_DrawArrays(GPU_TRIANGLES,0,6);C3D_FrameEnd(0);readback(&out,&rb);fwrite(rb.data,1,4096,sweep);
   }
   fclose(sweep);
  }
  video_gpu_idle();gba_gpu_detach_textures();C3D_TexDelete(&src);memset(&src,0,sizeof(src));
 }
 result=0;
done:
 if(f)fclose(f);if(bf)fclose(bf);if(pf)fclose(pf);video_gpu_idle();gba_gpu_restore_ui();
 if(peel)C3D_RenderTargetDelete(peel);if(rt)C3D_RenderTargetDelete(rt);if(src.data)C3D_TexDelete(&src);if(out.data)C3D_TexDelete(&out);if(peeled.data)C3D_TexDelete(&peeled);if(rb.data)C3D_TexDelete(&rb);
 for(unsigned k=0;k<4;k++)if(blend[k].data)C3D_TexDelete(&blend[k]);
 if(v)linearFree(v);if(pgok)shaderProgramFree(&pg);if(d)DVLB_Free(d);gba_gpu_restore_ui();
 log_printf("gba-lut-probe: result=%d\n",result);return result;
}
#endif
