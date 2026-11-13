/* GPL-2.0-or-later. GBA-only experimental renderer. */
#ifndef BB_GBA_INDEX_GPU_H
#define BB_GBA_INDEX_GPU_H
#include <citro2d.h>
bool gba_index_gpu_init(unsigned mode);
void gba_index_gpu_exit(void);
void gba_index_gpu_fence(void);
bool gba_index_gpu_render(const void *cpu,size_t pitch,const u16 *right);
C3D_Tex *gba_index_gpu_texture(unsigned eye);
#endif
