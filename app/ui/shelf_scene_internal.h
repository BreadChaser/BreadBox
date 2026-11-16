/* Private to the shelf scene (shelf_scene*.c, the room's Library and its desk):
 * constants, the shared state and the small helpers every part inlines. */
#ifndef SHELF_SCENE_INTERNAL_H
#define SHELF_SCENE_INTERNAL_H
#include "shelf_carton.h"
#include "bb_index.h"
#include "ui_fit.h"
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
#define X0 40.0f
#define Z .5f
#define PI 3.14159265f

#define GRAIN_W 512
#define GRAIN_H 512
#define GRAIN_FILE PSX3D_DIR "/cache/shelf-grain-1.l8"
// Grain atlas regions (x, y, w, h): grey wood, plaster and brass, tinted by the room.
enum { G_BOARD, G_LIP, G_CROWN, G_BACK, G_UPRIGHT, G_FLOOR, G_WALL, G_DESK, G_PLATE, G_COUNT };
extern const short shelf_scene_grain_region[G_COUNT][4];

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
#define TEXT_CACHE 96

#define LOGOS 4
typedef struct { const bb_system_t *system; C3D_Tex tex; float u, v, aspect; bool ok; } logo_t;

typedef struct shelf_scene_res {
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
} shelf_scene_res;
extern shelf_scene_res shelf_scene_state;
extern C3D_Mtx shelf_scene_view_saved;   // around rotated text and artwork
typedef struct { C3D_Tex tex; C3D_RenderTarget *rt; int size; unsigned who, tier; } shelf_scene_back;
extern shelf_scene_back shelf_scene_case_back;   // the held case's back (back_bake)
extern const theme_room_t *shelf_scene_room;
extern const shelf_view *shelf_scene_view;

// The Library in the room's own bookcase (a room file with a case): shelf r is the
// case's section r, the room is drawn around it (room_scene_draw_library) and only
// the desk below is painted here. UI mock R10 (#bb10=1): libStep, libPose, libraryDraw.
static inline bool in_room(void) { const room_file *f = room_scene_file(); return f && room_case_sections(f) > 0; }
// shelf_pulled_box and shelf_scene_hands for the scene's own per-frame callers (inlined, as before the split)
static inline const shelf_box *shelf_scene_pulled(const shelf_view *v)
{
	if (v->pull <= .002f || v->prow >= v->nrows || v->pidx >= v->rows[v->prow].n) return NULL;
	return &v->rows[v->prow].box[v->pidx];
}
static inline bool shelf_scene_hands_open(void) { return theme_room(theme()) && room_hands_open(); }

static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float rnd(int i) { float x = sinf(i * 127.1f + 311.7f) * 43758.5453f; return x - floorf(x); }
static inline int ch(u32 c, int i) { return (c >> (8 * i)) & 255; }
static inline u32 with_alpha(u32 c, float a) { return (c & 0x00ffffffu) | (u32)clampf(a * 255, 0, 255) << 24; }
// colours: one private copy per file, so each file folds its constant colours as before the split
static __attribute__((unused)) u32 shelf_scene_rgba(int r, int g, int b, int a)
{
	return (u32)clampf(r, 0, 255) | (u32)clampf(g, 0, 255) << 8 | (u32)clampf(b, 0, 255) << 16 | (u32)clampf(a, 0, 255) << 24;
}
static __attribute__((unused)) u32 shelf_scene_shade(u32 c, float k) { return shelf_scene_rgba(ch(c, 0) * k, ch(c, 1) * k, ch(c, 2) * k, ch(c, 3)); }
static __attribute__((unused)) u32 shelf_scene_mix(u32 a, u32 b, float t)
{
	return shelf_scene_rgba(lerpf(ch(a, 0), ch(b, 0), t), lerpf(ch(a, 1), ch(b, 1), t), lerpf(ch(a, 2), ch(b, 2), t), lerpf(ch(a, 3), ch(b, 3), t));
}
static inline u32 texel(u32 x, u32 y, u32 w)
{
	return (((y >> 3) * (w >> 3) + (x >> 3)) << 6) |
		(x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}

// shelf_scene_draw.c
void shelf_scene_solid4(const float p[4][2], const u32 c[4]);
void shelf_scene_tquad(C3D_Tex *t, const float p[4][2], const float uv[4][2], const u32 c[4]);
const C2D_Text *shelf_scene_text_get(const char *s, C2D_Text *tmp);
void shelf_scene_text_at(float x, float y, float px, u32 c, u32 flags, const char *s);
bool shelf_scene_prepare_gpu_now(void);
// the textures, atlases and text buffers; the made-or-failed check stays inline in every per-frame caller
static inline bool shelf_scene_prepare_gpu(void)
{
	if (shelf_scene_state.ready || shelf_scene_state.tried) return shelf_scene_state.ready;
	return shelf_scene_prepare_gpu_now();
}
// shelf_scene_wood.c
void shelf_scene_grain_start(C3D_Tex *t);
void shelf_scene_grain_step(void);
bool shelf_scene_radial(C3D_Tex *t, bool dark);
// shelf_scene_spines.c
void shelf_scene_bake_pending(void);
int shelf_scene_cell_of(const shelf_box *b);
void shelf_scene_spine_uv(int cell, const shelf_box *b, float *u0, float *v0, float *u1, float *v1);
// shelf_scene.c
int shelf_scene_visible_spines(int r, int *indices, int cap);

// The boxes of one shelf flowed through its cubbies (room_case_plan): in order, a cubby
// takes boxes until it is full; with more boxes than the cubbies hold they share them
// evenly and each cubby's boxes slide behind its uprights (the UI mock's window).
#define FLOW_MAX 1024
typedef struct shelf_scene_flow {
	int row, n, k, first;
	unsigned layout;
	const shelf_box *base;
	float cam_lo, cam_hi;               // cached viewport limits, mm (one cubby per system)
	float cw[ROOM_CUBBIES];              // mm of boxes in each cubby
	unsigned char cub[FLOW_MAX];         // a box's cubby (of the shelf's)
	float lx[FLOW_MAX];                  // its centre from the cubby's inside left
} shelf_scene_flow;
extern shelf_scene_flow shelf_scene_flowed;

static inline float row_cur(int r) { return shelf_scene_view->level == SHELF_ONE && r == shelf_scene_view->row ? shelf_scene_view->cur : shelf_scene_view->sel[r]; }

static inline void solid(const float p[4][2], u32 c) { const u32 cc[4] = { c, c, c, c }; shelf_scene_solid4(p, cc); }
static inline void rect(float x, float y, float w, float h, u32 c) { const float p[4][2] = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h } }; solid(p, c); }
static inline void vgrad(float x, float y, float w, float h, u32 a, u32 b)
{
	const float p[4][2] = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h } };
	const u32 c[4] = { a, a, b, b };
	shelf_scene_solid4(p, c);
}
static inline u32 hashc(const char *s)
{
	u32 h = 2166136261u;
	for (; *s; s++) h = (h ^ (u8)*s) * 16777619u;
	return h;
}
// text in the scene, at the scene's depth (shared text sits at 0.7)
static inline float text_px(float px) { return px / 30.0f; }
static inline float text_w(float px, const char *s)
{
	C2D_Text tmp;
	const C2D_Text *t = shelf_scene_text_get(s, &tmp);
	float w = 0;
	if (t) C2D_TextGetDimensions(t, text_px(px), text_px(px), &w, NULL);
	return w;
}
static inline float fit_px(const char *s, float width, float px)
{
	float w = text_w(px, s);
	return w > width ? px * width / w : px;
}
// placeholder art colours for boxes without a cover yet (the mock's palettes; one table, shelf_scene.c)
extern const u32 shelf_scene_placeholder_pal[8][2];
static inline u32 placeholder(const shelf_box *b, int which)
{
	return shelf_scene_placeholder_pal[hashc(b->title) % 8][which];
}
static inline u32 box_color(const shelf_box *b) { return b->color ? b->color : placeholder(b, 0); }
// A box's outer sides (top, bottom, the open edge), the same on the shelf, pulled and in the hand:
// a jewel case's dark plastic, a carton's own printed colour (never white, never brightened)
static inline u32 side_color(const shelf_box *b)
{
	const bb_package *pk = b->system->package;
	return pk && pk->style == BB_PACKAGE_CASE ? 0xff24201e : box_color(b);
}
static inline C3D_Tex *cell_tex(int c) { return &shelf_scene_state.atlas[c / shelf_scene_state.per_atlas]; }

#endif
