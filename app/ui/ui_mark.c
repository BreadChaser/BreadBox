/* The selection highlighter (ui_mark.h): one routine for every notebook card and the room's lists.
 * Three textured quads from one 8 KiB L8 texture, multiplied over what is already drawn:
 * dst = dst x (1 - mask x ink), ink = 1 - marker yellow, so ruled lines, the margin and the text show through. */
#include <string.h>
#include "ui_mark.h"
#include "ui_mark_masks.h"
#include "cold_code.h"
#ifdef HOST_CITRO2D_H
/* Host tests: record what would be drawn (tests read host_marks). */
u32 ui_mark_ticks;
ui_mark_geo host_marks[8];
float host_mark_strength[8];
int host_mark_n;
PSX3D_COLD void ui_mark_row(float x0, float x1, float line, int row, float progress, float strength, float z)
{
	(void)z;
	if (host_mark_n >= 8 || strength <= 0) return;
	ui_mark_layout(x0, x1, line, row, progress, &host_marks[host_mark_n]);
	host_mark_strength[host_mark_n++] = strength;
}
#else
#include <3ds.h>
#include <citro2d.h>

static C3D_Tex tex;
static bool tex_ok, tex_failed;

static inline u32 texel(u32 x, u32 y, u32 w)
{
	return (((y >> 3) * (w >> 3) + (x >> 3)) << 6) |
		(x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}

PSX3D_COLD static bool load(void)
{
	if (tex_ok || tex_failed) return tex_ok;
	const u32 w = UI_MARK_TEX_W, h = UI_MARK_TEX_H * UI_MARK_VARIANTS;
	if (!C3D_TexInit(&tex, w, h, GPU_L8)) { tex_failed = true; return false; }
	u8 *d = tex.data;
	for (u32 v = 0; v < UI_MARK_VARIANTS; v++)
		for (u32 y = 0; y < UI_MARK_TEX_H; y++)
			for (u32 x = 0; x < w; x++)
				d[texel(x, v * UI_MARK_TEX_H + y, w)] = ui_mark_masks[v][y][x];
	svcStoreProcessDataCache(CUR_PROCESS_HANDLE, (u32)tex.data, tex.size);
	C3D_TexSetFilter(&tex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	tex_ok = true;
	return true;
}

static Tex3DS_SubTexture slice(int variant, float u0, float u1)
{
	const float w = UI_MARK_TEX_W, h = UI_MARK_TEX_H * UI_MARK_VARIANTS, y = variant * UI_MARK_TEX_H;
	// half a texel in from the cell's top and bottom: linear filtering never reads the next stroke
	return (Tex3DS_SubTexture){ (u16)(u1 - u0), UI_MARK_TEX_H, u0 / w, 1.0f - (y + .5f) / h, u1 / w, 1.0f - (y + UI_MARK_TEX_H - .5f) / h };
}

u32 ui_mark_ticks;   // the last stroke's CPU time (dev switch framelog prints it)
PSX3D_COLD void ui_mark_row(float x0, float x1, float line, int row, float progress, float strength, float z)
{
	u64 t0 = svcGetSystemTick();
	if (strength <= 0 || !load()) return;
	ui_mark_geo g;
	ui_mark_layout(x0, x1, line, row, progress, &g);
	if (!g.n) return;
	if (strength > 1) strength = 1;
	C2D_ImageTint tint;
	C2D_PlainImageTint(&tint, C2D_Color32f(UI_MARK_INK_R * strength, UI_MARK_INK_G * strength, UI_MARK_INK_B * strength, 1), 1);
	C2D_Flush();
	C2D_SetTintMode(C2D_TintMult);   // L8 mask x ink: the amount each channel darkens
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ZERO, GPU_ONE_MINUS_SRC_COLOR, GPU_ZERO, GPU_ONE);
	float angle = atanf(g.tilt / (g.x1 - g.x0)), mid = (g.top + g.bottom) / 2;
	for (int i = 0; i < g.n; i++) {
		Tex3DS_SubTexture st = slice(g.variant, g.s[i].u0, g.s[i].u1);   // read while the quad is queued
		// every slice turns about the stroke's start, so the tilted pieces stay joined
		C2D_DrawParams p = { { g.x0, mid, g.s[i].x1 - g.s[i].x0, g.bottom - g.top }, { g.x0 - g.s[i].x0, mid - g.top }, z, angle };
		C2D_DrawImage((C2D_Image){ &tex, &st }, &p, &tint);
	}
	C2D_Flush();
	C2D_SetTintMode(C2D_TintSolid);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
	ui_mark_ticks = (u32)(svcGetSystemTick() - t0);
}
#endif
