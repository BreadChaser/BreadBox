/* GPL-2.0-or-later. Offscreen RGB565 mixing; no frontend surface or readback. */
#ifndef BB_GBA_PICTURE_MIX_H
#define BB_GBA_PICTURE_MIX_H
#include "gpu_state.h"
static inline void gba_picture_mix(C3D_RenderTarget *target,
 C3D_Tex *before,C3D_Tex *now)
{
 /* Render textures and uploaded tiles use the same row order. Tex3DS
  * reserves top < bottom for rotated atlas entries, not vertical flips. */
 Tex3DS_SubTexture sub={256,256,0,1,1,0};
 C2D_Prepare();C2D_SceneBegin(target);
 C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
 C2D_Image image={before,&sub};C2D_DrawImageAt(image,0,0,0,NULL,1,1);
 C2D_ImageTint half;C2D_AlphaImageTint(&half,0.5f);
 image.tex=now;C2D_DrawImageAt(image,0,0,0,&half,1,1);C2D_Flush();
 gba_gpu_restore_ui();
}
#endif
