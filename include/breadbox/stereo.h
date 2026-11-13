// Real 3D from the core's depth map (stereo.c)
#pragma once
#include <citro2d.h>
#include <math.h>
#include <stdbool.h>

// stereo.c - real 3D from the core's depth map
bool stereo_init(void);
void stereo_exit(void);
bool stereo_prepare(float max_px);
void stereo_draw(C3D_Tex *tex, float x0, float dw, unsigned frame_w, unsigned frame_h, int eye, float max_px);

void stereo_draw_preview(float x0, float y0, float cell);
const char *stereo_stats(void);
