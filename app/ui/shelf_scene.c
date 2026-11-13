#include "shelf_carton.h"
#include "bb_index.h"
#include "ui_fit.h"
/* BreadBox bookshelf artwork (room themes). Paints the approved mock
 * (UI mock js/breadbox-r8.js, variant A) with Citro2D vertex batches: every
 * surface is a world-space quad (mm) projected through one camera, so the top
 * screen is drawn once per eye for the 3D slider. The room's colours come
 * from theme_room_t; wood, plaster and brass share one grey grain atlas that
 * is generated once and cached on the SD card, and the shelf's spines are
 * baked into one texture from the cover thumbnails. Menus only: everything
 * here is released before a game starts (shelf_scene_release). */
#include "app_policy.h"

#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "theme.h"
#include "ui_format.h"
#include "ui_style.h"
#include "app_paths.h"
#include "cold_code.h"
#include "app_log.h"
#include "app_config.h"
#include "platform_video.h"
#include "ui.h"
#include "covers.h"
#include "hand_styles.h"
#include "games_dir.h"
#include "app_main.h"
#include "bb_system.h"
#include "shelf.h"
#include "ui_motion.h"
#include "ui_mark.h"
#include "scene_batch.h"
#include "room.h"
#include "bb_save.h"

#define F 600.0f            // focal length, px
#define D_SHELF 500.0f      // reference distance for the dust's spread (mm)
#define DEPTH 170.0f        // shelf depth
#define BOARD 22.0f
#define CROWN 40.0f
#define X0 40.0f
#define Z .5f
#define PI 3.14159265f

#define GRAIN_W 512
#define GRAIN_H 512
#define GRAIN_FILE PSX3D_DIR "/cache/shelf-grain-1.l8"
// Grain atlas regions (x, y, w, h): grey wood, plaster and brass, tinted by the room.
enum { G_BOARD, G_LIP, G_CROWN, G_BACK, G_UPRIGHT, G_FLOOR, G_WALL, G_DESK, G_PLATE, G_COUNT };
static const short region[G_COUNT][4] = {
	{ 0, 0, 512, 64 }, { 0, 64, 512, 32 }, { 0, 96, 512, 32 }, { 0, 128, 256, 256 }, { 256, 128, 64, 256 },
	{ 320, 128, 192, 128 }, { 320, 256, 192, 128 }, { 0, 384, 256, 128 }, { 256, 384, 128, 32 } };

// Close-up spines: baked at twice the close-up's screen pixels into two VRAM
// atlases (one per VRAM bank if need be), so a settled spine is an exact 2:1
// (Old, 1x) or 1:1 (New, supersampled) copy: crisp, filtered text. Cells
// are sized for the shelf in close-up and dropped when another one is.
#define ATLAS 512
#define ATLASES 2
#define K 1                 // atlas texels per close-up screen pixel (the room draws the top screen at 1x on both models)
#define MAX_CELLS 64
#define BAKES_PER_FRAME 1
#define ROOM_BOXES 31
#define MAX_VISIBLE_BOXES 512
// Far shelves: one quad each, a flat picture of generic spines in the real
// boxes' places (linear memory, made on the CPU whenever the boxes move).
#define TEXT_CACHE 96

#define LOGOS 4
typedef struct { const bb_system_t *system; C3D_Tex tex; float u, v, aspect; bool ok; } logo_t;
typedef struct { float x, y, z; } v3;
typedef struct { float x, y, z; } pt;     // screen x, y and camera depth

static struct {
	bool tried, ready;
	C3D_Tex grain, glow, dark, atlas[ATLASES];
	C3D_RenderTarget *atlas_rt[ATLASES];
	int atlases;
	C2D_TextBuf text, cache;
	struct { const bb_system_t *system; int row; u32 used; bool cover, tinted, logo; } cell[MAX_CELLS];
	logo_t logos[LOGOS];   // maker marks, from the SD card
	int cells, cols, per_atlas, cw, ch, cell_row;   // the cell grid of the shelf in close-up
	unsigned cell_key;
	bool compact;
	struct { u32 hash; char s[64]; C2D_Text t; } tc[TEXT_CACHE];
	int tcn;
	u32 frame;
	struct { unsigned frame, layout; int row, n, indices[ROOM_BOXES], target_n, target[ROOM_BOXES], tested; bool valid; } visible;
	u64 ticks[2], peak[2], since;    // CPU time per frame: top (both eyes), bottom
	u32 frames, eyes;
} R;

// The Library in the room's own bookcase (a room file with a case): shelf r is the
// case's section r, the room is drawn around it (room_scene_draw_library) and only
// the desk below is painted here. UI mock R10 (#bb10=1): libStep, libPose, libraryDraw.
static bool in_room(void) { const room_file *f = room_scene_file(); return f && room_case_sections(f) > 0; }
bool shelf_scene_in_room(void) { return in_room(); }
static int room_visible_spines(int r, int *indices, int cap);
static int room_lookahead(int r, const int *visible, int n);
static int room_target_spines(int r, int *indices, int cap);
static float row_cur(int r);
static C3D_Mtx view_saved;   // around rotated text and artwork
static struct { C3D_Tex tex; C3D_RenderTarget *rt; int size; unsigned who, tier; } BK;   // the held case's back (back_bake)
static const theme_room_t *room;
static const shelf_view *V;

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float rnd(int i) { float x = sinf(i * 127.1f + 311.7f) * 43758.5453f; return x - floorf(x); }

// ---------------------------------------------------------------- colours
static u32 rgba(int r, int g, int b, int a)
{
	return (u32)clampf(r, 0, 255) | (u32)clampf(g, 0, 255) << 8 | (u32)clampf(b, 0, 255) << 16 | (u32)clampf(a, 0, 255) << 24;
}
static int ch(u32 c, int i) { return (c >> (8 * i)) & 255; }
static u32 shade(u32 c, float k) { return rgba(ch(c, 0) * k, ch(c, 1) * k, ch(c, 2) * k, ch(c, 3)); }
static u32 with_alpha(u32 c, float a) { return (c & 0x00ffffffu) | (u32)clampf(a * 255, 0, 255) << 24; }
static u32 mixc(u32 a, u32 b, float t)
{
	return rgba(lerpf(ch(a, 0), ch(b, 0), t), lerpf(ch(a, 1), ch(b, 1), t), lerpf(ch(a, 2), ch(b, 2), t), lerpf(ch(a, 3), ch(b, 3), t));
}

// ---------------------------------------------------------------- grain (the mock's procedural wood)
static inline u32 texel(u32 x, u32 y, u32 w)
{
	return (((y >> 3) * (w >> 3) + (x >> 3)) << 6) |
		(x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}
static float ih(int x, int y, int s)
{
	u32 h = (u32)x * 374761393u + (u32)y * 668265263u + (u32)s * 1442695041u;
	h = (h ^ (h >> 13)) * 1274126177u;
	h ^= h >> 16;
	return h / 4294967296.0f;
}
// Value noise. The grain walks texel by texel, so each call site keeps the
// lattice cell it used last and its four corner values.
typedef struct { int xi, yi, s; float a, b, c, d; } cell_t;
static float vnc(float x, float y, int s, cell_t *k)
{
	int xi = x >= 0 ? (int)x : (int)floorf(x), yi = y >= 0 ? (int)y : (int)floorf(y);
	if (xi != k->xi || yi != k->yi || s != k->s) {
		*k = (cell_t){ xi, yi, s, ih(xi, yi, s), ih(xi + 1, yi, s), ih(xi, yi + 1, s), ih(xi + 1, yi + 1, s) };
	}
	float xf = x - xi, yf = y - yi, u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
	return k->a + (k->b - k->a) * u + (k->c - k->a) * v + (k->a - k->b - k->c + k->d) * u * v;
}
static cell_t noise_cell[5];
static void noise_reset(void) { for (int i = 0; i < 5; i++) noise_cell[i] = (cell_t){ .xi = 1 << 30 }; }
static float vn(float x, float y, int s) { return vnc(x, y, s, &noise_cell[3]); }
static float fbm(float x, float y, int s)
{
	return vnc(x, y, s, &noise_cell[0]) * .6f + vnc(x * 2.1f, y * 2.1f, s + 7, &noise_cell[1]) * .28f + vnc(x * 4.3f, y * 4.3f, s + 13, &noise_cell[2]) * .12f;
}

typedef struct { bool along_x; int seed, knots, planks; float ring, light, dark; bool edge, nails, scratch; } wood_t;
// ring^2.6 for ring in [0, 1], sampled (the grain is made once, then cached)
static float ring_pow[257];
static float ringp(float r) { float f = r * 256; int i = (int)f; return ring_pow[i] + (ring_pow[i + 1 < 257 ? i + 1 : 256] - ring_pow[i]) * (f - i); }
// Grey value: the mock's lerp(light, dark, ring) * tone, normalised so the
// room's material colour is the wood's lightest tone.
typedef struct { float knot[6][3]; } wood_state;
static void wood(u8 *dst, const short *rg, const wood_t *o, int first, int end, wood_state *state)
{
	int w = rg[2], h = rg[3];
	float (*knot)[3]=state->knot;
	if(!first)
	for (int i = 0; i < o->knots; i++) {
		knot[i][0] = rnd(o->seed * 31 + i) * (o->along_x ? w : h);
		knot[i][1] = rnd(o->seed * 17 + i + 5) * (o->along_x ? h : w);
		knot[i][2] = 5 + rnd(o->seed + i * 3) * 7;
	}
	if (!ring_pow[256]) for (int i = 0; i <= 256; i++) ring_pow[i] = powf(i / 256.0f, 2.6f);
	// sin(a * .012 + pl * 2.1) = sin(A) cos(B) + cos(A) sin(B), A per texel line, B per plank
	static float sa[512], ca[512], sp[8], cp[8];
	static bool trig_ready;
	if(!trig_ready) {
		for (int i = 0; i < 512; i++) { sa[i] = sinf(i * .012f); ca[i] = cosf(i * .012f); }
		for (int i = 0; i < 8; i++) { sp[i] = sinf(i * 2.1f); cp[i] = cosf(i * 2.1f); }
		trig_ready=true;
	}
	int span = o->along_x ? h : w, planks = o->planks ? o->planks : 1;
	for (int y = first; y < h && y < end && !bb_index_cancelled(); y++)
		for (int x = 0; x < w; x++) {
			int ai = o->along_x ? x : y;
			float a = ai, b = o->along_x ? y : x;
			int pl = (int)(b / span * planks);
			float pb = b - pl * (float)span / planks;
			float g = pb * o->ring + 9 * fbm(a * .004f, pb * .03f, o->seed + pl) + 1.6f * (sa[ai] * cp[pl] + ca[ai] * sp[pl]);
			float kn = 1;
			for (int i = 0; i < o->knots; i++) {
				float da = (a - knot[i][0]) * .42f, db = b - knot[i][1], d2 = da * da + db * db, r2 = knot[i][2] * knot[i][2];
				if (d2 < r2 * 36) g += 7 * expf(-d2 / (r2 * 3));   // beyond: below 1/100000
				if (d2 < r2 * .25f) g += 4;
				if (d2 < r2 * .16f) kn = .55f;
			}
			float ring = g - floorf(g), t = ringp(ring) * .75f + .25f * vn(a * .03f, b * .9f, o->seed + 3);
			float frac = pb / ((float)span / planks) * 2 - 1, f2 = frac * frac, f4 = f2 * f2;
			float k = (.9f + .2f * ih(pl, 0, o->seed)) * (1 - .25f * f4 * f4);
			if (planks > 1 && pb < 1.4f) k *= .45f;
			if (o->scratch && ih((int)(a / 3), (int)(b * .5f), o->seed + 9) > .997f) k *= 1.25f;
			float v = lerpf(1, o->dark, t) * k * kn;
			if (o->edge) {
				float fy = (float)y / h;
				v *= fy < .12f ? lerpf(1.38f, 1.08f, fy / .12f) : fy < .2f ? lerpf(1.08f, 1, (fy - .12f) / .08f) : fy > .85f ? lerpf(1, .5f, (fy - .85f) / .15f) : 1;
			}
			dst[texel(rg[0] + x, rg[1] + y, GRAIN_W)] = (u8)clampf(v * 230, 0, 255);
		}
	if (o->nails && end >= h && !bb_index_cancelled())
		for (int p = 0; p < planks; p++)
			for (int j = 0; j < 2; j++) {
				int cx = (int)((p + .5f) * w / planks), cy = (int)((j ? .92f : .08f) * h);
				for (int dy = -2; dy <= 2; dy++)
					for (int dx = -2; dx <= 2; dx++)
						if (dx * dx + dy * dy <= 5) dst[texel(rg[0] + cx + dx, rg[1] + cy + dy, GRAIN_W)] = dx < 0 && dy < 0 ? 140 : 30;
			}
}
static void plaster(u8 *dst, const short *rg, int first, int end)
{
	for (int y = first; y < rg[3] && y < end && !bb_index_cancelled(); y++)
		for (int x = 0; x < rg[2]; x++) {
			float n = fbm(x * .03f, y * .03f, 41) * .7f + vn(x * .4f, y * .4f, 42) * .3f;
			dst[texel(rg[0] + x, rg[1] + y, GRAIN_W)] = (u8)clampf((n * .7f + .3f) * 255, 0, 255);
		}
}
// Brass name plate: a vertical sheen and an engraved inner border.
static void plate(u8 *dst, const short *rg, int first, int end)
{
	for (int y = first; y < rg[3] && y < end && !bb_index_cancelled(); y++)
		for (int x = 0; x < rg[2]; x++) {
			float fy = (float)y / rg[3], v = fy < .45f ? lerpf(1, .72f, fy / .45f) : lerpf(.72f, .44f, (fy - .45f) / .55f);
			bool border = (x == 3 || x == rg[2] - 4 || y == 3 || y == rg[3] - 4) && x >= 3 && x <= rg[2] - 4 && y >= 3 && y <= rg[3] - 4;
			bool edge = x == 0 || y == 0 || x == rg[2] - 1 || y == rg[3] - 1;
			if (border) v *= .6f;
			if (edge) v *= .5f;
			dst[texel(rg[0] + x, rg[1] + y, GRAIN_W)] = (u8)clampf(v * 255, 0, 255);
		}
}
static void make_grain_part(u8 *g,int part,int first,int end,wood_state *state)
{
	if(!part && !first)noise_reset();
	static const wood_t board = { true, 3, 4, 1, .08f, 1, 96.f / 176, false, false, true };
	static const wood_t lip = { true, 5, 2, 1, .1f, 1, 82.f / 150, true, false, true };
	static const wood_t crown = { true, 61, 3, 1, .07f, 1, 78.f / 150, true, false, false };
	static const wood_t back = { false, 7, 5, 6, .06f, 1, 54.f / 104, false, true, false };
	static const wood_t upright = { false, 9, 2, 1, .09f, 1, 86.f / 160, false, false, true };
	static const wood_t floor_ = { true, 13, 3, 5, .05f, 1, 56.f / 110, false, false, false };
	static const wood_t desk = { true, 17, 2, 3, .07f, 1, 44.f / 96, false, false, true };
	if(part==0)wood(g, region[G_BOARD], &board, first, end, state);
	if(part==1)wood(g, region[G_LIP], &lip, first, end, state);
	if(part==2)wood(g, region[G_CROWN], &crown, first, end, state);
	if(part==3)wood(g, region[G_BACK], &back, first, end, state);
	if(part==4)wood(g, region[G_UPRIGHT], &upright, first, end, state);
	if(part==5)wood(g, region[G_FLOOR], &floor_, first, end, state);
	if(part==6)plaster(g, region[G_WALL], first, end);
	if(part==7)wood(g, region[G_DESK], &desk, first, end, state);
	if(part==8)plate(g, region[G_PLATE], first, end);
}

/* Missing grain used to generate all 262144 texels and write the SD cache in
 * the first Library frame (~714 ms in Azahar). Worker-owned bytes outlive the
 * GPU texture; two rows per low-lane job, cancellable between rows. */
static struct {u8 *buf;size_t size;int state,part,row;bool ready;wood_state wood;} grain_job;
static void grain_work(void *unused)
{
 (void)unused;
 if(bb_index_cancelled()){__atomic_store_n(&grain_job.state,0,__ATOMIC_RELEASE);return;}
 u64 start=svcGetSystemTick();
 if(!grain_job.part && !grain_job.row) {
  FILE *f=fopen(GRAIN_FILE,"rb");bool ok=f!=NULL;
  for(size_t at=0;ok && at<grain_job.size;at+=65536) {
   size_t n=grain_job.size-at<65536?grain_job.size-at:65536;
   ok=!bb_index_cancelled() && fread(grain_job.buf+at,1,n,f)==n;
  }
  if(f)fclose(f);
  if(ok){grain_job.ready=true;__atomic_store_n(&grain_job.state,2,__ATOMIC_RELEASE);return;}
  if(bb_index_cancelled()){__atomic_store_n(&grain_job.state,0,__ATOMIC_RELEASE);return;}
  memset(grain_job.buf,128,grain_job.size);
 }
 if(grain_job.part<G_COUNT) {
  make_grain_part(grain_job.buf,grain_job.part,grain_job.row,grain_job.row+2,&grain_job.wood);
  if(bb_index_cancelled()){__atomic_store_n(&grain_job.state,0,__ATOMIC_RELEASE);return;}
  grain_job.row+=2;
  if(grain_job.row>=region[grain_job.part][3]){grain_job.part++;grain_job.row=0;}
 }
 if(grain_job.part==G_COUNT && !bb_index_cancelled()) {
  mkdir(PSX3D_DIR "/cache",0777);
  FILE *f=fopen(GRAIN_FILE ".tmp","wb");
  if(f){bool ok=true;for(size_t at=0;ok && at<grain_job.size;at+=16384){size_t n=grain_job.size-at<16384?grain_job.size-at:16384;ok=!bb_index_cancelled() && fwrite(grain_job.buf+at,1,n,f)==n;}fclose(f);if(ok)rename(GRAIN_FILE ".tmp",GRAIN_FILE);else remove(GRAIN_FILE ".tmp");}
  grain_job.ready=true;
 }
 if(grain_job.row==0)log_printf("shelf: grain worker part=%d chunk-ms=%lu\n",grain_job.part,(unsigned long)((svcGetSystemTick()-start)/(SYSCLOCK_ARM11/1000)));
 __atomic_store_n(&grain_job.state,grain_job.ready?2:0,__ATOMIC_RELEASE);
}
static void grain_step(void)
{
 int state=__atomic_load_n(&grain_job.state,__ATOMIC_ACQUIRE);
 if(!R.grain.data)return;
 if(state==2 && grain_job.buf) {
  memcpy(R.grain.data,grain_job.buf,R.grain.size);
  svcStoreProcessDataCache(CUR_PROCESS_HANDLE,(u32)R.grain.data,R.grain.size);
  free(grain_job.buf);grain_job.buf=NULL;return;
 }
 if(state || !grain_job.buf)return;
 __atomic_store_n(&grain_job.state,1,__ATOMIC_RELEASE);
 if(!bb_index_defer_low(grain_work,NULL)) {
  /* No index worker (missing root / thread allocation failure): one CPU-only
   * row per frame. Never perform cache I/O on this fallback. */
  if(!bb_index_cancelled() && grain_job.part<G_COUNT) {
   make_grain_part(grain_job.buf,grain_job.part,grain_job.row,grain_job.row+1,&grain_job.wood);
   grain_job.row++;
   if(grain_job.row>=region[grain_job.part][3]){grain_job.part++;grain_job.row=0;}
   grain_job.ready=grain_job.part==G_COUNT;
  }
  __atomic_store_n(&grain_job.state,grain_job.ready?2:0,__ATOMIC_RELEASE);
 }
}
static void grain_start(C3D_Tex *t)
{
 memset(t->data,128,t->size);
 if(!grain_job.buf) {
  grain_job.buf=malloc(t->size);if(grain_job.buf)memset(grain_job.buf,128,t->size);grain_job.size=t->size;grain_job.part=grain_job.row=0;grain_job.ready=false;
  __atomic_store_n(&grain_job.state,0,__ATOMIC_RELEASE);
 }
 grain_step();
}

// White radial light (LA8) and the matching darkness away from a light (A8).
static bool radial(C3D_Tex *t, bool dark)
{
	if (!C3D_TexInit(t, 64, 64, dark ? GPU_A8 : GPU_LA8)) return false;
	u8 *d = t->data;
	for (int y = 0; y < 64; y++)
		for (int x = 0; x < 64; x++) {
			float r = sqrtf((x - 31.5f) * (x - 31.5f) + (y - 31.5f) * (y - 31.5f)) / 32;
			if (dark) d[texel(x, y, 64)] = (u8)(255 * clampf(r < .06f ? 0 : r < .55f ? (r - .06f) / .49f * .35f : .35f + (r - .55f) / .45f * .65f, 0, 1));
			else {
				float a = r >= 1 ? 0 : (1 - r) * (1 - r);
				u32 i = texel(x, y, 64);
				d[2 * i] = (u8)(255 * a);   // alpha
				d[2 * i + 1] = 255;         // luminance
			}
		}
	C3D_TexSetFilter(t, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(t, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	svcStoreProcessDataCache(CUR_PROCESS_HANDLE, (u32)t->data, t->size);
	return true;
}

// a render target must be in VRAM; the second atlas only if it fits
static void atlases_make(void)
{
	for (R.atlases = 0; R.atlases < ATLASES; R.atlases++) {
		C3D_Tex *a = &R.atlas[R.atlases];
		if (!C3D_TexInitVRAM(a, ATLAS, ATLAS, GPU_RGB565)) break;
		C3D_TexSetFilter(a, GPU_LINEAR, GPU_LINEAR);
		C3D_TexSetWrap(a, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		if (!(R.atlas_rt[R.atlases] = C3D_RenderTargetCreateFromTex(a, GPU_TEXFACE_2D, 0, -1))) { C3D_TexDelete(a); break; }
	}
}
// the case's back (not needed to run: without it the back is the cover, darker)
static void back_make(void)
{
	for (int size = 256; size >= 128 && !BK.rt; size /= 2) {
		if (!C3D_TexInitVRAM(&BK.tex, size, size, GPU_RGB565)) continue;
		C3D_TexSetFilter(&BK.tex, GPU_LINEAR, GPU_LINEAR);
		C3D_TexSetWrap(&BK.tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		if ((BK.rt = C3D_RenderTargetCreateFromTex(&BK.tex, GPU_TEXFACE_2D, 0, -1))) BK.size = size;
		else C3D_TexDelete(&BK.tex);
	}
}
static void back_release(void);
static bool prepare(void)
{
	if (R.ready || R.tried) return R.ready;
	R.tried = true;
	u64 t0 = svcGetSystemTick();
	const char *step = "grain";
	if (!C3D_TexInit(&R.grain, GRAIN_W, GRAIN_H, GPU_L8)) goto fail;
	grain_start(&R.grain);
	C3D_TexSetFilter(&R.grain, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&R.grain, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	svcStoreProcessDataCache(CUR_PROCESS_HANDLE, (u32)R.grain.data, R.grain.size);
	step = "lights";
	if (!radial(&R.glow, false) || !radial(&R.dark, true)) goto fail;
	step = "spines";
	atlases_make();
	// the case's back in what is left (128 px: 256 costs the second atlas; made first and the atlases
	// deleted and made again when they did not fit, the first frame hung in Azahar)
	back_make();
	if (!R.atlases) goto fail;
	step = "far shelves";
	step = "text";
	R.text = C2D_TextBufNew(1024);
	R.cache = C2D_TextBufNew(2048);
	if (!R.text || !R.cache) goto fail;
	R.cell_row = -1;
	R.ready = true;
	log_printf("shelf: ready in %u ms (linear free %u KB, %d spine atlases, back %d px, VRAM free %u KB)\n",
		(unsigned)((svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000)), (unsigned)(linearSpaceFree() / 1024), R.atlases, BK.size,
		(unsigned)(vramSpaceFree() / 1024));
	return true;
fail:
	log_printf("shelf: %s failed (linear free %u KB)\n", step, (unsigned)(linearSpaceFree() / 1024));
	shelf_scene_release();
	R.tried = true;
	return false;
}

bool shelf_scene_prepare(void) { return theme_room(theme()) && prepare(); }

// The held case's back (bb-room3d round 10, authentic backs): no real back scans, so one is
// made in the style of the system's real ones for the chosen box: GBA the cover's colour with
// the system band, PS1 a dark insert with the spine strips; the title, two screens cut from the
// cover, blurb lines, a blank rating box. No codes, no numbers. One 256 x 256 render target
// (128 x 128 if VRAM is short), made at the start of a frame when the chosen box changes or its
// full-size cover arrives (~1-2 ms, once).
static void back_release(void)
{
	if (BK.rt) C3D_RenderTargetDelete(BK.rt);
	if (BK.tex.data) C3D_TexDelete(&BK.tex);
	memset(&BK, 0, sizeof(BK));
}
void shelf_scene_release(void)
{
	back_release();
	for (int i = 0; i < ATLASES; i++) {
		if (R.atlas_rt[i]) C3D_RenderTargetDelete(R.atlas_rt[i]);
		if (R.atlas[i].data) C3D_TexDelete(&R.atlas[i]);
	}
	if (R.cache) C2D_TextBufDelete(R.cache);
	// grain_job owns CPU bytes: no wait for a cancelled/background read here.
	if (R.grain.data) C3D_TexDelete(&R.grain);
	if (R.glow.data) C3D_TexDelete(&R.glow);
	if (R.dark.data) C3D_TexDelete(&R.dark);
	for (int i = 0; i < LOGOS; i++) if (R.logos[i].ok) C3D_TexDelete(&R.logos[i].tex);
	if (R.text) C2D_TextBufDelete(R.text);
	memset(&R, 0, sizeof(R));
}

// ---------------------------------------------------------------- drawing primitives
static C2Di_Context *tex_mode(C3D_Tex *t, unsigned vertices)
{
	return scene_reserve(vertices, vertices / 4 * 6, C2DiF_Mode_ImageMult, t);
}
// 2D textured quad (TL, TR, BR, BL), texture rect in texture coords.
static void tquad(C3D_Tex *t, const float p[4][2], const float uv[4][2], const u32 c[4])
{
	C2Di_Context *ctx = tex_mode(t, 4);
	if (!ctx) return;
	C2Di_Vertex *v = scene_quad(ctx);   // order: 0 TL, 1 TR, 2 BL, 3 BR
	scene_vertex(&v[0], p[0][0], p[0][1], Z, uv[0][0], uv[0][1], 0, 1, c[0]);
	scene_vertex(&v[1], p[1][0], p[1][1], Z, uv[1][0], uv[1][1], 0, 1, c[1]);
	scene_vertex(&v[2], p[3][0], p[3][1], Z, uv[3][0], uv[3][1], 0, 1, c[3]);
	scene_vertex(&v[3], p[2][0], p[2][1], Z, uv[2][0], uv[2][1], 0, 1, c[2]);
}
static void solid4(const float p[4][2], const u32 c[4])
{
	C2Di_Context *ctx = scene_reserve(4, 6, C2DiF_Mode_Solid, NULL);
	if (!ctx) return;
	C2Di_Vertex *v = scene_quad(ctx);
	scene_vertex(&v[0], p[0][0], p[0][1], Z, 0, 0, 0, 0, c[0]);
	scene_vertex(&v[1], p[1][0], p[1][1], Z, 0, 0, 0, 0, c[1]);
	scene_vertex(&v[2], p[3][0], p[3][1], Z, 0, 0, 0, 0, c[3]);
	scene_vertex(&v[3], p[2][0], p[2][1], Z, 0, 0, 0, 0, c[2]);
}
static void solid(const float p[4][2], u32 c) { const u32 cc[4] = { c, c, c, c }; solid4(p, cc); }
static void rect(float x, float y, float w, float h, u32 c) { const float p[4][2] = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h } }; solid(p, c); }
static void vgrad(float x, float y, float w, float h, u32 a, u32 b)
{
	const float p[4][2] = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h } };
	const u32 c[4] = { a, a, b, b };
	solid4(p, c);
}
static void tri(float ax, float ay, u32 a, float bx, float by, u32 b, float cx, float cy, u32 c)
{
	scene_solid_triangle(NULL, ax, ay, a, bx, by, b, cx, cy, c, Z);
}
static void line(float x0, float y0, float x1, float y1, float w, u32 c)
{
	float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
	if (l < 1e-4f) return;
	float nx = -dy / l * w / 2, ny = dx / l * w / 2;
	const float p[4][2] = { { x0 + nx, y0 + ny }, { x1 + nx, y1 + ny }, { x1 - nx, y1 - ny }, { x0 - nx, y0 - ny } };
	solid(p, c);
}
// Filled outline (a star-shaped path round its centroid) with a linear gradient.
static void poly(float (*p)[2], int n, u32 c0, u32 c1, float gx0, float gy0, float gx1, float gy1)
{
	float cx = 0, cy = 0;
	for (int i = 0; i < n; i++) { cx += p[i][0]; cy += p[i][1]; }
	cx /= n; cy /= n;
	float gx = gx1 - gx0, gy = gy1 - gy0, gl = gx * gx + gy * gy;
#define AT(x, y) mixc(c0, c1, gl > 0 ? clampf(((x) - gx0) * gx / gl + ((y) - gy0) * gy / gl, 0, 1) : 0)
	u32 cc = AT(cx, cy);
	for (int i = 0; i < n; i++) {
		int j = (i + 1) % n;
		tri(cx, cy, cc, p[i][0], p[i][1], AT(p[i][0], p[i][1]), p[j][0], p[j][1], AT(p[j][0], p[j][1]));
	}
#undef AT
}
static void sprite(C3D_Tex *t, float cx, float cy, float rx, float ry, u32 c)
{
	const float p[4][2] = { { cx - rx, cy - ry }, { cx + rx, cy - ry }, { cx + rx, cy + ry }, { cx - rx, cy + ry } };
	const float uv[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
	const u32 cc[4] = { c, c, c, c };
	tquad(t, p, uv, cc);
}

static u32 hashc(const char *s)
{
	u32 h = 2166136261u;
	for (; *s; s++) h = (h ^ (u8)*s) * 16777619u;
	return h;
}
// text in the scene, at the scene's depth (shared text sits at 0.7)
static float text_px(float px) { return px / 30.0f; }
// Parsed text kept from frame to frame: the card and the plates change rarely,
// so most frames only draw their glyphs. Long one-off strings use R.text.
static const C2D_Text *text_get(const char *s, C2D_Text *tmp)
{
	if (!R.cache || !s || !*s) return NULL;
	size_t n = strlen(s);
	if (n >= sizeof(R.tc[0].s)) {
		if (!C2D_TextFontParse(tmp, NULL, R.text, s)) return NULL;
		C2D_TextOptimize(tmp);
		return tmp;
	}
	u32 h = hashc(s);
	for (int i = 0; i < R.tcn; i++)
		if (R.tc[i].hash == h && !strcmp(R.tc[i].s, s)) return &R.tc[i].t;
	if (R.tcn == TEXT_CACHE || !C2D_TextFontParse(&R.tc[R.tcn].t, NULL, R.cache, s)) {
		C2D_TextBufClear(R.cache);
		R.tcn = 0;
		if (!C2D_TextFontParse(&R.tc[0].t, NULL, R.cache, s)) return NULL;
	}
	C2D_TextOptimize(&R.tc[R.tcn].t);
	R.tc[R.tcn].hash = h;
	memcpy(R.tc[R.tcn].s, s, n + 1);
	return &R.tc[R.tcn++].t;
}
// y is the text's baseline: the room's 3D labels were placed with .82 of their size; on ruled paper (between
// card_begin and card_end) the loaded font's own baseline (UI_PAPER_TOP), so the letters stand on the ruled lines
static bool text_on_paper;
static void text_at(float x, float y, float px, u32 c, u32 flags, const char *s)
{
	C2D_Text tmp;
	const C2D_Text *t = text_get(s, &tmp);
	float top = text_on_paper ? UI_PAPER_TOP(y, text_px(px)) : y - px * .82f;
	if (t) C2D_DrawText(t, C2D_WithColor | flags, x, top, Z, text_px(px), text_px(px), c);
}
static float text_w(float px, const char *s)
{
	C2D_Text tmp;
	const C2D_Text *t = text_get(s, &tmp);
	float w = 0;
	if (t) C2D_TextGetDimensions(t, text_px(px), text_px(px), &w, NULL);
	return w;
}
static float fit_px(const char *s, float width, float px)
{
	float w = text_w(px, s);
	return w > width ? px * width / w : px;
}

// One line on the shared fit policy (ui_fit.h): px, or smaller down to min_px, then cut between whole characters
// with "..." (a long game name or path never turns into micro text). flags: 0 or C2D_AlignRight (x is the right edge).
static void text_fit_at(float x, float y, float width, float px, float min_px, u32 c, u32 flags, const char *s)
{
	float scale;
	const char *t = ui_fit_ex(NULL, s, text_px(px), text_px(min_px), width, &scale);
	if (t[0]) text_at(x, y, scale / text_px(1), c, flags, t);
}
static void text_fit(float x, float y, float width, float px, float min_px, u32 c, const char *s) { text_fit_at(x, y, width, px, min_px, c, 0, s); }
// The shared card header's text (ui_card_header): one line, any font, at the scene's depth, parsed into the
// scene's own per-draw buffer (the shelf loop never clears the shared one ui_text_ex uses).
static void header_text(C2D_Font font, float x, float y, float size, u32 c, u32 flags, const char *s)
{
	C2D_Text t;
	C2D_TextBufClear(R.text);
	if (!s || !*s || !C2D_TextFontParse(&t, font, R.text, s)) return;
	C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_WithColor | flags, x, y, Z, size, size, c);
}
// a card's header row: its title on the red rule (the pause and settings card's own routine and metrics)
static void card_title(const char *title, float width)
{
	ui_card_header(header_text, UI_ROOM_TEXT_X, UI_ROOM_TEXT_X + width, UI_ROOM_RULE_Y, title, NULL, room->ink, with_alpha(room->rule, 1));
}
// The longest head of s (bytes) that fits width at px: broken after a space when one fits, else between whole
// characters (a title without spaces, Japanese). 0 when s fits whole.
static size_t text_break(const char *s, float width, float px)
{
	if (text_w(px, s) <= width) return 0;
	char tmp[SHELF_TITLE + 8];
	size_t best = 0, best_space = 0, n = strlen(s);
	if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
	for (size_t i = 1; i <= n; i++) {
		if (i < n && ((unsigned char)s[i] & 0xc0) == 0x80) continue;
		memcpy(tmp, s, i); tmp[i] = 0;
		if (text_w(px, tmp) > width) break;
		best = i;
		if (i < n && s[i] == ' ') best_space = i;
	}
	return best_space ? best_space : best;
}
// Helper text over up to `lines` lines (broken at spaces, between characters if it must), the last one cut with
// "..." when the text is longer; returns the lines used.
static int text_lines(float x, float y, float step, float width, float px, int lines, u32 c, const char *s)
{
	char line[SHELF_TITLE + 8];
	int used = 0;
	while (*s && used < lines) {
		size_t cut = used + 1 < lines ? text_break(s, width, px) : 0;
		if (!cut) { text_fit(x, y + step * used++, width, px, UI_ROOM_MIN_PX, c, s); break; }
		ui_fit_copy(line, sizeof(line), s, cut);
		text_at(x, y + step * used++, px, c, 0, line);
		s += cut;
		while (*s == ' ') s++;
	}
	return used;
}


static float uv_u(int r, float f) { return (region[r][0] + f * region[r][2]) / GRAIN_W; }
static float uv_v(int r, float f) { return 1 - (region[r][1] + f * region[r][3]) / GRAIN_H; }


#define CLOSE_BOARD_Y 214.0f
// ---------------------------------------------------------------- the 3D hands
// The held box's clip class: cartons gba, jewel cases by thickness (ps1 / ps1p PAL / ps1d multi-disc)
static int hands_box(const shelf_box *b)
{
	const bb_package *pk = b->system ? b->system->package : NULL;
	return hands_box_class(!(pk && pk->style == BB_PACKAGE_CASE), b->t);
}
// With the room's hands (room_hands.c, hands.bbh) the chosen box is taken out,
// held, turned and opened by the UI mock's baked clips, in the box's slot.
static bool hands_now(hands_shelf_clip *hc, const shelf_box **box, float slot[3])
{
	const shelf_box *b = shelf_pulled_box(V);
	if (!b || !room_hands_open()) return false;
	int kind = hands_box(b);
	hands_shelf_state st = { V->t, V->pull, V->pto, V->pt0, V->pdur, V->play_t0 >= V->pt0 ? V->play_t0 : -1, V->side, V->turn_from, V->turn_steps, V->turn_t0,
		V->rev, V->rev_t };
	*hc = hands_for_shelf(room_hands_file(), kind, g_cfg.hand_glove == HAND_MITTENS, &st);
	if (!hc->clip) return false;
	*box = b;
	slot[0] = b->x; slot[1] = V->rows[V->prow].base; slot[2] = 4 + b->w / 2;   // the box standing in its slot
	return true;
}

bool shelf_scene_hands(void) { return theme_room(theme()) && room_hands_open(); }

float shelf_scene_clip_seconds(const shelf_view *v, const char *action, float dflt)
{
	const shelf_box *b = v->pto == 1 || v->pull > .002f ? shelf_pulled_box(v) : NULL;
	if (!b && v->row < v->nrows && v->rows[v->row].n) b = &v->rows[v->row].box[v->sel[v->row]];
	if (!b || !shelf_scene_hands()) return dflt;
	const hands_clip *c = hands_clip_for(room_hands_file(), action, hands_box(b), g_cfg.hand_glove == HAND_MITTENS);
	return c ? (!strncmp(action, "turn", 4) ? UI_MOTION_TURN_S : UI_MOTION_CASE_S) : dflt;
}

float shelf_scene_launch_seconds(const shelf_view *v)
{
	const shelf_box *b = shelf_pulled_box(v);
	if (!b || !shelf_scene_hands()) return .9f;
	const hands_clip *c = hands_clip_for(room_hands_file(), "open", hands_box(b), g_cfg.hand_glove == HAND_MITTENS);
	return c ? UI_MOTION_LAUNCH_S : .9f;
}


// ---------------------------------------------------------------- boxes
typedef struct { float x, y, z, yaw, pitch; } pose_t;
typedef struct { v3 c[4]; v3 n; } face_t;
enum { FACE_SPINE, FACE_FRONT, FACE_BACK, FACE_TOP, FACE_EDGE, FACE_BOTTOM };
// placeholder art colours for boxes without a cover yet (the mock's palettes)
static u32 placeholder(const shelf_box *b, int which)
{
	static const u32 pal[8][2] = { { 0xff4a2a1d, 0xff3c6fe8 }, { 0xff35102b, 0xff7a30c2 }, { 0xff3a3a0f, 0xffa0b53f }, { 0xff0e1e3a, 0xff2c89d8 },
		{ 0xff3a2012, 0xffe07f4d }, { 0xff0f0f2c, 0xff2a3ad2 }, { 0xff122a1f, 0xff3abf8f }, { 0xff4a2630, 0xffe07a9d } };
	return pal[hashc(b->title) % 8][which];
}
static u32 box_color(const shelf_box *b) { return b->color ? b->color : placeholder(b, 0); }
// A box's outer sides (top, bottom, the open edge), the same on the shelf, pulled and in the hand:
// a jewel case's dark plastic, a carton's own printed colour (never white, never brightened)
static u32 side_color(const shelf_box *b)
{
	const bb_package *pk = b->system->package;
	return pk && pk->style == BB_PACKAGE_CASE ? 0xff24201e : box_color(b);
}

// ---------------------------------------------------------------- close-up spine cells
// The shelf whose real spines are built: the one the camera is close to.
// In the room the highlighted section's spines are baked already on the wall view.
static int close_row(void)
{
	if (!V->nrows) return -1;
	return V->level == SHELF_ALL && V->zoom < .01f ? (V->hsel < V->nrows ? V->hsel : -1) : V->row < V->nrows ? V->row : -1;
}
static int px_of(float mm, float s) { return (int)lroundf(mm * s); }
// Keep enough cells for the draw bound, a held case, and lookahead.
// Oversized rows use compact title strips rather than starving visible spines.
static void cells_for(int r)
{
	unsigned key = V->layout;
	if (R.cell_row == r && R.cell_key == key) return;
	const shelf_row *row = &V->rows[r];
	float tmax = 1;
	for (int i = 0; i < row->n; i++) tmax = fmaxf(tmax, row->box[i].t);
	R.cw = K * px_of(tmax, row->s);
	R.ch = K * px_of(row->hmax, row->s);
	if (R.cw < K) R.cw = K;
	if (R.cw > ATLAS) R.cw = ATLAS;
	// Favorites can be empty, including when walking back to the wall.
	// Keep the atlas divisor valid even when that row has no box height.
	if (R.ch < K) R.ch = K;
	if (R.ch > ATLAS) R.ch = ATLAS;
	int rows = ATLAS / R.ch;
	int need = row->n < ROOM_BOXES + 2 ? row->n : ROOM_BOXES + 2;
	if (need < 1) need = 1;
	int need_cols = (need + rows * R.atlases - 1) / (rows * R.atlases);
	int limit = ATLAS / need_cols;
	R.compact = R.cw > limit;
	if (R.compact) R.cw = limit;
	R.cols = ATLAS / R.cw;
	R.per_atlas = R.cols * (ATLAS / R.ch);
	R.cells = R.per_atlas * R.atlases;
	if (R.cells > MAX_CELLS) R.cells = MAX_CELLS;
	assert(R.cells >= need);
	memset(R.cell, 0, sizeof(R.cell));
	R.cell_row = r;
	R.cell_key = key;
}
static int spine_width(const shelf_box *b) { int w = K * px_of(b->t, V->rows[R.cell_row].s); return w < R.cw ? w : R.cw; }
static int cell_of(const shelf_box *b)
{
	if (R.cell_row < 0 || R.cell_row != close_row()) return -1;   // cells belong to one shelf
	for (int i = 0; i < R.cells; i++)
		if (R.cell[i].system == b->system && R.cell[i].row == b->row && R.cell[i].used) return i;
	return -1;
}
static C3D_Tex *cell_tex(int c) { return &R.atlas[c / R.per_atlas]; }
static void cell_rect(int c, int *x, int *y)
{
	int k = c % R.per_atlas;
	*x = k % R.cols * R.cw;
	*y = k / R.cols * R.ch;
}
// a box's spine in its cell: whole close-up pixels, K texels each
static void spine_uv(int cell, const shelf_box *b, float *u0, float *v0, float *u1, float *v1)
{
	int x, y;
	float s = V->rows[R.cell_row].s;
	cell_rect(cell, &x, &y);
	*u0 = (float)x / ATLAS;
	*u1 = (float)(x + spine_width(b)) / ATLAS;
	// render targets keep the first framebuffer row at the texture's top
	*v0 = 1 - (float)y / ATLAS;
	*v1 = 1 - (float)(y + K * px_of(b->h, s)) / ATLAS;
}


// ---------------------------------------------------------------- spine bake
static int cell_for(const shelf_box *b)
{
	(void)b;
	int lru = 0;
	for (int i = 0; i < R.cells; i++) {
		if (!R.cell[i].used) return i;
		if (R.cell[i].used < R.cell[lru].used) lru = i;
	}
	return R.cells && R.cell[lru].used != R.frame ? lru : -1;
}
// A colour with its HSL lightness kept in [lo, hi] (hue and saturation kept).
static u32 clamp_lightness(u32 c, float lo, float hi)
{
	float r = ch(c, 0) / 255.0f, g = ch(c, 1) / 255.0f, b = ch(c, 2) / 255.0f;
	float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), l = (mx + mn) / 2, d = mx - mn;
	if (l >= lo && l <= hi) return c;
	float sat = d > 0 ? fminf(1, d / (1 - fabsf(2 * l - 1))) : 0, h = 0;   // h in sixths of the circle
	if (d > 0) h = mx == r ? fmodf((g - b) / d + 6, 6) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
	l = clampf(l, lo, hi);
	float C = (1 - fabsf(2 * l - 1)) * sat, X = C * (1 - fabsf(fmodf(h, 2) - 1)), m = l - C / 2, o[3] = { 0, 0, 0 };
	switch ((int)h % 6) {
	case 0: o[0] = C; o[1] = X; break;
	case 1: o[0] = X; o[1] = C; break;
	case 2: o[1] = C; o[2] = X; break;
	case 3: o[1] = X; o[2] = C; break;
	case 4: o[0] = X; o[2] = C; break;
	default: o[0] = C; o[2] = X; break;
	}
	return rgba((o[0] + m) * 255 + .5f, (o[1] + m) * 255 + .5f, (o[2] + m) * 255 + .5f, ch(c, 3));
}
// The cover's colours in one pass over every other texel of the picture (the top-left
// part of the texture): b->color its average (box sides, plain spines), b->accent its
// dominant saturated colour (carton spines): twelve hue bins weighted by saturation x
// value, the top bin's weighted mean, lightness kept in 18-82 %; a grey cover: its average.
#define HUE_BINS 12
static void average_color(shelf_box *b, C3D_Tex *t, const Tex3DS_SubTexture *s)
{
	const u16 *px = t->data;
	u32 w = (u32)(s->right * t->width + .5f), h = (u32)((1 - s->bottom) * t->height + .5f), r = 0, g = 0, bl = 0, k = 0;
	float bin[HUE_BINS][4] = { { 0 } }, total = 0;
	for (u32 y = 0; y < h && y < t->height; y += 2)
		for (u32 x = 0; x < w && x < t->width; x += 2, k++) {
			u16 c = px[texel(x, y, t->width)];
			int cr = (c >> 11) & 31, cg = (c >> 5) & 63, cb = c & 31;
			r += cr; g += cg; bl += cb;
			float fr = cr / 31.0f, fg = cg / 63.0f, fb = cb / 31.0f;
			float mx = fmaxf(fr, fmaxf(fg, fb)), mn = fminf(fr, fminf(fg, fb)), d = mx - mn;
			if (d <= 0) continue;
			float wt = d;   // saturation (d / mx) x value (mx)
			float hh = mx == fr ? fmodf((fg - fb) / d + 6, 6) : mx == fg ? (fb - fr) / d + 2 : (fr - fg) / d + 4;
			int i = (int)(hh * HUE_BINS / 6) % HUE_BINS;
			bin[i][0] += wt; bin[i][1] += wt * fr; bin[i][2] += wt * fg; bin[i][3] += wt * fb;
			total += wt;
		}
	if (k) b->color = rgba(r * 255 / 31 / k, g * 255 / 63 / k, bl * 255 / 31 / k, 255);
	if (!b->color) b->color = 0xff010101;
	int top = 0;
	for (int i = 1; i < HUE_BINS; i++) if (bin[i][0] > bin[top][0]) top = i;
	u32 a = k && total > .06f * k && bin[top][0] > 0
		? rgba(bin[top][1] / bin[top][0] * 255, bin[top][2] / bin[top][0] * 255, bin[top][3] / bin[top][0] * 255, 255) : b->color;
	b->accent = clamp_lightness(a, .18f, .82f) | 0xff000000u;
}
static float even(float v) { return K * roundf(v / K); }   // whole close-up pixels
#define INK_CENTER .48f    // where the middle of the letters sits in the line box, from its top
// The maker's logo of a system, as a small texture, once it is on the SD card
// (covers_logo: fetched through the cover downloader, never part of the app).
// NULL until then and offline: spines then carry the maker's name as plain text.
static const logo_t *logo_of(const bb_system_t *sys)
{
	if (!sys || !sys->logo_url) return NULL;
	logo_t *free_slot = NULL;
	for (int i = 0; i < LOGOS; i++) {
		if (R.logos[i].system == sys) return R.logos[i].ok ? &R.logos[i] : NULL;
		if (!R.logos[i].system && !free_slot) free_slot = &R.logos[i];
	}
	int w, h;
	const u32 *px = free_slot ? covers_logo(sys, &w, &h) : NULL;   // the first call asks the downloader
	if (!px) return NULL;
	int tw = 8, th = 8;
	while (tw < w) tw <<= 1;
	while (th < h) th <<= 1;
	if (!C3D_TexInit(&free_slot->tex, tw, th, GPU_RGBA8)) return NULL;
	free_slot->system = sys;
	u32 *d = free_slot->tex.data;
	memset(d, 0, (size_t)tw * th * 4);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) d[texel(x, y, tw)] = px[y * w + x];
	C3D_TexSetFilter(&free_slot->tex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&free_slot->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	svcStoreProcessDataCache(CUR_PROCESS_HANDLE, (u32)free_slot->tex.data, free_slot->tex.size);
	free_slot->u = (float)w / tw;
	free_slot->v = 1 - (float)h / th;   // the picture sits in the texture's top left, as the covers do
	free_slot->aspect = (float)w / h;
	free_slot->ok = true;
	return free_slot;
}
// the system's downloaded logo centred at cx, cy within w x h; false: none (yet)
static bool spine_logo(const bb_system_t *sys, float cx, float cy, float w, float h)
{
	const logo_t *l = logo_of(sys);
	if (!l) return false;
	float lw = w, lh = w / l->aspect;
	if (lh > h) { lh = h; lw = h * l->aspect; }
	const float p[4][2] = { { cx - lw / 2, cy - lh / 2 }, { cx + lw / 2, cy - lh / 2 }, { cx + lw / 2, cy + lh / 2 }, { cx - lw / 2, cy + lh / 2 } };
	const float uv[4][2] = { { 0, 1 }, { l->u, 1 }, { l->u, l->v }, { 0, l->v } };
	const u32 white[4] = { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff };
	tquad((C3D_Tex *)&l->tex, p, uv, white);
	return true;
}
// Rotated text down the spine (like the title) centred in [y0, y0 + len], at most px
// close-up pixels high; squeezed along the spine (to half) and then made smaller to fit.
// outline: a 1 px ring of that colour around the letters (0 none).
static void spine_text(const char *s, float cx, float y0, float len, float px, u32 ink, u32 outline)
{
	C2D_Text t;
	C2D_TextBufClear(R.text);
	if (!s || !*s || !C2D_TextFontParse(&t, NULL, R.text, s)) return;
	C2D_TextOptimize(&t);
	float sc = text_px(px * K), sx = 1, tw = 0;
	C2D_TextGetDimensions(&t, sc, sc, &tw, NULL);
	if (tw <= 0) return;
	if (tw > len) {
		float k = len / tw;
		if (k >= .5f) sx = k;
		else { sx = .5f; sc *= k / .5f; }
		tw = len;
	}
	float lh = 30 * sc, off = -even(lh * INK_CENTER);
	C2D_ViewSave(&view_saved);
	C2D_ViewTranslate(cx, even(y0 + (len - tw) / 2));
	C2D_ViewRotate(PI / 2);
	if (outline)
		for (int d = 0; d < 4; d++)
			C2D_DrawText(&t, C2D_WithColor, (d == 0 ? -K : d == 1 ? K : 0), off + (d == 2 ? -K : d == 3 ? K : 0), Z, sc * sx, sc, outline);
	C2D_DrawText(&t, C2D_WithColor, 0, off, Z, sc * sx, sc, ink);
	C2D_ViewRestore(&view_saved);
}
// Short text across the spine (upright), centred at cx, cy within w x h.
static void spine_across(const char *s, float cx, float cy, float w, float h, u32 ink)
{
	C2D_Text t;
	C2D_TextBufClear(R.text);
	if (!C2D_TextFontParse(&t, NULL, R.text, s)) return;
	C2D_TextOptimize(&t);
	float px = fminf(h / K * .9f, 9), sc = text_px(px * K), tw;
	C2D_TextGetDimensions(&t, sc, sc, &tw, NULL);
	if (tw > w * .86f) { sc *= w * .86f / tw; tw = w * .86f; }
	C2D_DrawText(&t, C2D_WithColor, even(cx - tw / 2), even(cy - 15 * sc), Z, sc, sc, ink);
}
// The title down the spine (letter tops toward the front cover), centred on
// the spine's middle `mid` (kept inside [y0, y0 + len]), as large as the spine allows.
// A long title is made smaller (to 10 px), then squeezed along the spine (to 60 %),
// and only then cut short. outline: a 1 px ring around the letters (0 none).
static void spine_title(const char *title, float cx, float mid, float y0, float len, float w, u32 ink, u32 outline)
{
	float px = clampf(w / K - 8, 8, 15), sc = text_px(px * K), sx = 1, tw = 0;
	char s[SHELF_TITLE + 4];
	snprintf(s, sizeof(s), "%s", title);
	C2D_Text t;
	C2D_TextBufClear(R.text);
	if (!C2D_TextFontParse(&t, NULL, R.text, s)) return;
	C2D_TextGetDimensions(&t, sc, sc, &tw, NULL);
	if (tw > len) {
		float least = fminf(px, 10);
		if (px * len / tw >= least) px *= len / tw;
		else if (least / px * .6f * tw <= len) { sx = len / (tw * least / px); px = least; }
		else {
			int n = (int)strlen(title), cut = (int)(n * len / (tw * .6f * least / px)) - 2;
			px = least;
			sx = .6f;
			for (cut = cut < n ? cut : n; cut > 1; cut--) {
				while (cut > 1 && title[cut - 1] == ' ') cut--;
				snprintf(s, sizeof(s), "%.*s...", cut, title);
				C2D_TextBufClear(R.text);
				if (!C2D_TextFontParse(&t, NULL, R.text, s)) return;
				C2D_TextGetDimensions(&t, text_px(px * K), text_px(px * K), &tw, NULL);
				if (tw * sx <= len) break;
			}
		}
		sc = text_px(px * K);
		C2D_TextGetDimensions(&t, sc, sc, &tw, NULL);
	}
	tw *= sx;
	C2D_TextOptimize(&t);
	float lh = 30 * sc, start = even(clampf(mid - tw / 2, y0, y0 + len - tw));
	C2D_ViewSave(&view_saved);
	C2D_ViewTranslate(cx, start);
	C2D_ViewRotate(PI / 2);
	// across the spine: the letters' ink (caps and x-height, not the empty line box) is centred
	float off = -even(lh * INK_CENTER);
	if (outline)
		for (int d = 0; d < 4; d++)
			C2D_DrawText(&t, C2D_WithColor, (d == 0 ? -K : d == 1 ? K : 0), off + (d == 2 ? -K : d == 3 ? K : 0), Z, sc * sx, sc, outline);
	C2D_DrawText(&t, C2D_WithColor, 0, off, Z, sc * sx, sc, ink);
	C2D_ViewRestore(&view_saved);
}
// A real spine scan of the game, fetched at runtime like the covers (gba-spine-sources.md).
// Hook for a later round: none yet, every spine is drawn.
static const C3D_Tex *spine_scan(const shelf_box *b)
{
	(void)b;
	return NULL;
}
// The maker's logo block at the head of a PlayStation spine (0-11 %): the downloaded
// logo, else the name as plain text down the spine like the title (nothing Sony shipped).
static void spine_head_block(const shelf_box *b, float x, float y, float w, float h, float scale, u32 ink)
{
	float cx = x + w / 2, top = h * .11f;
	if (!spine_logo(b->system, cx, y + top / 2, w * .8f * scale, top * .8f * scale))
		spine_text("PlayStation", cx, y + top * (.5f - .4f * scale), top * .8f * scale, fminf(w / K * .45f, 8) * scale, ink, 0);
}
// an opaque grey the RGB565 atlas holds exactly (v a multiple of 8)
static u32 grey565(unsigned v) { return 0xff000000u | v << 16 | v << 8 | v; }
// The black case's plastic in exact greys (room.h ROOM_JEWEL_*): four bands down, darker rounded sides
static void jewel_black(float x, float y, float w, float h)
{
	static const unsigned char mid[4] = ROOM_JEWEL_MID, side[4] = ROOM_JEWEL_SIDE;
	float l = even(w * .25f), r = even(w * .2f);
	for (int i = 0; i < 4; i++) {
		float y0 = y + even(h * i / 4), y1 = i == 3 ? y + h : y + even(h * (i + 1) / 4);
		rect(x, y0, l, y1 - y0, grey565(side[i]));
		rect(x + l, y0, w - l - r, y1 - y0, grey565(mid[i]));
		rect(x + w - r, y0, r, y1 - y0, grey565(side[i]));
	}
}
// One spine into its cell, K texels per close-up pixel (round 8, SPINE-SPEC-r8.md). Jewel
// cases by the game's region: North America black, PAL black or dark blue with a PAL tag
// and a light foot band, Japan a white spine card with the product code at the foot.
// Cartons: a real spine scan when there is one, else one solid colour from the cover's
// dominant saturated colour, the title, and GAME BOY ADVANCE as plain text at the foot.
// No slot numbers on any box. Called with the atlas bound.
static void bake(shelf_box *b, int cell, C3D_Tex *cover, const Tex3DS_SubTexture *cs)
{
	(void)cover; (void)cs;
	const bb_package *pk = b->system->package;
	bool cased = pk && pk->style == BB_PACKAGE_CASE;
	float s = V->rows[R.cell_row].s;
	int cx0, cy0;
	cell_rect(cell, &cx0, &cy0);
	float x = cx0, y = cy0, w = spine_width(b), h = K * px_of(b->h, s), cx = x + w / 2;
	const float p[4][2] = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h } };
	const C3D_Tex *scan = spine_scan(b);
	if (scan) {
		const float uv[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
		const u32 white[4] = { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff };
		tquad((C3D_Tex *)scan, p, uv, white);
		return;
	}
	if (!cased) {
		u32 c = b->accent ? b->accent : clamp_lightness(placeholder(b, 1), .18f, .82f);
		u32 ink = shelf_carton_ink(c);
		rect(x, y, w, h, c);   // printed ink: no white wash or clipped highlight
		u32 edge = shade(c, .82f);   // the cardboard's folded long edges
		rect(x, y, K, h, edge);
		rect(x + w - K, y, K, h, edge);
		spine_title(b->title, cx, y + h * .44f, y + h * .08f, h * .72f, w, ink, 0);
		spine_text("GAME BOY ADVANCE", cx, y + h * .84f, h * .13f, fminf(w / K * .3f, 8), ink, 0);
		return;
	}
	u32 ink = 0xfff6f9fb, dark = 0xff1a1a1a;
	if (b->region == SHELF_REGION_JP) vgrad(x, y, w, h, 0xffeef2f2, 0xffdce2e2);   // the white spine card
	else if (b->region == SHELF_REGION_PAL && hashc(b->title) % 3 == 0) vgrad(x, y, w, h, 0xff7a3a22, 0xff52220f);   // #1b2f6a
	bool black = b->region != SHELF_REGION_JP && !(b->region == SHELF_REGION_PAL && hashc(b->title) % 3 == 0);
	if (black) jewel_black(x, y, w, h);   // exact greys: no green in the RGB565 atlas (room.h)
	// the case's plastic: rounded shading across, lighter rails along both long edges
	else {
		const float q0[4][2] = { { x, y }, { x + w / 2, y }, { x + w / 2, y + h }, { x, y + h } };
		const u32 c0[4] = { 0x40000000, 0x06000000, 0x06000000, 0x40000000 };
		solid4(q0, c0);
		const float q1[4][2] = { { x + w / 2, y }, { x + w, y }, { x + w, y + h }, { x + w / 2, y + h } };
		const u32 c1[4] = { 0x06000000, 0x38000000, 0x38000000, 0x06000000 };
		solid4(q1, c1);
	}
	if (b->region == SHELF_REGION_JP) {
		spine_head_block(b, x, y, w, h, .75f, dark);
		spine_title(b->title, cx, y + h * .515f, y + h * .13f, h * .77f, w, 0xff181818, 0);
		if (b->serial[0]) spine_text(b->serial, cx, y + h * .92f, h * .06f, fminf(w / K * .4f, 7), 0xff303030, 0);
		else rect(cx - even(w * .25f), y + even(h * .945f), 2 * even(w * .25f), 2 * K, 0xff303030);
		rect(x, y, K, h, 0x26000000);
		rect(x + w - K, y, K, h, 0x26000000);
	} else {
		spine_head_block(b, x, y, w, h, 1, ink);
		float t0 = .13f;
		if (b->region == SHELF_REGION_PAL) {
			spine_across("PAL", cx, y + h * .115f, w, h * .03f, ink);
			rect(x, y + even(h * .94f), w, even(h * .025f), 0xffd6dada);   // the publisher band, plain
			t0 = .15f;
		}
		spine_title(b->title, cx, y + h * (t0 + .90f) / 2, y + h * t0, h * (.90f - t0), w, ink, 0);
		u32 rail = black ? grey565(ROOM_JEWEL_RAIL) : 0x30ffffff;
		rect(x, y, K, h, rail);
		rect(x + w - K, y, K, h, rail);
	}
	u32 edge = black ? grey565(ROOM_JEWEL_EDGE) : 0x1affffff;
	rect(x, y, w, K, edge);
	rect(x, y + h - K, w, K, edge);
}

// At the start of the next frame, before any screen target is bound: bake
// missing spines of the shelf in close-up and learn their cover colours.
static void bake_pending(void)
{
	if (!V->nrows) return;
	u64 start = svcGetSystemTick();
	int r = close_row();
	if (r < 0 || !V->rows[r].n || !in_room()) return;
	cells_for(r);
	int visible[2 * ROOM_BOXES + 2], nvisible = room_visible_spines(r, visible, ROOM_BOXES);
	shelf_box *todo[BAKES_PER_FRAME];
	int ntodo = 0, colors = 0;
	shelf_row *row = &V->rows[r];
	// The room list is shared with drawing: actual clamped camera, both eyes,
	// nearest screen center first. Never protect or bake hidden cubby boxes.
	if (V->zoom >= .3f) for (int k = 0; k < nvisible; k++) {
		int c = cell_of(&row->box[visible[k]]);
		if (c >= 0) R.cell[c].used = R.frame;
	}
	int current[ROOM_BOXES];memcpy(current,visible,nvisible*sizeof(*current));
	int candidates = nvisible;
	{
        int target[ROOM_BOXES], ntarget=room_target_spines(r,target,ROOM_BOXES);
        // Before the near boxes fade in, spend the same quota on the destination.
        if (V->zoom < .3f) candidates=0;
        for(int j=0;j<ntarget && candidates<R.cells-2;j++) {
            bool found=false;for(int k=0;k<candidates;k++)if(visible[k]==target[j])found=true;
            if(!found)visible[candidates++]=target[j];
        }
        for(int j=0;j<candidates;j++){int c=cell_of(&row->box[visible[j]]);if(c>=0)R.cell[c].used=R.frame;}

		const shelf_box *held = shelf_pulled_box(V);
		if (held && (uintptr_t)held >= (uintptr_t)row->box &&
            (uintptr_t)held < (uintptr_t)(row->box + row->n) &&
            ((uintptr_t)held - (uintptr_t)row->box) % sizeof(*held) == 0) {
			int c=cell_of(held);if(c>=0)R.cell[c].used=R.frame;
			int i=(int)(held-row->box);bool found=false;
			for(int j=0;j<candidates;j++)if(visible[j]==i)found=true;
			if(!found && i>=0 && i<row->n)visible[candidates++]=i;
		}
		int ahead = room_lookahead(r, visible, candidates);
		if (ahead >= 0) visible[candidates++] = ahead;
	}
	for (int k = 0; k < candidates && svcGetSystemTick() - start < SYSCLOCK_ARM11 / 300; ) {
		shelf_box *b = &row->box[visible[k++]];
		if (!b->color && colors < 1) {
			Tex3DS_SubTexture s;
			C3D_Tex *t = covers_small(b->cover, &s);
			if (t) { average_color(b, t, &s); colors++; }
		}
		int c = cell_of(b);
		if (c >= 0) {
			R.cell[c].used = R.frame;
			// Repaint only on a readiness transition. A painted cover and its tint
			// arrive separately; neither warrants a periodic rebake while waiting.
			Tex3DS_SubTexture sub;
			bool want_cover = !R.cell[c].cover && covers_small(b->cover, &sub);
			bool want_tint = !R.cell[c].tinted && b->color;
			bool want_logo = !R.cell[c].logo && logo_of(b->system);
			if (!want_cover && !want_tint && !want_logo) continue;
		}
		if (ntodo < BAKES_PER_FRAME) todo[ntodo++] = b;
		if (ntodo == BAKES_PER_FRAME) break;
	}
	{
        // Below the near-case fade threshold the wall uses generic bookcase art.
        // Only the destination is warmed there; the wall projection is not drawn.
        bool active=V->zoom>.3f;
        int drawn=active?nvisible:0,baked=0;
        for(int j=0;j<drawn;j++)baked+=cell_of(&row->box[current[j]])>=0;
        static int old_row=-1,old_cells=-1;static unsigned old_layout;
        static bool old_active,reported;static u32 missing_since;
        bool changed=old_row!=r || old_cells!=R.cells || old_layout!=V->layout || old_active!=active;
        if(changed || baked==drawn){missing_since=R.frame;reported=false;}
        bool deficit=baked<drawn && R.frame-missing_since>=60 && !reported;
        if(changed || deficit) {
            log_printf("shelf: cells=%d visible=%d baked=%d compact=%d row=%d cw=%d\n",R.cells,drawn,baked,R.compact,r,R.cw);
            if(deficit) {
                reported=true;
                for(int j=0;j<drawn;j++)if(cell_of(&row->box[current[j]])<0)
                    log_printf("shelf: missing box=%d title=%s reason=no-painted-cell zoom=%.3f\n",current[j],row->box[current[j]].title,V->zoom);
            }
            int marked=0;
            if(changed && R.compact)for(int j=0;j<row->n && marked<8;j++)if(K*px_of(row->box[j].t,row->s)>R.cw && ++marked)
                log_printf("shelf: compact row=%d box=%d title=%s natural=%d cell=%d\n",r,j,row->box[j].title,K*px_of(row->box[j].t,row->s),R.cw);
        }
        old_row=r;old_cells=R.cells;old_layout=V->layout;old_active=active;
    }
	if (!ntodo) return;
	int bound = -1;
	for (int i = 0; i < ntodo; i++) {
		// a few ms of spines a frame, then the rest next frame: a frame of an animation is 16.7 ms
		if (svcGetSystemTick() - start > SYSCLOCK_ARM11 / 300) break;
		shelf_box *b = todo[i];
		int c = cell_of(b);
		Tex3DS_SubTexture s;
		C3D_Tex *cover = covers_small(b->cover, &s);
		if (c < 0 && (c = cell_for(b)) < 0) { log_printf("shelf: no cell cells=%d visible=%d row=%d\n",R.cells,nvisible,r); break; }
		if (cover && !b->color && colors < 1) { average_color(b, cover, &s); colors++; }
		if (c / R.per_atlas != bound) {
			bound = c / R.per_atlas;
			C2D_SceneBegin(R.atlas_rt[bound]);
			C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
		}
		bake(b, c, cover, &s);
		R.cell[c].system = b->system;
		R.cell[c].row = b->row;
		R.cell[c].used = R.frame;
		// Track the actual painted states, including the one eventual tint repaint.
		R.cell[c].cover = cover != NULL;
		R.cell[c].tinted = b->color != 0;
		R.cell[c].logo = logo_of(b->system) != NULL;
	}
	if (bound < 0) return;
	C2D_Flush();
	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
}

// ---------------------------------------------------------------- the held case's back
static C3D_Mtx back_view, back_text_view;
static unsigned back_who(const shelf_box *b) { return hashc(b->title) * 31u + (unsigned)(uintptr_t)b->system; }
// text along a line from (x, y) at angle a (0 across, -PI/2 up), at most `len` long: smaller to
// 70 %, then squeezed
static void back_text(const char *s, float x, float y, float len, float px, u32 ink, float a)
{
	C2D_Text t;
	C2D_TextBufClear(R.text);
	if (!s || !*s || !C2D_TextFontParse(&t, NULL, R.text, s)) return;
	C2D_TextOptimize(&t);
	float sc = text_px(px), sx = 1, tw = 0;
	C2D_TextGetDimensions(&t, sc, sc, &tw, NULL);
	if (tw > len && tw > 0) {
		float k = len / tw;
		if (k >= .7f) sc *= k;
		else { sc *= .7f; sx = k / .7f; }
	}
	C2D_ViewSave(&back_text_view);
	C2D_ViewTranslate(x, y);
	if (a) C2D_ViewRotate(a);
	C2D_DrawText(&t, C2D_WithColor, 0, 0, Z, sc * sx, sc, ink);
	C2D_ViewRestore(&back_text_view);
}
// Made in 256 x 256 units (scaled to the target); the case layout's back half shows all of it
static bool back_bake(const shelf_box *b)
{
	Tex3DS_SubTexture cs;
	C3D_Tex *cover = covers_get(b->cover, &cs);
	unsigned who = back_who(b), tier = cover ? covers_full(b->cover) ? 3u : 2u : 1u;
	if (who == BK.who && tier == BK.tier) return true;
	const bb_package *pk = b->system->package;
	bool gba = !(pk && pk->style == BB_PACKAGE_CASE);
	u32 c0 = b->accent ? b->accent : box_color(b), white = 0xffffffff, paper = rgba(244, 244, 240, 255);
	u32 bg = gba ? shade(c0, .55f) : rgba(22, 23, 27, 255);
	u32 lite = rgba(fminf(255, ch(c0, 0) * 1.25f + 30), fminf(255, ch(c0, 1) * 1.25f + 30), fminf(255, ch(c0, 2) * 1.25f + 30), 255);
	C2D_SceneBegin(BK.rt);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C2D_ViewSave(&back_view);
	C2D_ViewScale(BK.size / 256.0f, BK.size / 256.0f);
	rect(0, 0, 256, 256, bg);
	vgrad(0, 0, 256, 256, with_alpha(white, gba ? 0 : .08f), with_alpha(0xff000000, .25f));
	float x0 = 10, x1 = 246;
	if (gba) {   // the system band down the left
		const float p[4][2] = { { 0, 0 }, { 30, 0 }, { 30, 256 }, { 0, 256 } };
		const u32 c[4] = { rgba(42, 31, 92, 255), rgba(74, 58, 142, 255), rgba(74, 58, 142, 255), rgba(42, 31, 92, 255) };
		solid4(p, c);
		back_text("GAME BOY ADVANCE", 6, 246, 236, 14, rgba(232, 226, 255, 255), -PI / 2);
		x0 = 40;
	} else {     // the insert's spine strips at both edges, the title down the left one
		for (int i = 0; i < 2; i++) {
			float x = i ? 240 : 0;
			rect(x, 0, 16, 256, rgba(12, 12, 14, 255));
			rect(x + 5, 0, 1, 256, with_alpha(white, .14f));
		}
		back_text(b->title, 13, 10, 236, 8, rgba(232, 232, 232, 255), PI / 2);
		x0 = 26; x1 = 230;
	}
	back_text(b->title, x0, 12, x1 - x0, 22, white, 0);
	rect(x0, 42, x1 - x0, 2, lite);
	// two screens cut from the cover art, white-framed
	float sw = (x1 - x0 - 10) / 2, sh = sw * .67f, sy = 54;
	static const float cut[2][2] = { { .15f, .25f }, { .55f, .55f } };
	for (int i = 0; i < 2; i++) {
		float sx = x0 + i * (sw + 10);
		rect(sx - 2, sy - 2, sw + 4, sh + 4, paper);
		if (!cover) { rect(sx, sy, sw, sh, shade(bg, .7f)); continue; }
		float du = cs.right - cs.left, dv = cs.bottom - cs.top, u0 = cs.left + du * cut[i][0], v0 = cs.top + dv * cut[i][1];
		const float p[4][2] = { { sx, sy }, { sx + sw, sy }, { sx + sw, sy + sh }, { sx, sy + sh } };
		const float uv[4][2] = { { u0, v0 }, { u0 + du * .32f, v0 }, { u0 + du * .32f, v0 + dv * .24f }, { u0, v0 + dv * .24f } };
		const u32 c[4] = { white, white, white, white };
		tquad(cover, p, uv, c);
	}
	// the blurb: lines, not text
	for (int i = 0; i < 7; i++) {
		float y = sy + sh + 16 + i * 9;
		if (y > 256 - 34) break;
		rect(x0, y, (x1 - x0) * (i == 6 ? .55f : .92f + .06f * sinf(i * 2.3f)), 3, with_alpha(white, .55f));
	}
	// the foot: a darker strip, a blank rating box, small marks (no codes, no numbers)
	rect(gba ? 30 : 16, 230, 256 - (gba ? 30 : 32), 26, with_alpha(0xff000000, .35f));
	rect(x0, 234, 14, 18, paper);
	for (int i = 0; i < 3; i++) rect(x0 + 24, 236 + i * 6, (x1 - x0) * .5f, 2, with_alpha(white, .5f));
	C2D_ViewRestore(&back_view);
	C2D_Flush();
	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
	BK.who = who;
	BK.tier = tier;
	return true;
}
// the chosen box's back, made ahead while it is browsed (settled) or in the hands
static void back_pending(void)
{
	if (!BK.rt || !V || V->level != SHELF_ONE || V->row >= V->nrows || !room_hands_file()) return;
	const shelf_box *b = shelf_pulled_box(V);
	if (!b) {
		const shelf_row *row = &V->rows[V->row];
		if (!row->n || fabsf(V->cur - V->sel[V->row]) > .3f) return;
		b = &row->box[V->sel[V->row]];
	}
	back_bake(b);
}

// One-shot frame-start hook, chained after the cover uploads (covers_pump).
static void (*chained)(void);
static void pre_draw(void)
{
	void (*next)(void) = chained;
	video_pre_draw = next;
	chained = NULL;
	if (next) next();
	if (R.ready && V) {
		u64 t0 = svcGetSystemTick();
		grain_step();
		bake_pending();
		back_pending();
		R.ticks[1] += svcGetSystemTick() - t0;
		room_pace_add(ROOM_PACE_SPINES, svcGetSystemTick() - t0);
	}
}
static void room_step(void);
// the hands' clip changes blend (hands.h hands_track): fed once a frame, on the shelf's clock
static hands_track hands_tr;
static void hands_track_frame(void)
{
	hands_shelf_clip hc;
	const shelf_box *hb;
	float slot[3];
	bool on = shelf_pulled_box(V) && room_hands_file() && hands_now(&hc, &hb, slot);
	hands_track_set(&hands_tr, on ? hc.clip : NULL, on ? hc.t : 0, V->t);
	room_hands_use_track(&hands_tr, V->t);
}

static void warm_case(void);
void shelf_scene_frame(const shelf_view *v)
{
	V = v;
	R.frame++;
	hands_track_frame();
	if (in_room()) room_step();
	if (video_pre_draw != pre_draw) {
		chained = video_pre_draw;
		video_pre_draw = pre_draw;
	}
}

// ---------------------------------------------------------------- the room
typedef struct { float x, y, s; } bill_t;

// A bookend closes the right end of a shelf that is not full (never the left).
// Its place is on the shelf's boxes' own depth order: scene() paints it with
// them, by distance from the camera, so it never shows through a spine.
#define BOOKEND_GAP 14.0f   // a shelf this much short of the longest one gets a bookend
#define BOOKEND_T 4.0f
#define BOOKEND_H 110.0f


// ---------------------------------------------------------------- the pulled box and the hand
static void case_of(const shelf_box *b, room_hands_case *box, Tex3DS_SubTexture *cs);

// The shelf paints back to front: no depth buffer reads or writes (GPU fill time).
static void depth_off(bool off)
{
	C2D_Flush();
	if (off) C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	else C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
}

// ---------------------------------------------------------------- the top screen
const shelf_box *shelf_pulled_box(const shelf_view *v)
{
	if (v->pull <= .002f || v->prow >= v->nrows || v->pidx >= v->rows[v->prow].n) return NULL;
	return &v->rows[v->prow].box[v->pidx];
}

typedef struct { const shelf_box *b; int r, i; } visible_t;


// ---------------------------------------------------------------- the Library in the room
static struct {
	bool init;
	float t, hy, hv, camx, camy, camvx, camvy;
	int row, cub;
	unsigned layout;
	float t_run;         // when the Library came on (its labels fade in)
	float cub_t;         // when the cubby in view changed (its boxes fade in)
	room_box box[ROOM_BOXES];
} L = { .cub = -1 };

static float row_total(const shelf_row *row) { return row->n ? row->end - X0 + 2 : 0; }

// The boxes of one shelf flowed through its cubbies (room_case_plan): in order, a cubby
// takes boxes until it is full; with more boxes than the cubbies hold they share them
// evenly and each cubby's boxes slide behind its uprights (the UI mock's window).
#define FLOW_MAX 1024
static struct {
	int row, n, k, first;
	unsigned layout;
	const shelf_box *base;
	float cam_lo, cam_hi;               // cached viewport limits, mm (one cubby per system)
	float cw[ROOM_CUBBIES];              // mm of boxes in each cubby
	unsigned char cub[FLOW_MAX];         // a box's cubby (of the shelf's)
	float lx[FLOW_MAX];                  // its centre from the cubby's inside left
} FL = { .row = -1 };
// Keep layout-only projection work out of the cached per-frame flow path.
static PSX3D_COLD void row_camera_bounds(const shelf_row *row)
{
	room_case_geo g;
	// Compute the viewport limits only when the row layout changes. At an
	// end, leave 12 mm beyond the outer upright (or the last box's bookend),
	// including stereo disparity and the selected spine's forward tip.
	room_pose close = room_section_pose(room_scene_file(), 0, 0, row->hmax > 0 ? row->hmax : 130, 0, 0);
	float half = 200 * close.conv / F;
	// room_case_plan_make assigns exactly one cubby to each system row.
	room_case_section(room_scene_file(), FL.first, &g);
	float upright = g.xin0 - g.xl;
	float left = g.xl - upright, right = fminf(g.xr + upright, g.xin0 + FL.cw[0] + 10);
	float lo = left + half - 12, hi = right - half + 12;
	// A short row fits whole: one stable frame, with both ends visible.
	if (lo > hi) lo = hi = (left + right) / 2;
	FL.cam_lo = lo; FL.cam_hi = hi;
}
static bool flow(int r)
{
	const room_case_plan *p = room_scene_case_plan();
	if (!p || r < 0 || r >= p->nsec || r >= V->nrows || p->count[r] <= 0) return false;
	const shelf_row *row = &V->rows[r];
	if (FL.row == r && FL.layout == V->layout && FL.base == row->box && FL.n == row->n && FL.k == p->count[r] && FL.first == p->first[r])
		return true;
	FL.row = r; FL.layout = V->layout; FL.base = row->box; FL.n = row->n; FL.k = p->count[r]; FL.first = p->first[r];
	room_case_geo g;
	room_case_section(room_scene_file(), FL.first, &g);
	float cap = fmaxf(g.inner - 2, row_total(row) / FL.k), x = 0;
	int j = 0;
	memset(FL.cw, 0, sizeof(FL.cw));
	for (int i = 0; i < row->n && i < FLOW_MAX; i++) {
		const shelf_box *b = &row->box[i];
		float gap = i ? (b->x - b->t / 2) - (row->box[i - 1].x + row->box[i - 1].t / 2) : b->x - b->t / 2 - X0;
		gap = clampf(gap, .5f, 6);
		if (x > 0 && x + gap + b->t > cap && j < FL.k - 1) { FL.cw[j] = x + 2; j++; x = 0; }
		x += gap;
		FL.cub[i] = (unsigned char)j;
		FL.lx[i] = x + b->t / 2;
		x += b->t;
	}
	FL.cw[j] = x + 2;
	row_camera_bounds(row);
	return true;
}
// The cubby showing a shelf at camera position cur (boxes, fractional), its geometry and
// how far its boxes have slid (mm)
static bool row_place(int r, float cur, int *cubby, room_case_geo *g, float *off)
{
	const shelf_row *row = &V->rows[r];
	if (!flow(r)) return false;
	*off = 0;
	*cubby = FL.first;
	if (!row->n) return room_case_section(room_scene_file(), *cubby, g);
	int n = row->n < FLOW_MAX ? row->n : FLOW_MAX;
	int i = (int)clampf(roundf(cur), 0, n - 1), i0 = (int)clampf(floorf(cur), 0, n - 1), i1 = i0 + 1 < n ? i0 + 1 : i0;
	int j = FL.cub[i];
	float lc = FL.cub[i0] == FL.cub[i1] && FL.cub[i0] == j ? lerpf(FL.lx[i0], FL.lx[i1], clampf(cur - i0, 0, 1)) : FL.lx[i];
	*cubby = FL.first + j;
	if (!room_case_section(room_scene_file(), *cubby, g)) return false;
	*off = clampf(lc - g->inner / 2, 0, fmaxf(0, FL.cw[j] - g->inner));
	return true;
}
static float row_cur(int r) { return V->level == SHELF_ONE && r == V->row ? V->cur : V->sel[r]; }
// a section's first cubby: its label, the wall view's row
static bool first_cubby(int r, room_case_geo *g)
{
	const room_case_plan *p = room_scene_case_plan();
	return p && r >= 0 && r < p->nsec && room_case_section(room_scene_file(), p->first[r], g);
}
static float hsel_base(void)
{
	room_case_geo g;
	return first_cubby(V->hsel, &g) ? g.base : 0;
}
// Aim at the chosen box until the viewport reaches this row's upright/bookend.
static bool cam_target(int r, float *x, float *base)
{
	int c;
	room_case_geo g;
	float off;
	if (!row_place(r, V->cur, &c, &g, &off)) return false;
	*base = g.base;
	*x = g.cx;
	const shelf_row *row = &V->rows[r];
	if (row->n) {
		int i = (int)clampf(roundf(V->cur), 0, (row->n < FLOW_MAX ? row->n : FLOW_MAX) - 1);
		*x = clampf(g.xin0 + FL.lx[i] - off, FL.cam_lo, FL.cam_hi);
	}
	return true;
}
// once per frame: the wall's row spring, the camera along the shelf, scrolling speed
static void room_step(void)
{
	float dt = L.init ? clampf(V->t - L.t, 0, .1f) : 0;
	L.t = V->t;
	float tx, ty;
	bool aim = V->nrows && cam_target(V->row < V->nrows ? V->row : 0, &tx, &ty);
	if (!L.init) {
		L.init = true;
		L.layout = V->layout;
		L.hy = hsel_base();
		L.hv = 0;
		if (aim) { L.camx = tx; L.camy = ty; }
		L.camvx = L.camvy = 0;
		return;
	}
	// Catalog metadata (Favorite/sort) may rebuild the rows while the camera moves.
	// Preserve the current pose and let the same springs reach the new target.
	L.layout = V->layout;
	ui_motion_settle(&L.hy, &L.hv, hsel_base(), UI_MOTION_SETTLE_RATE, dt);
	if (aim && V->zoom <= .001f) { L.camx = tx; L.camy = ty; L.camvx = L.camvy = 0; }   // walking up starts at the right cubby
	else if (aim) {
		ui_motion_settle(&L.camx, &L.camvx, tx, UI_MOTION_SETTLE_RATE, dt);
		ui_motion_settle(&L.camy, &L.camvy, ty, UI_MOTION_SETTLE_RATE, dt);
	}
}

// every field of a pose eased linearly (here F is this file's focal length macro)
_Static_assert(sizeof(room_pose) == 9 * sizeof(float), "room_pose is nine floats");
static room_pose mix_lin(const room_pose *a, const room_pose *b, float k)
{
	room_pose p;
	const float *x = (const float *)a, *y = (const float *)b;
	float *o = (float *)&p;
	for (int i = 0; i < 9; i++) o[i] = lerpf(x[i], y[i], k);
	return p;
}

static int cubbies_used(void) { const room_case_plan *p = room_scene_case_plan(); return p ? p->used : 0; }

bool shelf_scene_wall_pose(const shelf_view *v, room_pose *out)
{
	if (!in_room()) return false;
	const shelf_view *keep = V;
	V = v;
	float hy = L.init ? L.hy : v->nrows ? hsel_base() : 0;
	*out = room_wall_pose(room_scene_file(), cubbies_used(), hy);
	V = keep;
	return true;
}

// the pulled box's slot in the room: the box standing in its place, its spine plane
static bool room_slot(const shelf_box *b, float slot[3])
{
	int c;
	room_case_geo g;
	float off;
	int i = (int)(b - V->rows[V->prow].box);
	if (i < 0 || i >= FLOW_MAX || !row_place(V->prow, V->prow == V->row ? V->cur : V->pidx, &c, &g, &off)) return false;
	slot[0] = g.xin0 + FL.lx[i] - off; slot[1] = g.base; slot[2] = 5 + b->w / 2;
	return true;
}

// a point of the room on this eye's screen (400x240); false behind the camera
static bool room_project(const room_pose *p, float eye, float x, float y, float z, float *sx, float *sy)
{
	float m[16];
	room_camera(p, eye, m, NULL, NULL, NULL);
	float c[4];
	for (int r = 0; r < 4; r++) c[r] = m[r * 4] * x + m[r * 4 + 1] * y + m[r * 4 + 2] * z + m[r * 4 + 3];
	if (c[3] < 30) return false;
	// the top screen is turned a quarter: clip x is screen up, clip y screen left
	*sx = 200 - 200 * c[1] / c[3];
	*sy = 120 - 120 * c[0] / c[3];
	return true;
}

// The wall's section labels (UI mock wallLabels), on each section's first cubby: readable on
// the wall view, the highlighted one lit; they fade as the camera closes in. A name only, no
// second line (owner 2026-11-05): the bottom card gives the counts.
static void wall_labels(const room_pose *p)
{
	float a = (1 - ui_motion_ease(V->zoom / .5f)) * ui_motion_ease((V->t - L.t_run) / .4f);
	if (a <= .01f) return;
	float eye = ui_eye * (ui_parallax / 1.5f) * p->eye * .5f;
	for (int i = 0; i < V->nrows; i++) {
		room_case_geo g;
		float x, y;
		if (!first_cubby(i, &g) || !room_project(p, eye, g.cx, g.top - 14, -8, &x, &y) || y < -10 || y > 250) continue;
		const shelf_row *row = &V->rows[i];
		bool on = i == V->hsel;
		// a paper label in a brass holder, as on a real cubby: the system's name, the chosen one lit
		const char *label = row->system ? row->system->name : "Favorites";
		float px = fit_px(label, 118, on ? 11.5f : 10), w = text_w(px, label) + 14, h = 14;
		u32 paper = on ? room->paper : shade(room->paper, .68f);
		rect(x - w / 2 - 1.5f, y - 9.5f, w + 3, h + 3, with_alpha(shade(room->brass, on ? 1 : .72f), .96f * a));
		rect(x - w / 2, y - 8, w, h, with_alpha(paper, a));
		text_at(x, y + 3.5f, px, with_alpha(room->plate_ink, a), C2D_AlignCenter, label);
	}
}

void shelf_scene_begin(const shelf_view *v) { L.t_run = v->t; }

static room_pose room_cam(void)
{
	const room_file *f = room_scene_file();
	room_pose wall = room_wall_pose(f, cubbies_used(), L.hy);
	if (V->zoom <= .0005f || V->row >= V->nrows) return wall;
	const shelf_row *row = &V->rows[V->row];
	float pk = ui_motion_ease((V->pull - .3f) / .5f);
	room_pose close = room_section_pose(f, L.camx, L.camy, row->hmax > 0 ? row->hmax : 130, pk, 0);
	room_pose p = room_mix(&wall, &close, V->zoom);
	// a box in the hands: the clip's own camera (straight on, a little from above)
	hands_shelf_clip hc;
	const shelf_box *hb;
	float slot[3], c[7];
	if (hands_now(&hc, &hb, slot) && hc.camera_k > 0 && room_slot(hb, slot) && room_hands_camera(hc.clip, hc.t, c)) {
		room_pose clip = { { c[0] + slot[0], c[1] + slot[1], c[2] + slot[2] }, { c[3] + slot[0], c[4] + slot[1], c[5] + slot[2] }, F, p.eye,
			fmaxf(c[5] - c[2], 1) };
		p = mix_lin(&p, &clip, hc.camera_k);
	}
	return p;
}

/* One visibility/order contract for the atlas scheduler and both stereo eyes.
 * Cubby coordinates alone are insufficient: an overfull cubby can contain 1024
 * boxes, while the clamped end camera sees neighbors away from the selection. */
// Reject offscreen x intervals before per-box projection. The two horizontal
// homogeneous clip planes are linear in x; maximizing over the row's y range
// makes this conservative even during the pitched glide and held-case camera.
static void room_spine_xrange(const float m[16], float base, float height, float *lo, float *hi)
{
 for(int side=-1;side<=1;side+=2) {
  float a=1.06f*m[12]+side*m[4], by=1.06f*m[13]+side*m[5];
  float c=(1.06f*m[14]+side*m[6])*5+1.06f*m[15]+side*m[7];
  c+=fmaxf(by*base,by*(base+height));
  if(a>.000001f)*lo=fmaxf(*lo,-c/a);
  else if(a<-.000001f)*hi=fminf(*hi,-c/a);
 }
}
static bool room_spine_project(const float m[16],float x,float y,float *sx,float *sy)
{
 float w=m[12]*x+m[13]*y+m[14]*5+m[15];if(w<30)return false;
 *sx=200-200*(m[4]*x+m[5]*y+m[6]*5+m[7])/w;
 *sy=120-120*(m[0]*x+m[1]*y+m[2]*5+m[3])/w;return true;
}
static int room_collect_pose(int r,const room_pose *p,int *indices,int cap,int *tested)
{
 int cubby;room_case_geo g;float off;
 if(!row_place(r,row_cur(r),&cubby,&g,&off))return 0;
 const shelf_row *row=&V->rows[r];int cnt=row->n<FLOW_MAX?row->n:FLOW_MAX;
 float matrices[2][16],xmin=g.xl,xmax=g.xr,eye_lo=1e9f,eye_hi=-1e9f;
 for(int eye=0;eye<2;eye++) {
  float e=(eye?1:-1)*(ui_parallax/1.5f)*p->eye*.5f;
  room_camera(p,e,matrices[eye],NULL,NULL,NULL);
  float lo=-1e9f,hi=1e9f;room_spine_xrange(matrices[eye],g.base,row->hmax,&lo,&hi);
  eye_lo=fminf(eye_lo,lo);eye_hi=fmaxf(eye_hi,hi);
 }
 xmin=fmaxf(xmin,eye_lo);xmax=fminf(xmax,eye_hi);
 int cub=cubby-FL.first,lo=0,hi=cnt;
 while(lo<hi){int m=(lo+hi)/2;if(FL.cub[m]<cub)lo=m+1;else hi=m;}
 int first=lo;hi=cnt;
 while(lo<hi){int m=(lo+hi)/2;if(FL.cub[m]<=cub)lo=m+1;else hi=m;}
 int end=lo;lo=first;hi=end;
 while(lo<hi){int m=(lo+hi)/2;float right=g.xin0+FL.lx[m]-off+row->box[m].t/2;if(right<xmin)lo=m+1;else hi=m;}
 first=lo;lo=first;hi=end;
 while(lo<hi){int m=(lo+hi)/2;float left=g.xin0+FL.lx[m]-off-row->box[m].t/2;if(left<=xmax)lo=m+1;else hi=m;}
 end=lo;
 int n=0;float distances[64];if(cap>64)cap=64;
 for(int i=first;i<end;i++) {
  const shelf_box *b=&row->box[i];float x=g.xin0+FL.lx[i]-off;
  if(x-b->t/2<g.xl || x+b->t/2>g.xr)continue;
  (*tested)++;bool visible=false;float distance=1e9f;
  for(int eye=0;eye<2;eye++) {
   float a,y,z,y2;
   if(!room_spine_project(matrices[eye],x-b->t/2,g.base+b->h/2,&a,&y) ||
      !room_spine_project(matrices[eye],x+b->t/2,g.base+b->h/2,&z,&y2))continue;
   if(fmaxf(a,z)>=-12 && fminf(a,z)<=412 && y>=-24 && y<=264)visible=true;
   distance=fminf(distance,fabsf((a+z)/2-200));
  }
  if(!visible)continue;
  int at=n;while(at>0 && distance<distances[at-1])at--;
  if(at>=cap)continue;
  if(n<cap)n++;
  for(int j=n-1;j>at;j--){indices[j]=indices[j-1];distances[j]=distances[j-1];}
  indices[at]=i;distances[at]=distance;
 }
 return n;
}
static __attribute__((unused)) int room_collect_spines(int r, int *indices, int cap)
{
 room_pose p=room_cam();int tested=0;return room_collect_pose(r,&p,indices,cap,&tested);
}
static int room_collect_target(int r,int *indices,int cap,int *tested)
{
 int cubby;room_case_geo g;float off;
 if(!row_place(r,row_cur(r),&cubby,&g,&off))return 0;
 const shelf_row *row=&V->rows[r];if(!row->n)return 0;
 int i=(int)clampf(roundf(row_cur(r)),0,(row->n<FLOW_MAX?row->n:FLOW_MAX)-1);
 float x=clampf(g.xin0+FL.lx[i]-off,FL.cam_lo,FL.cam_hi);
 room_pose p=room_section_pose(room_scene_file(),x,g.base,row->hmax>0?row->hmax:130,0,0);
 return room_collect_pose(r,&p,indices,cap,tested);
}

static int room_visible_spines(int r, int *indices, int cap)
{
 if (!R.visible.valid || R.visible.frame!=R.frame || R.visible.row!=r || R.visible.layout!=V->layout) {
  u64 start=svcGetSystemTick();
  R.visible.tested=0;room_pose p=room_cam();
  R.visible.n=room_collect_pose(r,&p,R.visible.indices,ROOM_BOXES,&R.visible.tested);
  R.visible.target_n=V->zoom<.98f && V->pull<.01f?room_collect_target(r,R.visible.target,ROOM_BOXES,&R.visible.tested):0;
  R.visible.frame=R.frame;R.visible.row=r;R.visible.layout=V->layout;R.visible.valid=true;
  static u64 total,peak;static unsigned samples;
  u64 dt=svcGetSystemTick()-start;total+=dt;if(dt>peak)peak=dt;
  if (++samples==300) {
   log_printf("shelf: visible list avg=%lu us peak=%lu us samples=%u row=%d boxes=%d tested=%d\n",
    (unsigned long)(total/(SYSCLOCK_ARM11/1000000)/samples),(unsigned long)(peak/(SYSCLOCK_ARM11/1000000)),samples,r,V->rows[r].n,R.visible.tested);
   total=peak=0;samples=0;
  }
 }
 int n=R.visible.n<cap?R.visible.n:cap;
 memcpy(indices,R.visible.indices,n*sizeof(*indices));return n;
}
static int room_target_spines(int r,int *indices,int cap)
{
 int discard[ROOM_BOXES];room_visible_spines(r,discard,ROOM_BOXES);
 int n=R.visible.target_n<cap?R.visible.target_n:cap;memcpy(indices,R.visible.target,n*sizeof(*indices));return n;
}
// One immediate neighbor outside the visible interval, in the scroll direction.
// It is considered only after every visible candidate needs no bake.
static int room_lookahead(int r, const int *visible, int n)
{
 if (!n) return -1;
 int lo=visible[0],hi=lo;
 for(int j=1;j<n;j++){if(visible[j]<lo)lo=visible[j];if(visible[j]>hi)hi=visible[j];}
 float delta=V->sel[r]-row_cur(r);
 static float previous;static int direction=1,old_row=-1;static unsigned layout;
 if(old_row!=r || layout!=V->layout){previous=row_cur(r);direction=1;old_row=r;layout=V->layout;}
 if(delta>.001f || row_cur(r)>previous+.001f)direction=1;
 else if(delta<-.001f || row_cur(r)<previous-.001f)direction=-1;
 previous=row_cur(r);
 int i=direction>0?hi+1:lo-1;
 return i>=0 && i<V->rows[r].n && FL.cub[i]==FL.cub[visible[0]]?i:-1;
}

// the hands and the case in the room's camera (room_library.after)
// the case in the hands: the box's cover, its readable spine for the turned case
static void case_of(const shelf_box *b, room_hands_case *box, Tex3DS_SubTexture *cs)
{
	u64 c0 = svcGetSystemTick();
	C3D_Tex *cover = covers_get(b->cover, cs);
	room_pace_add(ROOM_PACE_COVER, svcGetSystemTick() - c0);
	*box = (room_hands_case){ b->cover * 4 + (cover != NULL) * 2, cover, cs, box_color(b), { b->t, b->h, b->w } };
	box->side = side_color(b);
	int c = cell_of(b);
	if (c >= 0) {
		float u0, v0, u1, v1;
		spine_uv(c, b, &u0, &v0, &u1, &v1);
		box->spine = cell_tex(c);
		box->spine_sub = (Tex3DS_SubTexture){ 0, 0, u0, v0, u1, v1 };
		box->cover_id += 1;
		R.cell[c].used = R.frame;
	}
	if (BK.rt && BK.tier && BK.who == back_who(b)) {   // its back is made (maybe still with the small cover's screens)
		box->back = &BK.tex;
		box->back_sub = (Tex3DS_SubTexture){ (u16)BK.size, (u16)BK.size, 0, 1, 1, 0 };
	}
}
// The chosen box's case is made ahead, ~1.5 ms a frame, while nothing is in the hands
static void warm_case(void)
{
	if (!in_room() || !room_hands_file() || V->level != SHELF_ONE || V->zoom < .5f || shelf_pulled_box(V) ||
	    V->row >= V->nrows || !V->rows[V->row].n)
		return;
	const shelf_box *b = &V->rows[V->row].box[V->sel[V->row]];
	if (cell_of(b) < 0) return;   // its spine first (the case shows it when turned)
	static Tex3DS_SubTexture cs;
	room_hands_case box;
	case_of(b, &box, &cs);
	u64 t0 = svcGetSystemTick();
	room_hands_case_warm(&box, SYSCLOCK_ARM11 / 1000 * 3 / 2);
	room_pace_add(ROOM_PACE_PRELOAD, svcGetSystemTick() - t0);
}
// Round 16: the case flies out in 1.1 s (was 2.7 s), so its cover, back and texture must be there when A is pressed:
// a late one showed the plain body or the cover darkened for a moment. Called while A waits (shelf.c want_out), it
// also gives the case texture a larger share of each frame. True when all three are made, or cannot be made.
bool shelf_scene_case_ready(const shelf_view *v)
{
	if (!in_room() || !room_hands_file() || v->row >= v->nrows || !v->rows[v->row].n) return true;
	const shelf_box *b = &v->rows[v->row].box[v->sel[v->row]];
	if (cell_of(b) < 0) return false;
	static Tex3DS_SubTexture cs;
	room_hands_case box;
	case_of(b, &box, &cs);
	unsigned tier = box.cover ? covers_full(b->cover) ? 3u : 2u : 1u;   // what back_bake makes for this cover now
	bool back = !BK.rt || (BK.who == back_who(b) && BK.tier == tier);
	return room_hands_case_warm(&box, SYSCLOCK_ARM11 / 1000 * 4) && back;
}

static void room_hands_cb(const float proj[16], void *ud)
{
	(void)ud;
	hands_shelf_clip hc;
	const shelf_box *hb;
	float slot[3];
	const shelf_box *pulled = shelf_pulled_box(V);
	if (!pulled || !hands_now(&hc, &hb, slot) || !room_slot(pulled, slot)) return;
	float place[12] = { 1, 0, 0, slot[0], 0, 1, 0, slot[1], 0, 0, 1, slot[2] };
	float at[3] = { slot[0], slot[1] + pulled->h / 2, slot[2] - 200 }, to_cam[3] = { 0, .3f, -1 };
	room_scene_hands_light(at, to_cam);
	static Tex3DS_SubTexture cs;
	room_hands_case box;
	case_of(pulled, &box, &cs);
	box.yaw_mode = hc.yaw_mode; box.yaw = hc.yaw;
	u64 h0 = svcGetSystemTick();
	room_hands_draw(hc.clip, hc.t, place, proj, &box);
	room_pace_add(ROOM_PACE_HANDS, svcGetSystemTick() - h0);
	room_hands_light(NULL);
}

static void room_top(void)
{
	room_pose p = room_cam();
	room_library lib = { .hsel = V->level == SHELF_ALL || V->zoom < .98f ? V->hsel : -1, .wall = 1 - ui_motion_ease(V->zoom), .cur = -1,
		.box = L.box, .after = room_hands_cb };
	int cr = V->zoom > .001f && V->row < V->nrows ? V->row : -1, cubby;
	room_case_geo g;
	float off;
	if (cr >= 0 && row_place(cr, V->cur, &cubby, &g, &off)) {
		const shelf_row *row = &V->rows[cr];
		const shelf_box *pulled = shelf_pulled_box(V);
		float zs = ui_motion_ease(V->zoom);
		if (cubby != L.cub) { L.cub = cubby; L.cub_t = V->t; }   // another cubby: its boxes come in over its tile
		lib.cur = cubby;
		lib.fade = ui_motion_ease((V->zoom - .3f) / .4f) * ui_motion_ease((V->t - L.cub_t) / .25f);
		lib.lamp = ui_motion_ease((V->zoom - .6f) / .3f);   // the lamp over the cubby: on as the camera arrives (~0.2 s)
		// Draw exactly the candidates the atlas scheduler considers visible.
		int n = 0, cnt = row->n < FLOW_MAX ? row->n : FLOW_MAX, j = cubby - FL.first;
		int indices[ROOM_BOXES], count=room_visible_spines(cr,indices,ROOM_BOXES);
		for(int k=0;k<count;k++) {
			int i=indices[k];
			const shelf_box *b = &row->box[i];
			float x = g.xin0 + FL.lx[i] - off;
			if (b == pulled) continue;
			float near = i == V->sel[cr] && V->level == SHELF_ONE ? ui_motion_ease(1 - fabsf(V->cur - i) * 3) * zs * (1 - V->pull) : 0;
			const bb_package *pk = b->system->package;
			bool cased = pk && pk->style == BB_PACKAGE_CASE;
			u32 col = box_color(b);
			room_box *o = &L.box[n++];
			*o = (room_box){ x, g.base, 5 - UI_SHELF_TIP_MM * near, b->t, b->h, b->w, NULL, 0, 0, 0, 0,
				b->color ? mixc(col, 0xff000000, .25f) : mixc(placeholder(b, 1), placeholder(b, 0), .55f),
				side_color(b), 1 + (cased ? .35f : 0) * near };
			int c = cell_of(b);
			if (c >= 0) {
				spine_uv(c, b, &o->u0, &o->v0, &o->u1, &o->v1);
				o->spine = cell_tex(c);
				R.cell[c].used = R.frame;   // shared visible set: keep its cell
			}
		}
		// a row its system does not fill: a bookend after its last box (the right end only)
		// (oiled wood as the far view's, room_shelf_texture)
		if (n < ROOM_BOXES && cnt > 0 && FL.cub[cnt - 1] == j && FL.cw[j] + 6 + 4 <= g.inner)
			L.box[n++] = (room_box){ g.xin0 + FL.cw[j] + 2 + 3, g.base, 5, 6, BOOKEND_H, 120, NULL, 0, 0, 0, 0,
				0xff386496, 0xff284a70, 1 };
		lib.nbox = n;
	} else
		L.cub = -1;
	room_scene_draw_library(&p, V->t, &lib);
	room_scene_overlay_begin();
	u64 l0 = svcGetSystemTick();
	wall_labels(&p);
	room_pace_add(ROOM_PACE_LABELS, svcGetSystemTick() - l0);
	room_scene_overlay_end();
}

void shelf_scene_top(const shelf_view *v)
{
	V = v;
	room = theme_room(theme());
	if (room && in_room() && prepare() && v->nrows) {
		if (ui_eye <= 0) {
			C2D_TextBufClear(R.text);
		}
		if (ui_eye <= 0) warm_case();   // after the frame's covers_focus: its cover is asked for in rank
		u64 t0 = svcGetSystemTick();
		room_top();
		u64 dt = svcGetSystemTick() - t0;
		static u64 frame_top;
		frame_top = ui_eye <= 0 ? dt : frame_top + dt;
		R.ticks[0] += dt;
		room_pace_add(ROOM_PACE_TOP, dt);
		if (frame_top > R.peak[0]) R.peak[0] = frame_top;
		R.eyes += ui_eye != 0;
		return;
	}
	// no room (it did not load) or nothing to show yet: the page colour (home shows the room's error card)
	C2D_DrawRectSolid(0, 0, Z, 400, 240, theme()->surface);
}

// ---------------------------------------------------------------- the desk (bottom screen)
static void desk(void)
{
	// the desk grain twice, one above the other (no vertical stretch)
	for (int i = 0; i < 2; i++) {
		float y = i * 120;
		const float p[4][2] = { { 0, y }, { 320, y }, { 320, y + 120 }, { 0, y + 120 } };
		const float uv[4][2] = { { uv_u(G_DESK, 0), uv_v(G_DESK, 0) }, { uv_u(G_DESK, 1), uv_v(G_DESK, 0) },
			{ uv_u(G_DESK, 1), uv_v(G_DESK, 1) }, { uv_u(G_DESK, 0), uv_v(G_DESK, 1) } };
		const u32 c[4] = { room->desk, room->desk, room->desk, room->desk };
		tquad(&R.grain, p, uv, c);
	}
	sprite(&R.glow, 70, 40, 260, 260, with_alpha(room->light, .16f));
	sprite(&R.dark, 120, 90, 330, 330, with_alpha(0xff000000, .5f));
}
// a paper card, turned by ang about its centre; the caller draws on it in card coordinates
static C3D_Mtx card_saved;
static void card_begin(float x, float y, float w, float h, float ang)
{
	C2D_ViewSave(&card_saved);
	C2D_ViewTranslate(x + w / 2, y + h / 2);
	C2D_ViewRotate(ang);
	C2D_ViewTranslate(-w / 2, -h / 2);
	rect(4, 5, w, h, 0x73000000);
	const float p[4][2] = { { 0, 0 }, { w, 0 }, { w, h }, { 0, h } };
	u32 mid = mixc(room->paper, room->paper_edge, .45f);
	const u32 c[4] = { room->paper, mid, room->paper_edge, mid };
	solid4(p, c);
	for (int i = 0; i < 70; i++) rect(rnd(i + 1) * w, rnd(i + 2) * h, 1 + rnd(i + 3) * 3, 1, with_alpha(0xff1e5078, .05f + rnd(i) * .06f));
	// one pitch for the ruled lines and the lines of text (ui_style.h UI_PAPER_LINE): every baseline is on a line
	rect(0, UI_ROOM_RULE_Y, w, 1.2f, room->rule);
	for (float yy = UI_ROOM_RULE_Y + UI_PAPER_LINE; yy < h - UI_ROOM_RULE_FOOT; yy += UI_PAPER_LINE) rect(0, yy, w, 1, room->rule_soft);
	rect(UI_ROOM_MARGIN_X, UI_ROOM_RULE_Y, 1, h - UI_ROOM_RULE_Y, with_alpha(room->rule, .35f));
	text_on_paper = true;
}
static void card_end(void) { text_on_paper = false; C2D_ViewRestore(&card_saved); }
static void star(float x, float y, float r, u32 c)
{
	float p[10][2];
	for (int i = 0; i < 10; i++) {
		float a = -PI / 2 + i * PI / 5, rr = i % 2 ? r * .45f : r;
		p[i][0] = x + cosf(a) * rr; p[i][1] = y + sinf(a) * rr;
	}
	poly(p, 10, c, c, 0, 0, 0, 0);
}
static void stamp(float x, float y, float a)
{
	C2D_ViewSave(&view_saved);
	C2D_ViewTranslate(x, y);
	C2D_ViewRotate(-.22f);
	u32 c = with_alpha(room->stamp, a);
	float o[2][4] = { { -38, -12, 76, 24 }, { -34, -9, 68, 18 } };
	for (int k = 0; k < 2; k++) {
		float lw = k ? 1 : 2, X = o[k][0], Y = o[k][1], W = o[k][2], Hh = o[k][3];
		rect(X, Y, W, lw, c); rect(X, Y + Hh - lw, W, lw, c); rect(X, Y, lw, Hh, c); rect(X + W - lw, Y, lw, Hh, c);
	}
	text_at(-6, 5, 11, c, C2D_AlignCenter, "FAVORITE");
	star(26, 1, 4, c);
	for (int i = 0; i < 40; i++) rect(-40 + rnd(i + 5) * 80, -14 + rnd(i + 9) * 28, 1.5f, 1.5f, with_alpha(room->paper, .3f + rnd(i) * .5f));
	C2D_ViewRestore(&view_saved);
}
static void clip(float x, float y)
{
	u32 c = 0xff5ea3c9;
	line(x + 7, y + 30, x + 7, y + 8, 2.2f, c);
	for (int i = 0; i < 6; i++) {
		float a0 = PI + i * PI / 6, a1 = PI + (i + 1) * PI / 6;
		line(x + 11 + cosf(a0) * 4, y + 8 + sinf(a0) * 4, x + 11 + cosf(a1) * 4, y + 8 + sinf(a1) * 4, 2.2f, c);
		line(x + 10 + cosf(a0 - PI) * 5, y + 34 + sinf(a0 - PI) * 5, x + 10 + cosf(a1 - PI) * 5, y + 34 + sinf(a1 - PI) * 5, 2.2f, c);
	}
	line(x + 15, y + 8, x + 15, y + 34, 2.2f, c);
	line(x + 5, y + 34, x + 5, y + 12, 2.2f, c);
	line(x + 6.4f, y + 26, x + 6.4f, y + 10, .8f, 0x99c8f0ff);
}
// the mini shelf's scale: the whole shelf, but never wider than 400 mm across 296 px
static float strip_len(const shelf_row *r) { return fmaxf(r->end - X0, 400); }
static float strip_x(const shelf_row *r, float x) { return 12 + (x - X0) / strip_len(r) * 296; }
int shelf_scene_strip_hit(const shelf_view *v, float x, float y)
{
	if (v->level != SHELF_ONE || y >= 44) return -1;
	const shelf_row *r = &v->rows[v->row];
	int best = -1;
	float bd = 1e9f;
	for (int i = 0; i < r->n; i++) {
		float d = fabsf(strip_x(r, r->box[i].x) - x);
		if (d < bd) { bd = d; best = i; }
	}
	return best;
}
static float home_row_y(const shelf_view *v, int i) { (void)v; return UI_ROOM_ROWS_Y + i * UI_ROOM_LIST_STEP; }
// rows that fit above the catalog's foot line (more rows: the foot line gives way)
static bool home_foot(const shelf_view *v) { return home_row_y(v, v->nrows) <= UI_ROOM_HINT_Y; }
int shelf_scene_card_hit(const shelf_view *v, float x, float y)
{
	(void)x;
	for (int i = 0; i < v->nrows; i++) {   // a row's band: between the ruled line above it and its own
		float ry = UI_ROOM_CARD_Y + home_row_y(v, i);
		if (y >= ry - UI_PAPER_LINE + 1 && y < ry + 1) return i;   // from the line above to its own line
	}
	return -1;
}

static void shelf_bottom(const shelf_view *v);
void shelf_scene_bottom(const shelf_view *v)
{
	V = v;
	room = theme_room(theme());
	if (!room || !prepare()) {
		C2D_DrawRectSolid(0, 0, Z, 320, 240, theme()->surface);
		return;
	}
	u64 t0 = svcGetSystemTick();
	C2D_TextBufClear(R.text);
	depth_off(true);
	desk();
	shelf_bottom(v);
	depth_off(false);
	u64 dt = svcGetSystemTick() - t0;
	R.ticks[1] += dt;
	room_pace_add(ROOM_PACE_BOTTOM, dt);
	if (dt > R.peak[1]) R.peak[1] = dt;
	if (!R.since) R.since = t0;
	if (++R.frames >= 300) {
		double ms = 1000.0 / SYSCLOCK_ARM11;
		C2Di_Context *ctx = C2Di_GetContext();
		log_printf("shelf: top %.2f ms per frame (peak %.2f, %s), bottom %.2f ms (peak %.2f), %u vertices, %u frames in %.1f s\n",
			R.ticks[0] * ms / R.frames, R.peak[0] * ms, R.eyes ? "both eyes" : "one eye", R.ticks[1] * ms / R.frames, R.peak[1] * ms,
			(unsigned)ctx->vtxBufPos, (unsigned)R.frames, (t0 - R.since) * ms / 1000);
		R.ticks[0] = R.ticks[1] = R.peak[0] = R.peak[1] = 0;
		R.frames = R.eyes = 0;
		R.since = t0;
	}
}

// a list row's highlight and pointer (the catalog, the place card's lists): the shared highlighter stroke behind the
// row's text from x0 to x1 (drawn out when the row becomes the selection; `site` tells the two lists apart), and
// the pointer at its capitals
static void list_mark(float y, float x0, float x1, int row, int site)
{
	static int marked = -1;
	static u64 since;
	u64 now = svcGetSystemTick();
	if (marked != site * 256 + row) { marked = site * 256 + row; since = now; }
	float progress = (float)(now - since) / SYSCLOCK_ARM11 / UI_MARK_DRAW_S;
	ui_mark_row(x0, x1, y, row, progress, 1, Z);
	float m = y - UI_PAPER_CAP / 2.0f;
	tri(UI_ROOM_TEXT_X - 16, m - 5, 0xff1c5a8a, UI_ROOM_TEXT_X - 8, m, 0xff1c5a8a, UI_ROOM_TEXT_X - 16, m + 5, 0xff1c5a8a);
}
// the width text_fit draws s at
static float text_fit_w(float width, float px, float min_px, const char *s)
{
	float scale;
	const char *t = ui_fit_ex(NULL, s, text_px(px), text_px(min_px), width, &scale);
	return t[0] ? ui_text_width(NULL, scale, t) : 0;
}
// an info row: the label from the left, its value right-aligned (the label keeps at least UI_ROOM_LABEL_MAX of
// the line when both are long; both are fitted at the floor and cut with "...")
static void info_row(float y, const char *l, const char *v, u32 c)
{
	const float tw = UI_ROOM_TEXT_R - UI_ROOM_TEXT_X;
	if (!v) { if (l) text_fit(UI_ROOM_TEXT_X, y, tw, UI_ROOM_LINE_PX, UI_ROOM_MIN_PX, c, l); return; }
	float lw = l ? text_w(UI_ROOM_LINE_PX, l) : 0, vw = text_w(UI_ROOM_VALUE_PX, v), room_v = tw - UI_ROOM_GAP - fminf(lw, tw * UI_ROOM_LABEL_MAX);
	if (vw > room_v) vw = room_v;
	if (l) text_fit(UI_ROOM_TEXT_X, y, tw - UI_ROOM_GAP - vw, UI_ROOM_LINE_PX, UI_ROOM_MIN_PX, c, l);
	text_fit_at(UI_ROOM_TEXT_R, y, vw, UI_ROOM_VALUE_PX, UI_ROOM_MIN_PX, c, C2D_AlignRight, v);
}
static void shelf_bottom(const shelf_view *v)
{
	u32 ink = room->ink, soft = room->ink_soft, red = with_alpha(room->rule, 1);
	bool notice = v->notice && v->t - v->notice_t < 2.5f;
	const float tw = UI_ROOM_TEXT_R - UI_ROOM_TEXT_X;
	if (v->level == SHELF_ALL || !v->nrows) {
		card_begin(UI_ROOM_CARD_X, UI_ROOM_CARD_Y, UI_ROOM_CARD_W, UI_ROOM_CARD_H, -.012f);
		card_title("Collection", tw);
		for (int i = 0; i < v->nrows; i++) {
			float y = home_row_y(v, i);
			bool on = i == v->hsel;
			float x = UI_ROOM_TEXT_X + (v->rows[i].system ? 0 : UI_ROOM_STAR_W);
			if (on) list_mark(y, UI_ROOM_TEXT_X, x + text_fit_w(UI_ROOM_TEXT_R - x, UI_ROOM_ROW_PX, UI_ROOM_MIN_PX, v->rows[i].name), i, 0);
			if (!v->rows[i].system) star(UI_ROOM_TEXT_X + 6, y - UI_ROOM_STAR_UP, UI_ROOM_STAR_R, on ? ink : soft);
			text_fit(x, y, UI_ROOM_TEXT_R - x, UI_ROOM_ROW_PX, UI_ROOM_MIN_PX, on ? ink : soft, v->rows[i].name);   // no count: nothing to act on
		}
		float free_y = home_row_y(v, v->nrows);   // under the last row
		if (!v->total && !notice) {   // an empty library (first run, or the folder holds no games): what to do, for every system
			char gone[PATH_LEN + 24];
			snprintf(gone, sizeof(gone), "Can't read %s", bb_unreadable_dir);
			char where[96];
			ui_empty_help(where, sizeof(where), g_cfg.games_dir, true);   // where games go, and Y (the bar's rescan) to look again
			const char *help = bb_unreadable_dir[0] ? gone : where;   // the saved folder is kept, not forgotten
			int room_lines = (int)((UI_ROOM_HINT_Y - UI_ROOM_LINE_STEP - free_y) / UI_ROOM_LINE_STEP) + 1;
			text_lines(UI_ROOM_TEXT_X, free_y, UI_ROOM_LINE_STEP, tw, UI_ROOM_LINE_PX, room_lines < 1 ? 1 : room_lines, ink, help);
		}
		// the foot line only for something to act on: a notice, or a folder that cannot be read (no counts, no
		// "Last played": owner 2026-11-05)
		char last[PATH_LEN + 32];
		last[0] = 0;
		if (notice) snprintf(last, sizeof(last), "%s", v->notice);
		else if (bb_unreadable_dir[0] && v->total) {
			// the saved folder's end (whole characters); Rescan and Settings are on the bar
			char tail[24 * 4 + 8];   // 24 whole characters of up to 4 bytes, "..." and the end
			ui_path_tail(tail, sizeof(tail), bb_unreadable_dir, 24);
			snprintf(last, sizeof(last), "Can't read: %s", tail);
		}
		if (last[0] && (home_foot(v) || notice)) text_fit(UI_ROOM_TEXT_X, UI_ROOM_HINT_Y, tw, UI_ROOM_HINT_PX, UI_ROOM_MIN_PX, red, last);
		card_end();
		return;
	}
	const shelf_row *row = &v->rows[v->row];
	// the mini shelf: every spine at scale, a brass pointer under the chosen one (no letters: too small to read)
	rect(8, 6, 304, 34, 0x66000000);
	for (int i = 0; i < row->n; i++) {
		const shelf_box *b = &row->box[i];
		float x = strip_x(row, b->x - b->t / 2), bw = fmaxf(2, b->t / strip_len(row) * 296 - .6f), hh = b->h * .19f;
		bool on = i == v->sel[v->row];
		u32 c = box_color(b);
		rect(x, 34 - hh - (on ? 3 : 0), bw, hh, rgba(ch(c, 0) * .7f + 20, ch(c, 1) * .7f + 20, ch(c, 2) * .7f + 20, 255));
		if (on) tri(x + bw / 2, 35, room->brass, x + bw / 2 - 3, 40, room->brass, x + bw / 2 + 3, 40, room->brass);
	}
	vgrad(8, 34, 304, 3, shade(room->wood, .85f), shade(room->wood, .45f));
	card_begin(UI_ROOM_GAME_X, UI_ROOM_GAME_Y, UI_ROOM_GAME_W, UI_ROOM_GAME_H, -.014f);
	if (!row->n) {
		card_title("Nothing here yet", tw);
		text_lines(UI_ROOM_TEXT_X, UI_ROOM_ROWS_Y, UI_ROOM_LINE_STEP, tw, UI_ROOM_LINE_PX, 2, soft, "Games you star wait here for you.");
		card_end();
		clip(28, 40);
		return;
	}
	const shelf_box *b = &row->box[v->sel[v->row]];
	// the title in the header row (one line: smaller down to the floor, then cut with "..."; the focused game's whole
	// name is on its box above), then the foot line: no system kicker, no "Last played" / "Play time" / "Never played"
	card_title(b->title, tw);
	bool foot = true;
	if (b->fav) stamp(UI_ROOM_STAMP_X, UI_ROOM_STAMP_Y, .82f);   // over the foot line's right end, clear of the rows
	if (notice) text_fit(UI_ROOM_TEXT_X, UI_ROOM_GAME_HINT_Y, tw, UI_ROOM_HINT_PX, UI_ROOM_MIN_PX, red, v->notice);
	else if (foot) {   // the L and R glyphs and what they do; nothing when they do nothing (no hands to turn the box)
		bool out = v->pto == 1 || v->pull > .002f;
		const char *lr = out ? (shelf_scene_hands() ? UI_HINT_LR_TURN : NULL) : UI_HINT_LR_JUMP;
		if (lr) text_fit(UI_ROOM_TEXT_X, UI_ROOM_GAME_HINT_Y, tw, UI_ROOM_HINT_PX, UI_ROOM_MIN_PX, soft, lr);
	}
	card_end();
	clip(28, 40);
}

// The cold start (k 0..1): the room comes up out of the dark, the lamp first. The black cover has a
// lit disc round the floor lamp that grows until it holds the whole screen (10 x 6 quads of 40 px,
// each corner's alpha from its distance to the lamp).
void shelf_scene_intro(float k)
{
	if (k >= 1) return;
	const float lx = 150, ly = 62, soft = 150;
	float r = soft + 20 + ui_motion_ease(k) * 300;   // lit round the lamp from the first frame, the farthest corner (307 px from it) at the end
	float a[7][11];
	for (int j = 0; j < 7; j++)
		for (int i = 0; i < 11; i++) {
			float x = i * 40, y = j * 40, d = sqrtf((x - lx) * (x - lx) + (y - ly) * (y - ly));
			a[j][i] = clampf((d - (r - soft)) / soft, 0, 1);
		}
	for (int j = 0; j < 6; j++)
		for (int i = 0; i < 10; i++) {
			float x = i * 40, y = j * 40;
			const float p[4][2] = { { x, y }, { x + 40, y }, { x + 40, y + 40 }, { x, y + 40 } };
			u32 c[4] = { with_alpha(0xff000000, a[j][i]), with_alpha(0xff000000, a[j][i + 1]),
				with_alpha(0xff000000, a[j + 1][i + 1]), with_alpha(0xff000000, a[j + 1][i]) };
			solid4(p, c);
		}
}

// The app's first picture, drawn as soon as the screens work, long before the room can be: the dark
// with the floor lamp's glow where the lamp will be (shelf_scene_intro(0) over the lamp's warm light),
// the desk's dark wood below. The room's own lamp-first fade-up takes over from the same picture.
void shelf_scene_dawn(bool top)
{
	if (!top) {
		rect(0, 0, 320, 240, rgba(26, 15, 8, 255));
		return;
	}
	rect(0, 0, 400, 240, rgba(112, 68, 30, 255));
	shelf_scene_intro(0);
}

// The room's bottom screen (shelf.h shelf_card): the desk, one card, as R8's
// catalogue card above, so the room and the bookshelf share one look.
// The focused place's sign at home (UI mock R10 focusTag): a paper tag hanging over the
// place on a thread, its name and one line, swaying a little; k fades it in.
void shelf_scene_card(const shelf_card *c)
{
	room = theme_room(theme());
	if (!room || !prepare()) {
		C2D_DrawRectSolid(0, 0, Z, 320, 240, theme()->surface);
		return;
	}
	u32 ink = room->ink, soft = room->ink_soft;
	const float tw = UI_ROOM_TEXT_R - UI_ROOM_TEXT_X;
	C2D_TextBufClear(R.text);
	depth_off(true);
	desk();
	card_begin(UI_ROOM_CARD_X, UI_ROOM_CARD_Y, UI_ROOM_CARD_W, UI_ROOM_CARD_H, -.012f);
	card_title(c->title, c->picture ? UI_ROOM_TITLE_W_PICTURE : tw);   // clear of the picture's tape
	for (int i = 0; c->pages > 1 && i < c->pages; i++) {   // the page dots on the foot line: this page filled, the others rings
		float x = UI_ROOM_TEXT_R - (c->pages - 1 - i) * UI_ROOM_DOT_STEP - UI_ROOM_DOT_R, y = UI_ROOM_DOT_Y;
		C2D_DrawCircleSolid(x, y, Z, UI_ROOM_DOT_R, ink);
		if (i + 1 != c->page) C2D_DrawCircleSolid(x, y, Z, UI_ROOM_DOT_R - 1.2f, room->paper);
	}
	if (c->picture) {   // the save picture, taped to the card
		static const Tex3DS_SubTexture sub = { 256, 192, 0, 1, 1, 1 - 192 / 256.0f };
		const float px = UI_ROOM_PICTURE_X, py = UI_ROOM_PICTURE_Y, pw = UI_ROOM_PICTURE_W, ph = pw * 3 / 4;
		rect(px - 2, py - 2, pw + 4, ph + 4, 0xff201810);
		C2D_DrawImageAt((C2D_Image){ c->picture, &sub }, px, py, Z, NULL, pw / 256.0f, ph / 192.0f);
		if (c->picture_label) {
			rect(px, py + ph / 2 - UI_PAPER_LINE / 2, pw, UI_PAPER_LINE, 0x99000000);   // on the picture, not the paper
			text_at(px + pw / 2, py + ph / 2 + UI_PAPER_CAP / 2, UI_ROOM_TINY_PX, 0xffffffff, C2D_AlignCenter, c->picture_label);
		}
		rect(px + 15, py - 5, 30, 7, with_alpha(room->paper_edge, .8f));   // tape
	}
	float y = UI_ROOM_ROWS_Y;
	for (int i = 0; i < c->nrows && i < SHELF_CARD_ROWS; i++) {
		const char *l = c->row[i].label, *v = c->row[i].value;
		if (c->list) {   // R8's catalogue rows
			bool on = c->row[i].on;
			float x = UI_ROOM_TEXT_X + (c->row[i].star ? UI_ROOM_STAR_W : 0);
			float vw = v ? text_w(UI_ROOM_VALUE_PX, v) + UI_ROOM_GAP : 0;
			if (on) list_mark(y, UI_ROOM_TEXT_X, v ? UI_ROOM_TEXT_R : x + (l ? text_fit_w(UI_ROOM_TEXT_R - vw - x, UI_ROOM_ROW_PX, UI_ROOM_MIN_PX, l) : 0), i, 1);
			if (c->row[i].star) star(UI_ROOM_TEXT_X + 6, y - UI_ROOM_STAR_UP, UI_ROOM_STAR_R, on ? ink : soft);
			if (l) text_fit(x, y, UI_ROOM_TEXT_R - vw - x, UI_ROOM_ROW_PX, UI_ROOM_MIN_PX, on ? ink : soft, l);
			if (v) text_at(UI_ROOM_TEXT_R, y, UI_ROOM_VALUE_PX, soft, C2D_AlignRight, v);
			y += UI_ROOM_LIST_STEP;
		} else {
			if (c->row[i].icon)   // the row's icon in the margin, centred on its capitals
				ui_icon_draw(c->row[i].icon, UI_ROOM_MARGIN_X / 2.0f, y - UI_PAPER_CAP / 2.0f, UI_ROOM_ICON_PX, soft, room->paper);
			info_row(y, l, v, soft);
			y += UI_ROOM_LINE_STEP;
		}
	}
	if (c->hint && y <= UI_ROOM_HINT_Y - UI_ROOM_LINE_STEP)   // up to two lines (the empty library's help), ending on the foot line
		text_lines(UI_ROOM_TEXT_X, text_break(c->hint, tw, UI_ROOM_HINT_PX) ? UI_ROOM_HINT_Y - UI_ROOM_LINE_STEP : UI_ROOM_HINT_Y, UI_ROOM_LINE_STEP, tw, UI_ROOM_HINT_PX, 2, soft, c->hint);
	card_end();
	depth_off(false);
}

PSX3D_COLD float shelf_scene_line_width(const char *s) { return text_w(UI_ROOM_LINE_PX, s); }

// The desk the room's cards lie on, without a card (the Continue postcard, postcards.inc). Nothing is written
// to the depth buffer: what is drawn next lies on it. load false: never loads the room's textures (a game's
// Continue question comes after shelf_scene_release; the game needs that memory): without them the desk is its
// colour without the grain, lit as desk() lights it (corners measured on a real room still).
void shelf_scene_desk(bool load)
{
	room = theme_room(theme());
	bool grain = room && (load ? prepare() : R.ready);
	depth_off(true);
	if (grain) desk();
	else if (room) C2D_DrawRectangle(0, 0, Z, 320, 240, shade(room->desk, .68f), shade(room->desk, .42f), shade(room->desk, .62f), shade(room->desk, .55f));
	else C2D_DrawRectSolid(0, 0, Z, 320, 240, theme()->surface);
	depth_off(false);
}

// Other menus in a room theme (settings, dialogs): the room's wall above and
// the desk below, with the lamp's light.
void shelf_scene_backdrop(bool bottom)
{
	room = theme_room(theme());
	if (!room || !prepare()) {
		C2D_DrawRectSolid(0, 0, Z, bottom ? 320 : 400, 240, theme()->surface);
		return;
	}
	if (bottom) {
		desk();
		rect(0, 0, 320, 240, 0x40000000);
		return;
	}
	const float p[4][2] = { { 0, 0 }, { 400, 0 }, { 400, 240 }, { 0, 240 } };
	const float uv[4][2] = { { uv_u(G_WALL, 0), uv_v(G_WALL, 0) }, { uv_u(G_WALL, 1), uv_v(G_WALL, 0) },
		{ uv_u(G_WALL, 1), uv_v(G_WALL, 1) }, { uv_u(G_WALL, 0), uv_v(G_WALL, 1) } };
	const u32 c[4] = { shade(room->wall, .8f), shade(room->wall, .8f), shade(room->wall, .7f), shade(room->wall, .7f) };
	tquad(&R.grain, p, uv, c);
	vgrad(0, 206, 400, 34, shade(room->floor, .6f), shade(room->floor, .4f));
	rect(0, 204, 400, 4, room->skirting);
	sprite(&R.glow, 90, 70, 300, 300, with_alpha(room->light, .3f));
	sprite(&R.dark, 160, 100, 340, 340, room->shade);
}
