/* GPL-2.0-or-later. Private binding to the established GBA frame state. */
#ifndef BB_GBA_PICTURE_HOST_H
#define BB_GBA_PICTURE_HOST_H
#include <citro2d.h>
#include "libretro.h"
#include "gba_picture.h"
typedef struct {
 retro_video_refresh_t original;
 C3D_Tex *texture[2];
 u16 **right_pixels;
 bool *texture_ready, *probe, *picture_ready, *stereo_frame, *indexed_picture;
 bool *boot_hold, *failed;
 u64 *last_video;
 unsigned *nulls, *blank, *repeat, *lock_hold, *drawn;
 u32 *hash;
 void (*track)(const void *,size_t);
 void (*sample)(const void *,size_t);
} gba_picture_host;
extern const gba_picture_host gba_picture_bridge;
#endif
