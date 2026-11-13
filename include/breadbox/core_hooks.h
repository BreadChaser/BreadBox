// PS1 core entry points and counters the frontend uses (real 3D, profiling, renderer)
#pragma once
#include <stdbool.h>
#include <stdint.h>

// core (patches/0002): real 3D renders each eye into its own VRAM
void psx3d_set_strength(float px);
void psx3d_set_half3d(int on);
// core profiling (libpcsxcore/psx3d_prof.h)
extern unsigned long long psx3d_prof_t[6];
extern unsigned int psx3d_prof_c[6];
extern int psx3d_prof_on;
void new_dynarec_print_stats(void);

extern unsigned pica_prims;
extern bool pica_texmask, pica_texmemo;
extern int psx3d_skipgate;

extern void (*pica_pmu)(int end);

void psx3d_stats(float *zc, int *n3d, int *n2d, int *dmin, int *dmax, int *zmin, int *zmax);
const uint16_t *psx3d_right_get(int *x, int *y, int *w, int *h, int *rgb24);

void psx3d_depth_frame(void);   // in the core
