/* GPL-2.0-or-later. Return GBA's private PICA state to the UI contract. */
#ifndef BB_GBA_GPU_STATE_H
#define BB_GBA_GPU_STATE_H
#include <citro2d.h>
#include <string.h>
/* Process-lifetime binding: citro3d 1.7.1 dereferences NULL on units1/2.
 * Keep their harmless descriptor/data alive even after a GBA session exits. */
static C3D_Tex gba_ui_neutral;
static inline bool gba_gpu_ui_init(void){
 if(gba_ui_neutral.data)return true;
 if(!C3D_TexInit(&gba_ui_neutral,8,8,GPU_RGBA8))return false;
 memset(gba_ui_neutral.data,255,gba_ui_neutral.size);C3D_TexFlush(&gba_ui_neutral);
 return true;
}
static inline void gba_gpu_detach_textures(void){
 if(gba_ui_neutral.data)for(int i=0;i<3;i++)C3D_TexBind(i,&gba_ui_neutral);
}
static inline void gba_gpu_restore_ui(void){
 C3D_LightEnvBind(NULL);C3D_ProcTexBind(0,NULL);
 /* Bind all units before freeing any private render textures. */
 gba_gpu_detach_textures();
 C3D_TexEnvBufUpdate(C3D_Both,0);
 for(unsigned i=0;i<6;i++)C3D_TexEnvInit(C3D_GetTexEnv(i));
 C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0);
 C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_StencilTest(false,GPU_ALWAYS,0,255,0);
 C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP);
 C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);
 C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA);
 C2D_Prepare();
}
#endif
