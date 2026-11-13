#ifndef GBA_BOXART_H
#define GBA_BOXART_H
#include "boxart.h"
/* File stem (ZIP member when present), then cartridge code -> No-Intro names. */
int gba_boxart_candidates(const char *path, const char *code, char out[][BOXART_NAME], int max);
void gba_boxart_stem(const char *path, char *out, size_t len);
#endif
