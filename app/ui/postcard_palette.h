// Shared postcard colors, resolved before drawing from semantic theme roles.
#pragma once
#include <3ds.h>

typedef struct {
 u32 paper, edge, frame, ink, soft, stamp, cont, cork, board, wall, wall_low, mark, light, glow, shade;
 u32 pin[4], empty;
} pc_palette;
void pc_palette_get(pc_palette *palette);
