/* GPL-2.0-or-later. RGB565 picture tables, built only when the look changes. */
#ifndef BB_GBA_COLOR_H
#define BB_GBA_COLOR_H
#include <stddef.h>
#include <stdint.h>
enum { GBA_COLOR_ORIGINAL, GBA_COLOR_CORRECTED, GBA_COLOR_SOFT, GBA_COLOR_BRIGHT, GBA_COLOR_COUNT };
extern const char *const gba_color_names[GBA_COLOR_COUNT];
const char *gba_color_name(int color);
void gba_color_table(uint16_t table[65536], int color);
/* Color lookup replaces the copy inside the texture upload: no extra image pass. */
void gba_color_upload(uint16_t *out, const void *pixels, size_t pitch, const uint16_t table[65536]);
#endif
