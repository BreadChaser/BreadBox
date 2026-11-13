/* GPL-2.0-or-later. Native L4/L8 palettes and contributor composition on PICA. */
#include <string.h>
#include <sys/types.h>
#include "app_log.h"
#include "platform_video.h"
#include "index_gpu.h"
#include "palette_lut.h"
#include "gpu_state.h"
#include <stdlib.h>
#include <string.h>
#include "gba_index_shbin.h"
#include "../../gba-core/indexed.h"
#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif
extern void gpsp_gba_index_config(unsigned,void*,void*);
extern const struct GixFrame *gpsp_gba_index_frame(void);
extern const uint16_t *gpsp_gba_index_updates(unsigned,unsigned*);
extern struct GixStats gpsp_gba_index_stats(void);
extern void video_gpu_wait_previous(void);
struct Vertex {float x,y,z,w,u,v;};
struct Packet {unsigned key,start,count,bank,layer,rank,semi,obj;};
static C3D_Tex raw[2],image[2],present[2],aux[8],storestage,readback;
static C3D_RenderTarget *eyes[2],*targets[8];
static shaderProgram_s program;static DVLB_s *dvlb;static unsigned mode,ready;
static struct Vertex *vertices;static unsigned nvertices,np,fullfirst;
static struct Packet packets[GIX_QUADS];static unsigned sortq[GIX_QUADS];
static const struct GixFrame *frame;
static C3D_LightEnv env;static C3D_Light light;
struct LutCache {C3D_LightLut lut[3];unsigned ver,key;};
static struct LutCache (*luts)[6];static struct LutCache *bound;static unsigned role;
static int depthloc;static bool native_targets;
extern bool video_stereo_targets(bool);
static uint64_t draws,lut_updates,verify_frames,verify_errors,geometry_ticks,lut_ticks,render_ticks,lut_builds;
/* FrameDrawOn resets the viewport and disables scissoring. Reapply the
 * journal band/window after every attachment change, including resolve. */
static unsigned clip_mode,clip_l,clip_t,clip_r,clip_b;
static void clip(unsigned mode,unsigned l,unsigned t,unsigned r,unsigned b){
 clip_mode=mode;clip_l=l;clip_t=t;clip_r=r;clip_b=b;
 C3D_SetScissor((GPU_SCISSORMODE)mode,l,t,r,b);
}
static void on(C3D_RenderTarget *target){
 C3D_FrameDrawOn(target);
 C3D_SetScissor((GPU_SCISSORMODE)clip_mode,clip_l,clip_t,clip_r,clip_b);
}
static void state(void){
 gba_gpu_detach_textures();
 C3D_BindProgram(&program);C3D_CullFace(GPU_CULL_NONE);C3D_DepthMap(true,-1,0);
 C3D_SetViewport(0,0,256,256);clip(GPU_SCISSOR_DISABLE,0,0,0,0);
 C3D_AttrInfo *a=C3D_GetAttrInfo();AttrInfo_Init(a);AttrInfo_AddLoader(a,0,GPU_FLOAT,4);AttrInfo_AddLoader(a,1,GPU_FLOAT,2);
 C3D_BufInfo *b=C3D_GetBufInfo();BufInfo_Init(b);BufInfo_Add(b,vertices,sizeof(*vertices),2,0x10);
 C3D_Mtx projection;Mtx_Ortho(&projection,0,256,256,0,0,1,true);C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,shaderInstanceGetUniformLocation(program.vertexShader,"projection"),&projection);
 C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
 C3D_TexEnvBufUpdate(C3D_Both,0);
 for(unsigned i=0;i<6;i++){C3D_TexEnv *e=C3D_GetTexEnv(i);C3D_TexEnvInit(e);C3D_TexEnvSrc(e,C3D_Both,GPU_PREVIOUS,0,0);C3D_TexEnvFunc(e,C3D_Both,GPU_REPLACE);}
}
static void z(unsigned rank,unsigned obj){C3D_FVUnifSet(GPU_VERTEX_SHADER,depthloc,-(1.0f-(rank+obj/1024.0f)/32.0f),0,0,0);}
static void draw(unsigned first,unsigned count){C3D_DrawArrays(GPU_TRIANGLES,first,count);draws++;}
static void full(void){C3D_FVUnifSet(GPU_VERTEX_SHADER,depthloc,0,0,0,0);draw(fullfirst,6);}
static void rgba(unsigned c,unsigned rgb[3]){rgb[0]=(c&31)*8;rgb[1]=(c>>5&31)*8;rgb[2]=(c>>10&31)*8;}
static void palette(const struct GixBand *b,unsigned bank,unsigned variant,unsigned factor,unsigned target){
 if(variant==3&&(!target||!(b->regs[40]&128)))variant=0;
 unsigned slot=variant==0?0:variant==3?5:variant+(role?2:0),key=factor|((b->regs[40]&0xc0)<<8)|(b->regs[42]<<16);
 if(!variant)key=0;
 struct LutCache *cache=&luts[bank][slot];C3D_LightLut *lut=cache->lut;
 if(cache->ver!=b->pver[bank]||cache->key!=key){
 uint64_t begin=svcGetSystemTick();lut_builds++;cache->ver=b->pver[bank];cache->key=key;
 for(int j=-128;j<128;j++){
  unsigned i=bank<32?(unsigned)((j+128)*15.0f/256.0f+0.5f):(unsigned)(j+128),base=bank<32?bank*16:bank==33?256:0;
  unsigned rgb[3];rgba(b->pal[base+i],rgb);
  for(unsigned k=0;k<3;k++){
   unsigned v=rgb[k],unit=k==1?4:8,limit=k==1?63:31,n=v/unit;
   if(variant==1)v=((n*factor)>>4)*unit;
   if(variant==2)v=((n*factor)&15)*16;
   if(variant==3&&target){unsigned e=b->regs[40]>>6&3,f=b->regs[42]&31;if(f>16)f=16;if(e==2)v=(((limit*f)>>4)+((n*(16-f))>>4))*unit;if(e==3)v=((n*(16-f))>>4)*unit;}
   lut[k].data[j&255]=gba_palette_lut_entry(v);
  }
 }
 lut_ticks+=svcGetSystemTick()-begin;
 }
 C3D_LightEnvBind(&env);if(bound==cache)return;bound=cache;
 C3D_LightEnvLut(&env,GPU_LUT_RR,GPU_LUTINPUT_NV,true,&lut[0]);C3D_LightEnvLut(&env,GPU_LUT_RG,GPU_LUTINPUT_NV,true,&lut[1]);C3D_LightEnvLut(&env,GPU_LUT_RB,GPU_LUTINPUT_NV,true,&lut[2]);lut_updates++;
}
static unsigned key(const struct GixQuad *q){return q->bank*132+(q->layer<4?q->layer:4+q->obj);}
static int compare(const void *a,const void *b){unsigned x=*(const unsigned*)a,y=*(const unsigned*)b;return (int)key(&frame->quads[x])-(int)key(&frame->quads[y]);}
static void quad(float x,float y,float w,float h,float u,float v,float u1,float v1){
 struct Vertex q[6]={{x,y,0,1,u,v},{x,y+h,0,1,u,v1},{x+w,y,0,1,u1,v},{x+w,y,0,1,u1,v},{x,y+h,0,1,u,v1},{x+w,y+h,0,1,u1,v1}};
 memcpy(vertices+nvertices,q,sizeof(q));nvertices+=6;
}
static void geometry(const struct GixBand *b,int eye){
 uint64_t begin=svcGetSystemTick();
 fullfirst=nvertices;quad(0,0,256,256,0,1,1,0);np=0;
 for(unsigned i=0;i<b->nq;i++)sortq[i]=b->firstq+i;
 qsort(sortq,b->nq,sizeof(*sortq),compare);
 for(unsigned i=0;i<b->nq;i++){
  const struct GixQuad *q=&frame->quads[sortq[i]];unsigned k=key(q);
  if(!np||packets[np-1].key!=k){packets[np++]=(struct Packet){k,nvertices,0,q->bank,q->layer,q->rank,q->semi,q->obj};}
  int shift=q->layer<4?b->half[q->layer]:q->layer==5?0:b->half[4+(q->rank-1)/5];
  float u=(q->slot%64)*8/512.0f,v=1-(q->slot/64)*8/512.0f,u1=u+8/512.0f,v1=v-8/512.0f;
  if(q->flip&1){float t=u;u=u1;u1=t;}if(q->flip&2){float t=v;v=v1;v1=t;}
  quad(q->x+shift*eye,q->y,8,8,u,v,u1,v1);packets[np-1].count+=6;
 }
 GSPGPU_FlushDataCache(vertices+fullfirst,(nvertices-fullfirst)*sizeof(*vertices));geometry_ticks+=svcGetSystemTick()-begin;
}
/* RGB metadata pass leaves alpha as the native index/winner test, so index0
 * is rejected before depth/stencil. OBJ is flattened by a separate ID target. */
static void packet(const struct GixBand *b,const struct Packet *p,unsigned variant,unsigned factor,unsigned metadata,unsigned mask){
 if(p->layer==5||!(mask&(1u<<p->layer)))return;
 C3D_TexBind(0,&raw[p->bank>=32]);
 unsigned first=b->regs[40]&(1u<<p->layer);
 if(metadata){C3D_LightEnvBind(NULL);bound=NULL;}else palette(b,p->bank,variant,factor,first&&!p->semi&&(mask&32));
 C3D_TexEnv *e=C3D_GetTexEnv(0);C3D_TexEnvInit(e);C3D_TexEnvSrc(e,C3D_RGB,metadata?GPU_CONSTANT:GPU_FRAGMENT_SECONDARY_COLOR,0,0);C3D_TexEnvFunc(e,C3D_RGB,GPU_REPLACE);
 if(metadata){unsigned eligible=metadata==1?(p->semi||((mask&32)&&(b->regs[40]&64)&&!(b->regs[40]&128)&&first)):(b->regs[40]&(256u<<p->layer));C3D_TexEnvColor(e,eligible?0xffffffff:0xff000000);}
 C3D_TexEnvSrc(e,C3D_Alpha,p->layer==4?GPU_TEXTURE2:GPU_TEXTURE0,0,0);C3D_TexEnvOpAlpha(e,GPU_TEVOP_A_SRC_R,GPU_TEVOP_A_SRC_ALPHA,GPU_TEVOP_A_SRC_ALPHA);C3D_TexEnvFunc(e,C3D_Alpha,GPU_REPLACE);
 C3D_AlphaTest(true,p->layer==4?GPU_EQUAL:GPU_GREATER,p->layer==4?p->obj+1:0);z(p->rank,0);draw(p->start,p->count);
}
static void objmask(const struct GixBand *b){
 on(targets[0]);C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);C3D_StencilTest(false,GPU_ALWAYS,0,255,0);C3D_LightEnvBind(NULL);
 for(unsigned i=0;i<np;i++){const struct Packet *p=&packets[i];if(p->layer!=4)continue;
  C3D_TexBind(0,&raw[p->bank>=32]);C3D_TexEnv *e=C3D_GetTexEnv(0);C3D_TexEnvInit(e);C3D_TexEnvSrc(e,C3D_RGB,GPU_CONSTANT,0,0);C3D_TexEnvColor(e,0xff000000|(p->obj+1));C3D_TexEnvFunc(e,C3D_RGB,GPU_REPLACE);
  C3D_TexEnvSrc(e,C3D_Alpha,GPU_TEXTURE0,0,0);C3D_TexEnvOpAlpha(e,GPU_TEVOP_A_SRC_R,GPU_TEVOP_A_SRC_ALPHA,GPU_TEVOP_A_SRC_ALPHA);C3D_TexEnvFunc(e,C3D_Alpha,GPU_REPLACE);
  C3D_AlphaTest(true,GPU_GREATER,0);z(p->rank,p->obj);draw(p->start,p->count);
 }
 C3D_TexBind(2,&aux[0]);
}
static void backdrop(const struct GixBand *b,unsigned variant,unsigned factor,unsigned meta,unsigned mask){
 unsigned rgb[3];rgba(b->pal[0],rgb);
 for(unsigned k=0;k<3;k++){unsigned unit=k==1?4:8,n=rgb[k]/unit,limit=k==1?63:31;
  if(variant==1)rgb[k]=((n*factor)>>4)*unit;if(variant==2)rgb[k]=((n*factor)&15)*16;
  if(variant==3&&(mask&32)&&(b->regs[40]&32)){unsigned f=b->regs[42]&31,e=b->regs[40]>>6&3;if(f>16)f=16;if(e==2)rgb[k]=(((limit*f)>>4)+((n*(16-f))>>4))*unit;if(e==3)rgb[k]=((n*(16-f))>>4)*unit;}
 }
 unsigned c=rgb[0]|rgb[1]<<8|rgb[2]<<16|0xff000000;
 if(meta)c=(meta==1?((mask&32)&&(b->regs[40]&0xc0)==64&&(b->regs[40]&32)):(b->regs[40]&8192))?0xffffffff:0xff000000;
 C3D_LightEnvBind(NULL);bound=NULL;C3D_TexEnv *e=C3D_GetTexEnv(0);C3D_TexEnvInit(e);C3D_TexEnvSrc(e,C3D_Both,GPU_CONSTANT,0,0);C3D_TexEnvColor(e,c);C3D_TexEnvFunc(e,C3D_Both,GPU_REPLACE);C3D_AlphaTest(false,GPU_ALWAYS,0);z(22,0);draw(fullfirst,6);
}
/* pass0 establishes top stencil;1/2 top weighted;3 top eligibility;
 *4 establishes second depth;5 remainder;6 second eligibility. */
static void scene(const struct GixBand *b,unsigned pass,unsigned mask){
 /* Resolve borrows units1/2. Restore OBJ IDs and detach unused unit1:
  * leaving aux5 bound while drawing into aux5 creates framebuffer feedback,
  * even though this scene does not consume unit1 in its TEV stages. */
 C3D_TexBind(1,&gba_ui_neutral);C3D_TexBind(2,&aux[0]);
 role=pass>=4;
 unsigned variant=pass==0?3:pass==1||pass==4?1:pass==2||pass==5?2:0;
 unsigned factor=MIN(16,pass<4?b->regs[41]&31:b->regs[41]>>8&31),metadata=pass==3?1:pass==6?2:0;
 C3D_DepthTest(true,pass==0||pass==4?GPU_GREATER:GPU_EQUAL,pass==0||pass==4?GPU_WRITE_ALL:GPU_WRITE_COLOR);
 C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,pass==0?GPU_STENCIL_REPLACE:GPU_STENCIL_KEEP);
 for(unsigned i=0;i<np;i++){const struct Packet *p=&packets[i];C3D_StencilTest(true,pass==0?GPU_ALWAYS:pass<4?GPU_EQUAL:GPU_GREATER,p->rank,255,pass==0?255:0);packet(b,p,variant,factor,metadata,mask);}
 C3D_StencilTest(true,pass==0?GPU_ALWAYS:pass<4?GPU_EQUAL:GPU_GREATER,22,255,pass==0?255:0);backdrop(b,variant,factor,metadata,mask);
}
static void resolve(C3D_RenderTarget *out){
 /* aux1/2 A quotient/remainder, aux4/5 B quotient/remainder;
  * aux3 eligibility already multiplied with aux6. */
 on(out);C3D_LightEnvBind(NULL);C3D_StencilTest(false,GPU_ALWAYS,0,255,0);C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
 C3D_TexBind(0,&aux[1]);C3D_TexBind(1,&aux[4]);C3D_TexBind(2,&aux[3]);
 C3D_TexEnv *e=C3D_GetTexEnv(0);C3D_TexEnvInit(e);C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE0,GPU_TEXTURE1,0);C3D_TexEnvFunc(e,C3D_RGB,GPU_ADD);C3D_TexEnvSrc(e,C3D_Alpha,GPU_TEXTURE2,0,0);C3D_TexEnvOpAlpha(e,GPU_TEVOP_A_SRC_R,GPU_TEVOP_A_SRC_ALPHA,GPU_TEVOP_A_SRC_ALPHA);C3D_TexEnvFunc(e,C3D_Alpha,GPU_REPLACE);C3D_AlphaTest(true,GPU_GREATER,0);full();
 C3D_TexBind(0,&aux[2]);C3D_TexBind(1,&aux[5]);
 for(unsigned k=0;k<3;k++){
  e=C3D_GetTexEnv(0);C3D_TexEnvInit(e);C3D_TexEnvSrc(e,C3D_RGB,GPU_CONSTANT,0,0);C3D_TexEnvColor(e,k==1?0x04040404:0x08080808);C3D_TexEnvFunc(e,C3D_RGB,GPU_REPLACE);
  C3D_TexEnvSrc(e,C3D_Alpha,GPU_TEXTURE0,GPU_TEXTURE1,0);C3D_TexEnvOpAlpha(e,(GPU_TEVOP_A)(GPU_TEVOP_A_SRC_R+k*2),(GPU_TEVOP_A)(GPU_TEVOP_A_SRC_R+k*2),GPU_TEVOP_A_SRC_ALPHA);C3D_TexEnvFunc(e,C3D_Alpha,GPU_ADD);
  C3D_TexEnv *gate=C3D_GetTexEnv(1);C3D_TexEnvSrc(gate,C3D_Alpha,GPU_PREVIOUS,GPU_TEXTURE2,0);C3D_TexEnvOpAlpha(gate,GPU_TEVOP_A_SRC_ALPHA,GPU_TEVOP_A_SRC_R,GPU_TEVOP_A_SRC_ALPHA);C3D_TexEnvFunc(gate,C3D_Alpha,GPU_MODULATE);
  C3D_AlphaTest(true,GPU_GREATER,254);C3D_DepthTest(false,GPU_ALWAYS,(GPU_WRITEMASK)(GPU_WRITE_RED<<k));C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ONE,GPU_ONE,GPU_ZERO);full();
 }
 C3D_TexEnvInit(C3D_GetTexEnv(1));C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
}
static int inwy(unsigned y,unsigned v){unsigned t=v>>8,b=v&255;if(t>227)return 0;if(b>227)return 1;return t>b?y>t||y<=b:y>=t&&y<b;}
static unsigned region(const struct GixBand *b,int x,int y){unsigned d=b->regs[0];if(!(d&0xe000))return 63;for(unsigned w=0;w<2;w++)if((d&(8192u<<w))&&inwy(y,b->regs[34+w])){unsigned l=b->regs[32+w]>>8,r=b->regs[32+w]&255;if(l!=r&&(l<r?(x>=(int)l&&x<(int)r):(x<(int)r||x>=(int)l)))return b->regs[36]>>(8*w)&63;}return b->regs[37]&63;}
static void band(const struct GixBand *b,C3D_RenderTarget *out,int eye){
 geometry(b,eye);state();
 if(b->regs[0]&128){on(out);clip(GPU_SCISSOR_NORMAL,0,256-b->last,240,256-b->first);C3D_LightEnvBind(NULL);C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_StencilTest(false,GPU_ALWAYS,0,255,0);C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);C3D_TexEnv *e=C3D_GetTexEnv(0);C3D_TexEnvInit(e);C3D_TexEnvSrc(e,C3D_Both,GPU_CONSTANT,0,0);C3D_TexEnvColor(e,0xffffffff);C3D_TexEnvFunc(e,C3D_Both,GPU_REPLACE);full();return;}
 clip(GPU_SCISSOR_NORMAL,0,256-b->last,240,256-b->first);objmask(b);
 int xb[6]={0,240},yb[6]={b->first,b->last};unsigned nx=2,ny=2;
 for(unsigned w=0;w<2;w++){unsigned h=b->regs[32+w],v=b->regs[34+w];if(b->regs[0]&(8192u<<w)){int l=MIN(240,h>>8),r=MIN(240,h&255),t=v>>8,bot=v&255;xb[nx++]=l;xb[nx++]=r;if(t>(int)b->first&&t<(int)b->last)yb[ny++]=t;if(bot>(int)b->first&&bot<(int)b->last)yb[ny++]=bot;}}
 for(unsigned i=0;i<nx;i++)for(unsigned j=i+1;j<nx;j++)if(xb[i]>xb[j]){int v=xb[i];xb[i]=xb[j];xb[j]=v;}
 for(unsigned i=0;i<ny;i++)for(unsigned j=i+1;j<ny;j++)if(yb[i]>yb[j]){int v=yb[i];yb[i]=yb[j];yb[j]=v;}
 for(unsigned yi=0;yi+1<ny;yi++)for(unsigned xi=0;xi+1<nx;xi++){
  if(xb[xi]==xb[xi+1]||yb[yi]==yb[yi+1])continue;unsigned mask=region(b,xb[xi],yb[yi]);if(!(mask&31))mask|=32;
  clip(GPU_SCISSOR_NORMAL,xb[xi],256-yb[yi+1],xb[xi+1],256-yb[yi]);
  on(out);C3D_DepthTest(true,GPU_ALWAYS,GPU_WRITE_DEPTH);C3D_StencilTest(false,GPU_ALWAYS,0,255,0);C3D_AlphaTest(false,GPU_ALWAYS,0);full();
  scene(b,0,mask);
  bool blend=((mask&32)&&(b->regs[40]&0xc0)==64);for(unsigned p=0;p<np;p++)blend|=packets[p].semi!=0;
  if(!blend)continue;
  for(unsigned pass=1;pass<=3;pass++){on(targets[pass]);scene(b,pass,mask);}
  on(out);C3D_DepthTest(true,GPU_ALWAYS,GPU_WRITE_DEPTH);C3D_StencilTest(false,GPU_ALWAYS,0,255,0);C3D_AlphaTest(false,GPU_ALWAYS,0);full();
  for(unsigned pass=4;pass<=6;pass++){on(targets[pass]);scene(b,pass,mask);}
  on(targets[7]);C3D_LightEnvBind(NULL);C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);C3D_StencilTest(false,GPU_ALWAYS,0,255,0);C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_TexBind(0,&aux[3]);C3D_TexBind(1,&aux[6]);
  C3D_TexEnv *e=C3D_GetTexEnv(0);C3D_TexEnvInit(e);C3D_TexEnvSrc(e,C3D_Both,GPU_TEXTURE0,GPU_TEXTURE1,0);C3D_TexEnvFunc(e,C3D_Both,GPU_MODULATE);full();
  /* Keep read/write targets disjoint when applying the mask. */
  C3D_Tex tmp=aux[3];aux[3]=aux[7];aux[7]=tmp;resolve(out);tmp=aux[3];aux[3]=aux[7];aux[7]=tmp;
 }
}
bool gba_index_gpu_init(unsigned requested){
 gba_index_gpu_exit();if(!requested||!gba_gpu_ui_init())return false;mode=requested;video_gpu_idle();
 if(!video_stereo_targets(true))goto fail;native_targets=true;
 dvlb=DVLB_ParseFile((u32*)gba_index_shbin,gba_index_shbin_size);if(!dvlb)goto fail;shaderProgramInit(&program);shaderProgramSetVsh(&program,&dvlb->DVLE[0]);depthloc=shaderInstanceGetUniformLocation(program.vertexShader,"depth");
 luts=calloc(34,sizeof(*luts));if(!luts)goto fail;
 vertices=linearAlloc((GIX_QUADS*12+GIX_BANDS*12)*sizeof(*vertices));if(!vertices)goto fail;
 for(unsigned i=0;i<2;i++){
  if(!C3D_TexInit(&raw[i],512,512,i?GPU_L8:GPU_L4)||!C3D_TexInitVRAM(&image[i],256,256,GPU_RGBA8)||!C3D_TexInitVRAM(&present[i],256,256,GPU_RGB565))goto fail;
  C3D_TexSetFilter(&present[i],GPU_NEAREST,GPU_NEAREST);
  C3D_TexSetFilter(&raw[i],GPU_NEAREST,GPU_NEAREST);C3D_TexSetFilter(&image[i],GPU_NEAREST,GPU_NEAREST);
  eyes[i]=C3D_RenderTargetCreateFromTex(&image[i],GPU_TEXFACE_2D,0,GPU_RB_DEPTH24_STENCIL8);if(!eyes[i])goto fail;
 }
 for(unsigned i=0;i<8;i++){
  if(!C3D_TexInitVRAM(&aux[i],256,256,GPU_RGBA8))goto fail;C3D_TexSetFilter(&aux[i],GPU_NEAREST,GPU_NEAREST);
  targets[i]=C3D_RenderTargetCreateFromTex(&aux[i],GPU_TEXFACE_2D,0,i==0?GPU_RB_DEPTH24_STENCIL8:-1);if(!targets[i])goto fail;
 }
 if(!C3D_TexInit(&storestage,256,256,GPU_RGB565))goto fail;
 if(mode==2&&!C3D_TexInit(&readback,256,256,GPU_RGB565))goto fail;
 C3D_LightEnvInit(&env);C3D_Material m={.specular1={1,1,1}};C3D_LightEnvMaterial(&env,&m);C3D_LightEnvBumpMode(&env,GPU_BUMP_AS_BUMP);C3D_LightEnvBumpSel(&env,0);C3D_LightEnvBumpNormalZ(&env,true);C3D_LightEnvClampHighlights(&env,false);C3D_LightInit(&light,&env);C3D_LightColor(&light,1,1,1);C3D_FVec pos=FVec4_New(0,0,1,0);C3D_LightPosition(&light,&pos);
 gpsp_gba_index_config(mode,raw[0].data,raw[1].data);ready=1;log_printf("gba-index: init mode=%u vram=%u linear=%u\n",mode,(unsigned)vramSpaceFree(),(unsigned)linearSpaceFree());return true;
fail:log_printf("gba-index: init FAILED vram=%u linear=%u\n",(unsigned)vramSpaceFree(),(unsigned)linearSpaceFree());gba_index_gpu_exit();return false;
}
void gba_index_gpu_fence(void){if(ready)video_gpu_wait_previous();}
C3D_Tex *gba_index_gpu_texture(unsigned eye){return &present[eye!=0];}
bool gba_index_gpu_render(const void *cpu,size_t pitch,const u16 *right){
 if(!ready)return false;uint64_t begin=svcGetSystemTick();nvertices=0;frame=gpsp_gba_index_frame();if(!frame||frame->fallback)return false;
 for(unsigned bits=4;bits<=8;bits+=4){unsigned n;const uint16_t *u=gpsp_gba_index_updates(bits,&n);unsigned bytes=bits==4?32:64;unsigned lo=GIX_SLOTS,hi=0;for(unsigned i=0;i<n;i++){if(u[i]<lo)lo=u[i];if(u[i]>=hi)hi=u[i]+1;}if(hi>lo)GSPGPU_FlushDataCache((u8*)raw[bits==8].data+lo*bytes,(hi-lo)*bytes);}
 for(unsigned eye=0;eye<(frame->stereo?2u:1u);eye++){
  for(unsigned i=1;i<8;i++){targets[i]->frameBuf.depthBuf=eyes[eye]->frameBuf.depthBuf;targets[i]->frameBuf.depthFmt=eyes[eye]->frameBuf.depthFmt;targets[i]->frameBuf.depthMask=eyes[eye]->frameBuf.depthMask;}
  /* GX clears are queued immediately, unlike deferred PICA commands. Keep
   * them after the preceding eye, and clear OBJ IDs once: disjoint scanline
   * bands must never erase a winner mask between its draw and later reads. */
  C3D_FrameSplit(0);
  C3D_RenderTargetClear(targets[0],C3D_CLEAR_ALL,0,0);
  C3D_RenderTargetClear(eyes[eye],C3D_CLEAR_ALL,0,0);
  for(unsigned i=1;i<8;i++)C3D_RenderTargetClear(targets[i],C3D_CLEAR_COLOR,0,0);
  for(unsigned bi=0;bi<frame->nb;bi++)band(&frame->bands[bi],eyes[eye],eye?1:-1);
  /* Discard unused RGBA8 bits on the GPU, then move each channel into
   * the interior of its normalized RGB565 storage bin. Both PICA and GX
   * pack normalized channels rather than doing the CPU's bit shifts. */
  state();on(eyes[eye]);C3D_LightEnvBind(NULL);
  C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_StencilTest(false,GPU_ALWAYS,0,255,0);
  C3D_TexEnv *storage=C3D_GetTexEnv(0);C3D_TexEnvSrc(storage,C3D_Both,GPU_CONSTANT,0,0);
  C3D_TexEnvColor(storage,0xfff8fcf8);C3D_ColorLogicOp(GPU_LOGICOP_AND);full();
  C3D_TexEnvColor(C3D_GetTexEnv(0),0x00030203);
  C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ONE,GPU_ONE,GPU_ONE);full();
  /* GX format conversion stores the exact RGB565 bits the CPU path uses.
   * PICA RGB565 framebuffer writes normalize by31/63 instead of discarding
   * low bits; use the GX transfer encoder, keeping output tiled for sampling. */
  C3D_FrameSplit(0);
  GX_DisplayTransfer(image[eye].data,GX_BUFFER_DIM(256,256),storestage.data,GX_BUFFER_DIM(256,256),
   GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565));
  GX_DisplayTransfer(storestage.data,GX_BUFFER_DIM(256,256),present[eye].data,GX_BUFFER_DIM(256,256),
   GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565)|GX_TRANSFER_OUT_TILED(1));
  if(mode==2){
   C3D_FrameEnd(0);C3D_FrameBegin(0);GX_TextureCopy(present[eye].data,0,readback.data,0,readback.size,8);C3D_FrameEnd(0);C3D_FrameBegin(0);GSPGPU_InvalidateDataCache(readback.data,readback.size);
   if(verify_frames<2){FILE *d=fopen(verify_frames?"sdmc:/3ds/BreadBox/index-gpu1.bin":"sdmc:/3ds/BreadBox/index-gpu0.bin","wb");if(d){fwrite(readback.data,1,readback.size,d);fclose(d);}d=fopen(verify_frames?"sdmc:/3ds/BreadBox/index-cpu1.bin":"sdmc:/3ds/BreadBox/index-cpu0.bin","wb");if(d){for(unsigned y=0;y<160;y++)fwrite((const char*)cpu+y*pitch,2,240,d);fclose(d);}}
   unsigned errors=0;for(unsigned y=0;y<160;y++)for(unsigned x=0;x<240;x++){
    unsigned yy=y,m=((yy/8)*32+x/8)*64+(x&1)+((yy&1)<<1)+((x&2)<<1)+((yy&2)<<2)+((x&4)<<2)+((yy&4)<<3);
    u16 got=((u16*)readback.data)[m],want=eye&&right?right[y*240+x]:*(const u16*)((const u8*)cpu+y*pitch+x*2);errors+=got!=want;
   }if(errors){FILE *d=fopen("sdmc:/3ds/BreadBox/index-error-gpu.bin","wb");if(d){fwrite(readback.data,1,readback.size,d);fclose(d);}d=fopen("sdmc:/3ds/BreadBox/index-error-cpu.bin","wb");if(d){for(unsigned y=0;y<160;y++)fwrite((const char*)cpu+y*pitch,2,240,d);fclose(d);}}
   if(errors){const char *names[]={"sdmc:/3ds/BreadBox/index-error-frame.bin","sdmc:/3ds/BreadBox/index-error-raw4.bin","sdmc:/3ds/BreadBox/index-error-raw8.bin"};const void *data[]={frame,raw[0].data,raw[1].data};size_t sizes[]={sizeof(*frame),raw[0].size,raw[1].size};for(unsigned i=0;i<3;i++){FILE *d=fopen(names[i],"wb");if(d){fwrite(data[i],1,sizes[i],d);fclose(d);}}}

   verify_errors+=errors;verify_frames++;if(errors||verify_frames%60==0)log_printf("gba-index-verify: frame=%llu eye=%u errors=%u total=%llu\n",(unsigned long long)verify_frames,eye,errors,(unsigned long long)verify_errors);
  }
 }
 C3D_LightEnvBind(NULL);clip(GPU_SCISSOR_DISABLE,0,0,0,0);C3D_StencilTest(false,GPU_ALWAYS,0,255,0);C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);gba_gpu_restore_ui();render_ticks+=svcGetSystemTick()-begin;return true;
}
void gba_index_gpu_exit(void){
 if(ready){struct GixStats s=gpsp_gba_index_stats();log_printf("gba-index: frames=%llu gpu=%llu fallback=%llu dirty=%llu upload=%llu bytes=%llu quads=%llu bands=%llu lut=%llu draws=%llu verify=%llu/%llu reasons=%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",s.frames,s.gpu,s.fallback,s.dirty,s.uploads,s.bytes,s.quads,s.bands,lut_updates,draws,verify_errors,verify_frames,s.reasons[0],s.reasons[1],s.reasons[2],s.reasons[3],s.reasons[4],s.reasons[5],s.reasons[6]);log_printf("gba-index-cost: shown=%llu consume_ms=%.6f swizzle_ms=%.6f list_ms=%.6f vertices_ms=%.6f lut_build_ms=%.6f render_ms=%.6f lut_builds=%llu\n",s.gpu+s.fallback,s.ticks[0]*1000.0/SYSCLOCK_ARM11,s.ticks[1]*1000.0/SYSCLOCK_ARM11,s.ticks[2]*1000.0/SYSCLOCK_ARM11,geometry_ticks*1000.0/SYSCLOCK_ARM11,lut_ticks*1000.0/SYSCLOCK_ARM11,render_ticks*1000.0/SYSCLOCK_ARM11,lut_builds);gpsp_gba_index_config(0,NULL,NULL);}
 video_gpu_idle();gba_gpu_restore_ui();
 for(unsigned i=1;i<8;i++)if(targets[i])targets[i]->frameBuf.depthBuf=NULL;
 for(unsigned i=0;i<8;i++){if(targets[i])C3D_RenderTargetDelete(targets[i]);targets[i]=NULL;if(aux[i].data)C3D_TexDelete(&aux[i]);memset(&aux[i],0,sizeof(aux[i]));}
 for(unsigned i=0;i<2;i++){if(present[i].data)C3D_TexDelete(&present[i]);memset(&present[i],0,sizeof(present[i]));if(eyes[i])C3D_RenderTargetDelete(eyes[i]);eyes[i]=NULL;if(image[i].data)C3D_TexDelete(&image[i]);if(raw[i].data)C3D_TexDelete(&raw[i]);memset(&image[i],0,sizeof(image[i]));memset(&raw[i],0,sizeof(raw[i]));}
 if(storestage.data)C3D_TexDelete(&storestage);memset(&storestage,0,sizeof(storestage));
 if(readback.data)C3D_TexDelete(&readback);memset(&readback,0,sizeof(readback));if(vertices)linearFree(vertices);vertices=NULL;if(dvlb){shaderProgramFree(&program);DVLB_Free(dvlb);}dvlb=NULL;free(luts);luts=NULL;bound=NULL;if(native_targets){video_stereo_targets(false);native_targets=false;}ready=mode=0;draws=lut_updates=verify_frames=verify_errors=geometry_ticks=lut_ticks=render_ticks=lut_builds=0;gba_gpu_restore_ui();
}
