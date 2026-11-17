#ifndef BB_PICTURE_FAKE_C2D_H
#define BB_PICTURE_FAKE_C2D_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;
#define GPU_RGB565 0
#define GPU_NEAREST 0
#define GPU_LINEAR 1
#define GPU_TEXFACE_2D 0
#define GPU_ALWAYS 0
#define GPU_WRITE_COLOR 1
#define SYSCLOCK_ARM11 268123480u
#define BB_GBA_GPU_STATE_H
struct C3D_Tex {void *data;unsigned size,filter;bool vram;};
typedef struct C3D_Tex C3D_Tex;
typedef struct {struct {void *colorBuf;}frameBuf;} C3D_RenderTarget;
typedef struct {unsigned width,height;float left,top,right,bottom;} Tex3DS_SubTexture;
typedef struct {C3D_Tex *tex;Tex3DS_SubTexture *subtex;} C2D_Image;
typedef struct {float alpha;} C2D_ImageTint;
bool C3D_TexInit(C3D_Tex *,unsigned,unsigned,int);
bool C3D_TexInitVRAM(C3D_Tex *,unsigned,unsigned,int);
void C3D_TexDelete(C3D_Tex *);
void C3D_TexSetFilter(C3D_Tex *,unsigned,unsigned);
C3D_RenderTarget *C3D_RenderTargetCreateFromTex(C3D_Tex *,int,unsigned,int);
void C3D_RenderTargetDelete(C3D_RenderTarget *);
void C3D_FrameSplit(unsigned);
float C3D_GetDrawingTime(void);
void C3D_DepthTest(bool,unsigned,unsigned);
void GX_TextureCopy(const void *,unsigned,void *,unsigned,unsigned,unsigned);
void GSPGPU_FlushDataCache(const void *,unsigned);
void C2D_Prepare(void);
void C2D_SceneBegin(C3D_RenderTarget *);
void C2D_DrawImageAt(C2D_Image,float,float,float,const C2D_ImageTint *,float,float);
void C2D_AlphaImageTint(C2D_ImageTint *,float);
void C2D_Flush(void);
bool gba_gpu_ui_init(void);
void gba_gpu_detach_textures(void);
void gba_gpu_restore_ui(void);
bool gspHasGpuRight(void);
u64 svcGetSystemTick(void);
#endif
