// The PSX3D theme's backdrop, as PSX3D v0.5.2 drew it: a sunset sky, the banded sun, two palms and a grid
// that rolls toward the viewer. Every frame is a pure function of the system clock. Menu only: its storage
// (two small textures and the grid's tables) is made on first use and released before a game starts.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <citro2d.h>
#include "list_scene.h"
#include "theme.h"
#include "platform_video.h"
#include "scene_batch.h"
#include "cold_code.h"
#include "list_scene_palms.h"

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
// The picture's own colours (artwork, not UI roles): they stay with the scene in every later theme change.
static const struct {
    u32 sky[4], sun[4], grid[3], floor, floor_far, palm, palm_rim, sun_glow;
} ink = {
    .sky = {TH_RGB(40,8,80), TH_RGB(110,20,120), TH_RGB(230,40,120), TH_RGB(255,120,60)},
    .sun = {TH_RGB(255,236,110), TH_RGB(255,196,60), TH_RGB(255,120,60), TH_RGB(255,80,100)},
    .grid = {TH_RGB(150,60,255), TH_RGB(40,90,255), TH_RGB(255,70,200)},
    .floor = TH_RGB(16,2,44), .floor_far = TH_RGB(4,1,12), .palm = TH_RGB(18,22,52), .palm_rim = TH_RGB(255,120,90),
    .sun_glow = TH_RGB(255,60,130),
};
#define PALMS_W 512   // the palms at screen size (400x240): each texel the coverage of 2x2 traced pixels
#define PALMS_H 256
typedef struct {
    C3D_Tex radial, palms;
    Tex3DS_SubTexture radial_sub, palms_sub;
    bool palms_ok, palms_refused;
    float mesh_px[29][37], mesh_py[29][37], mesh_h[29][37], mesh_z[29];
    float mesh_rise[37], mesh_side[37];
    struct { float x, y, width; u32 colour; } sun[170];
    unsigned sun_count;
    u32 mesh_colour[29][37];
    // Exact world-depth keys: floating point cancellation can give adjacent
    // frames slightly different zw values even within one scrolling row.
    // Four entries retain those values without quantizing time or geometry.
    struct { float world[4], height[4][37]; unsigned valid, next; } mesh_wave[29];
} workspace_t;
static workspace_t *ws;
static bool additive, refused;   // refused: no memory for the scene this menu visit (not tried again every frame)
#define Z .1f

static PSX3D_COLD float clamp(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static PSX3D_COLD u32 alpha(u32 c, float a) { return (c & 0x00ffffff) | ((u32)(255 * clamp(a)) << 24); }
static PSX3D_COLD u32 mix(u32 a, u32 b, float k) {
    u32 c = 0;
    k = clamp(k);
    for (int i = 0; i < 3; i++)
        c |= (u32)((float)((a >> (8 * i)) & 255) +
                   ((int)((b >> (8 * i)) & 255) - (int)((a >> (8 * i)) & 255)) * k)
             << (8 * i);
    return c | 0xff000000;
}
static PSX3D_COLD void blend(bool add) {
    if (add == additive)
        return;
    C2D_Flush();
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, add ? GPU_ONE : GPU_ONE_MINUS_SRC_ALPHA,
                   GPU_SRC_ALPHA, add ? GPU_ONE : GPU_ONE_MINUS_SRC_ALPHA);
    additive = add;
}
static PSX3D_COLD void rect(float x, float y, float ww, float hh, u32 c) { scene_rectangle(x, y, Z, ww, hh, c, c, c, c); }
static PSX3D_COLD void grad(float x, float y, float ww, float hh, u32 a, u32 b) { scene_rectangle(x, y, Z, ww, hh, a, a, b, b); }
// A texel of a GPU texture (8x8 tiles, Morton order inside a tile).
static PSX3D_COLD unsigned texel(unsigned x, unsigned y, unsigned width) {
    return (((y >> 3) * (width >> 3) + (x >> 3)) << 6) | (x & 1) | ((y & 1) << 1) |
           ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}
// PSX3D v0.5.2's rolling landscape, at full detail on both models.
// X-only powers are prepared once; all hill occlusion and clipping remain.
#define MESH_COLS 37            // x = -18 .. 18
#define MESH_ROWS 28
#define MESH_DZ   1.4f
#define HORIZON   140.0f

static PSX3D_COLD const float *mesh_wave(int row, float world)
{
    for (unsigned k = 0; k < ws->mesh_wave[row].valid; k++)
        if (ws->mesh_wave[row].world[k] == world)
            return ws->mesh_wave[row].height[k];
    unsigned k = ws->mesh_wave[row].next;
    ws->mesh_wave[row].next = (k + 1) % 4;
    if (ws->mesh_wave[row].valid < 4) ws->mesh_wave[row].valid++;
    ws->mesh_wave[row].world[k] = world;
    for (int i = 0; i < MESH_COLS; i++) {
        float x = i - 18.0f;
        float n = 0.8f + 0.22f * sinf(x * 0.9f + world * 0.35f) +
                  0.12f * sinf(x * 1.7f - world * 0.5f);
        ws->mesh_wave[row].height[k][i] = ws->mesh_rise[i] * n;
    }
    return ws->mesh_wave[row].height[k];
}

// The theme's grid colours (near middle, sides, far), as numbers for mesh_colour.
typedef struct { int n[3], s[3], f[3]; } mesh_ink_t;

static PSX3D_COLD void mesh_ink(mesh_ink_t *k)
{
	for (int i = 0; i < 3; i++) {
		k->n[i] = (ink.grid[0] >> (8 * i)) & 255;
		k->s[i] = (ink.grid[1] >> (8 * i)) & 255;
		k->f[i] = (ink.grid[2] >> (8 * i)) & 255;
	}
}

static PSX3D_COLD u32 mesh_colour(const mesh_ink_t *k, float x, float depth_power, u8 alpha)
{
	float side = fabsf(x) / 9.0f;
	if (side > 1) side = 1;
	float t = ws->mesh_side[(int)(x+18)];
	float r = k->n[0] + (k->s[0] - k->n[0]) * t, g = k->n[1] + (k->s[1] - k->n[1]) * t,
	      b = k->n[2] + (k->s[2] - k->n[2]) * t;
	float p = depth_power * (1 - side) * 0.85f;
	r += (k->f[0] - r) * p; g += (k->f[1] - g) * p; b += (k->f[2] - b) * p;
	return TH_RGBA((u8)r, (u8)g, (u8)b, alpha);
}

// Line clipped to just beyond the screen. Far off-screen vertices (the
// nearest grid rows land thousands of pixels out) can make the GPU drop the
// whole line, so lines vanished before reaching the screen edges.
static inline __attribute__((always_inline)) void clip_line(C2Di_Context *batch, float x0, float y0, u32 c0, float x1, float y1, u32 c1, float w)
{
	const float xmin = -8, xmax = 408, ymin = -8, ymax = 248;
    // Trivial reject before the four clip divisions. This changes no emitted
    // endpoint: the original clipper rejects these segments as well.
    if ((x0 < xmin && x1 < xmin) || (x0 > xmax && x1 > xmax) ||
        (y0 < ymin && y1 < ymin) || (y0 > ymax && y1 > ymax)) return;
	float t0 = 0, t1 = 1, dx = x1 - x0, dy = y1 - y0;
    if (!(x0 >= xmin && x0 <= xmax && y0 >= ymin && y0 <= ymax &&
          x1 >= xmin && x1 <= xmax && y1 >= ymin && y1 <= ymax)) {
    float p[4] = { -dx, dx, -dy, dy }, q[4] = { x0 - xmin, xmax - x0, y0 - ymin, ymax - y0 };
	for (int i = 0; i < 4; i++) {
		if (p[i] == 0) {
			if (q[i] < 0) return;
			continue;
		}
		float r = q[i] / p[i];
		if (p[i] < 0) { if (r > t1) return; if (r > t0) t0 = r; }
		else { if (r < t0) return; if (r < t1) t1 = r; }
	}
    }
	float ax=x0+dx*t0, ay=y0+dy*t0, bx=x0+dx*t1, by=y0+dy*t1;
    float len=sqrtf((bx-ax)*(bx-ax)+(by-ay)*(by-ay));
    if (len < .0001f) return;
    float nx=-(by-ay)*w*.5f/len, ny=(bx-ax)*w*.5f/len;
    scene_solid_line(batch, ax-nx,ay-ny,bx-nx,by-ny,ax+nx,ay+ny,bx+nx,by+ny,c0,c1,.16f);
}

static PSX3D_COLD void draw_grid(float t)
{
	float (*px)[37] = ws->mesh_px, (*py)[37] = ws->mesh_py;
	float (*hgt)[37] = ws->mesh_h;
	float *rz = ws->mesh_z;
	const float cx = 200, f = 225;
	float zoff = t * 0.45f;                      // world units scrolled (slow drift)
	float phase = fmodf(zoff, MESH_DZ);

	for (int j = 0; j <= MESH_ROWS; j++) {
		// The nearest row must always land below the screen, or the floor
		// stops short of the bottom edge for part of each scroll step (at
		// z = 1.85 it projected to y = 194). It runs from 0.45 down past the
		// camera; clamped at 0.4 it still projects below the screen (y ~ 392),
		// so the lines toward it reach the bottom edge.
		float z = 0.45f - phase + j * MESH_DZ;
		if (z < 0.4f)
			z = 0.4f;
		rz[j] = z;
		float zw = z + zoff;
        const float *wave = mesh_wave(j, zw);
        float near = (z - 1.0f) / 6.0f;
        if (near > 1) near = 1;
        if (near < 0) near = 0;
        float scale = 0.35f + 0.65f * near;
		for (int i = 0; i < MESH_COLS; i++) {
			float x = i - 18.0f;
			float h = fabsf(x) <= 2.0f ? 0 : wave[i] * scale;
			hgt[j][i] = h;
			// far rows sit deeper behind the screen in 3D
			px[j][i] = cx + x / z * f + ui_depth(2.5f * (z - 1) / 40.0f - 0.5f);
			py[j][i] = HORIZON + (1.0f - h) / z * f * 0.45f;
		}
	}
	// hills hide the lines behind them in the floor's colour (the picture's, through the theme)
	u32 fillc = ink.floor;
	mesh_ink_t mesh;
	mesh_ink(&mesh);
    // A row has one depth power and each vertex has one RGB value. Keep
    // the original float operations; only reuse results formerly recomputed
    // for every endpoint. Alpha still belongs to the line's far/near edge.
    for (int j = 0; j <= MESH_ROWS; j++) {
        float d = (rz[j] - 1.0f) / 25.0f;
        if (d > 1) d = 1;
        if (d < 0) d = 0;
        float power = powf(d, 0.7f);
        // RGB depends on |x|; mirrored columns use exactly the same value.
        for (int i = 0; i <= MESH_COLS / 2; i++)
            ws->mesh_colour[j][MESH_COLS - 1 - i] =
                ws->mesh_colour[j][i] = mesh_colour(&mesh, i - 18.0f, power, 255);
    }
    // Worst case: every cell emits two triangles, every edge a quad.
    // Buffer space is checked once; when it cannot fit, preserve the old
    // per-primitive checks and truncation behaviour via the NULL fallback.
    C2Di_Context *batch = scene_solid_begin(
        MESH_ROWS*(MESH_COLS-1)*6 +
        (MESH_ROWS*(2*MESH_COLS-1)+MESH_COLS-1)*4,
        MESH_ROWS*(MESH_COLS-1)*6 +
        (MESH_ROWS*(2*MESH_COLS-1)+MESH_COLS-1)*6);
    (void)batch;
	// back to front: hill cells hide the lines behind them
	for (int j = MESH_ROWS - 1; j >= 0; j--) {
		for (int i = 0; i < MESH_COLS - 1; i++) {
			if (hgt[j][i] + hgt[j][i + 1] + hgt[j + 1][i] + hgt[j + 1][i + 1] < 0.01f)
				continue;   // flat floor: nothing to hide
            // All coordinates are finite. Compare the same four corners
            // directly instead of six general-purpose libm min/max calls.
            if ((px[j][i] < 0 && px[j][i + 1] < 0 &&
                 px[j + 1][i] < 0 && px[j + 1][i + 1] < 0) ||
                (px[j][i] > 400 && px[j][i + 1] > 400 &&
                 px[j + 1][i] > 400 && px[j + 1][i + 1] > 400))
                continue;
            scene_solid_cell(batch, px[j][i], py[j][i], px[j][i + 1], py[j][i + 1],
                px[j + 1][i + 1], py[j + 1][i + 1], px[j + 1][i], py[j + 1][i], fillc, .15f);
		}
		// this strip's lines: the far edge across, and the lines toward the viewer
		float zf = rz[j + 1];
		u8 fade = zf > 32 ? (u8)(255 * (40 - zf) / 8) : 255;   // fade in at the horizon
		for (int i = 0; i < MESH_COLS - 1; i++)
			clip_line(batch, px[j + 1][i], py[j + 1][i], (ws->mesh_colour[j + 1][i] & 0x00ffffff) | ((u32)fade << 24),
				px[j + 1][i + 1], py[j + 1][i + 1], (ws->mesh_colour[j + 1][i + 1] & 0x00ffffff) | ((u32)fade << 24), 1.2f);
		for (int i = 0; i < MESH_COLS; i++)
			clip_line(batch, px[j + 1][i], py[j + 1][i], (ws->mesh_colour[j + 1][i] & 0x00ffffff) | ((u32)fade << 24),
				px[j][i], py[j][i], ws->mesh_colour[j][i], 1.2f);
	}
	for (int i = 0; i < MESH_COLS - 1; i++)
		clip_line(batch, px[0][i], py[0][i], ws->mesh_colour[0][i],
			px[0][i + 1], py[0][i + 1], ws->mesh_colour[0][i + 1], 1.4f);
}


// Cache detailed CC0 silhouettes from code-drawn spans once per menu visit.
// Colours remain theme roles; texture is just coverage and dies before a game.

// The palms' coverage, made once per menu visit from the traced spans; the colours are the scene's.
static PSX3D_COLD void palms(void) {
    if (ws->palms_refused) return;
    if (!ws->palms_ok) {
        if (!C3D_TexInit(&ws->palms, PALMS_W, PALMS_H, GPU_A8)) { ws->palms_refused = true; return; }   // no room: the scene without its palms
        u8 *data = ws->palms.data;
        memset(data, 0, ws->palms.size);
        for (int i = 0; i < COUNT(list_scene_palm_spans); i++) {
            unsigned x = list_scene_palm_spans[i][0], y = list_scene_palm_spans[i][1];
            for (unsigned end = x + list_scene_palm_spans[i][2]; x < end; x++) {
                u8 *t = &data[texel(x / 2, y / 2, PALMS_W)];
                *t = *t >= 192 ? 255 : *t + 64;   // four traced pixels fill a texel
            }
        }
        C3D_TexSetFilter(&ws->palms, GPU_LINEAR, GPU_LINEAR);
        C3D_TexSetWrap(&ws->palms, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        svcStoreProcessDataCache(CUR_PROCESS_HANDLE, (u32)ws->palms.data, ws->palms.size);
        ws->palms_sub = (Tex3DS_SubTexture){400, 240, 0, 1, 400 / (float)PALMS_W, 1 - 240 / (float)PALMS_H};
        ws->palms_ok = true;
    }
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, alpha(ink.palm_rim, .65f), 1);
    C2D_DrawImageAt((C2D_Image){&ws->palms, &ws->palms_sub}, .6f, 0, Z, &tint, 1, 1);
    C2D_PlainImageTint(&tint, ink.palm, 1);
    C2D_DrawImageAt((C2D_Image){&ws->palms, &ws->palms_sub}, 0, 0, Z, &tint, 1, 1);
}
static PSX3D_COLD bool prepare(void) {
    if (ws)
        return true;
    if (refused)
        return false;
    ws = calloc(1, sizeof(*ws));
    if (!ws) {
        refused = true;
        return false;
    }
    for (int i = 0; i < 37; i++) {
        float x = fabsf(i - 18.f);
        ws->mesh_rise[i] = x <= 2 ? 0 : powf(x - 2, 1.15f) * .32f;
        ws->mesh_side[i] = powf(fminf(1, x / 9), .8f);
    }
    // The sun's glow: one soft disc, drawn as one textured quad.
    if (!C3D_TexInit(&ws->radial, 128, 128, GPU_A8)) {
        free(ws);
        ws = NULL;
        refused = true;
        return false;
    }
    C3D_TexSetFilter(&ws->radial, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&ws->radial, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    for (unsigned y = 0; y < 128; y++)
        for (unsigned x = 0; x < 128; x++) {
            float dx = (x + .5f - 64) / 64, dy = (y + .5f - 64) / 64;
            float d = sqrtf(dx * dx + dy * dy);
            float a = d < .45f ? 1 - d * (.65f / .45f) : .35f * (1 - d) / .55f;
            ((u8 *)ws->radial.data)[texel(x, y, 128)] = (u8)(255 * clamp(a));
        }
    svcStoreProcessDataCache(CUR_PROCESS_HANDLE, (u32)ws->radial.data, ws->radial.size);
    ws->radial_sub = (Tex3DS_SubTexture){128, 128, 0, 1, 1, 0};
    // The sun's bands are the same for a whole menu visit: half-pixel rows, cut by the widening gaps.
    for (float y = 55; y < 140; y += .5f) {
        float dy = y + .25f - 130, half = sqrtf(fmaxf(0, 75 * 75 - dy * dy));
        bool cut = false;
        for (float cy = 75 * .62f, k = 0; cy < 150; k++) {
            float gap = 75 * (.02f + .016f * k);
            if (y - 55 >= cy && y - 55 <= cy + gap) cut = true;
            cy += gap + 75 * .08f;
        }
        if (cut) continue;
        float d = (y - 55) / 150.f;
        u32 c = d < .5f ? mix(ink.sun[0], ink.sun[1], d * 2) :
                d < .85f ? mix(ink.sun[1], ink.sun[2], (d - .5f) / .35f) :
                mix(ink.sun[2], ink.sun[3], (d - .85f) / .15f);
        unsigned i = ws->sun_count++;
        ws->sun[i].x = 200 - half; ws->sun[i].y = y;
        ws->sun[i].width = half * 2; ws->sun[i].colour = c;
    }
    return true;
}
PSX3D_COLD void list_scene_release(void) {
    refused = false;   // the next menu visit tries again
    if (!ws)
        return;
    video_gpu_idle();
    C3D_TexDelete(&ws->radial);
    if (ws->palms_ok) C3D_TexDelete(&ws->palms);
    free(ws);
    ws = NULL;
}
// The top screen's picture. False: no memory for it now (the caller keeps its plain page colour).
PSX3D_COLD bool list_scene_draw(void) {
    if (!prepare())
        return false;
    // The clock is the console's uptime, so the grid never jumps on a theme change or after a game.
    double seconds = svcGetSystemTick() / (double)SYSCLOCK_ARM11;
    float t = (float)(seconds - 3600.0 * (u32)(seconds / 3600.0));
    additive = false;
    static const float at[] = {0, .4f, .78f, 1};
    for (int k = 1; k < 4; k++)
        grad(0, at[k - 1] * 140, 400, (at[k] - at[k - 1]) * 140, ink.sky[k - 1], ink.sky[k]);
    grad(0, 140, 400, 100, ink.floor, ink.floor_far);
    blend(true);
    scene_radial(&ws->radial, 200 - 112.5f, 130 - 112.5f, Z, 225, 225, alpha(ink.sun_glow, .38f));
    blend(false);
    for (unsigned i = 0; i < ws->sun_count; i++)
        rect(ws->sun[i].x, ws->sun[i].y, ws->sun[i].width, .5f, ws->sun[i].colour);
    palms();
    draw_grid(t);
    blend(false);
    return true;
}
