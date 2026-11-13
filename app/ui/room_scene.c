// BreadBox room main menu: the GPU side (room.h). The room file (BBR v3, the Lamplight
// room) is read once from romfs; the room style's atlas is tiled into a texture, its
// vertices (with the style's baked colours) and indices are copied into linear memory and
// drawn as they are (no per-frame vertex work). One eye is a handful of citro3d draws
// (tools/room/source/SPEC.md), one texture stage each:
//   1 the room: every model of the file, one range, palette slots 0-11, light baked per vertex
//   2 the TV picture (the Continue thumbnail)
//   3 the bookcase from far away: one quad per system section + brass plates, lit by the style's probes
//   4 glows and steam (additive, built facing the camera in the shader)
//   5 the final pass: the PS1's 4x4 ordered dither down to RGB565 (two full-screen quads: add, AND)
// Menu-only: released with the other menu buffers before a game.
#include "app_policy.h"
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "ui_style.h"
#include "app_log.h"
#include "platform_video.h"
#include "playlog.h"
#include "app_main.h"
#include "bb_index.h"
#include "bb_memprobe.h"
#include "bb_registry.h"
#include "room.h"
#include "room_walls.h"
#include "theme.h"
#include "room_shbin.h"

#define ROOM_PATH "romfs:/room/room.bbr"
#define CASE_MAX (ROOM_CUBBIES * 18 * 2)   // per cubby its back, boxes or decor, a plate; per section its lamp
#define LAMP_VERTS 256                   // the open cubby's lit back and the lamp's face (room_lamp_mesh)
#define BOX_MAX 40                       // real boxes of one section (UI mock: at most 31)
#define BOX_VERTS (BOX_MAX * 4 * 6)      // spine, top, two sides
#define ANIM_MAX 16                      // steam: moved on the CPU
#define ROOM_STYLES 4                    // room styles a file may have (Day, Night, ...)
#define BUF_ROOM (&S.buf_style[S.style])

static struct {
	bool tried, ok;
	void *file;
	room_file f;
	room_vertex *vb, *case_vb;          // vb: the room's positions and uvs (its colour bytes unused: S.cols)
	u16 *ib;
	int case_n, case_backs, case_first[ROOM_CUBBIES + 1];
	room_case_plan plan;
	room_vertex *box_vb[2];              // the real boxes, rebuilt every frame (double buffered)
	C3D_BufInfo buf_box[2];
	room_vertex *lamp_vb[2];             // the open cubby under its lamp (double buffered, made again as the lamp fades)
	C3D_BufInfo buf_lamp[2];
	int lamp_n, lamp_k;
	float lamp_now[3];                   // the lamp's light at the box in the hands (hands_light)
	unsigned box_frame, anim_frame;
	// steam over the mug: the PICA has no sine, so its few sprites are moved here and
	// drawn from a small buffer per frame
	struct { room_vertex v[4]; u16 idx[4]; float ph; } anim[ANIM_MAX];
	int nanim;
	room_vertex *anim_vb[2];
	u16 *anim_ib;
	C3D_BufInfo buf_anim[2];
	u32 room_first, room_count, tv_first, tv_count, sprite_first, sprite_count;
	// every room style: its own copy of the vertices (with its baked colours) and its atlas,
	// made at load; a style change only picks other ones (nothing is rewritten under the GPU)
	int style, nstyles;
	u8 *cols[ROOM_STYLES];              // every style's baked colours (rgba per vertex): its second vertex buffer
	C3D_BufInfo buf_style[ROOM_STYLES];
	C3D_Tex atlas_tex[ROOM_STYLES], *atlas[ROOM_STYLES];
	C3D_Tex shelf, blank, tv, dither;
	room_vertex *post_vb;               // the final pass's screen quad
	u16 *post_ib;
	C3D_BufInfo buf_post;
	bool tv_ok, tv_static;              // the TV's baked picture; static: a channel with no picture
	DVLB_s *dvlb;
	shaderProgram_s prog;
	C3D_AttrInfo attr;
	C3D_BufInfo buf_case;
	int u_proj, u_texscale, u_right, u_up, u_eye, u_corner, u_pal, u_tint, u_sprite, u_fadek, u_lscale;
	// the room is drawn at the screen's own 400x240 into this target, then put on the
	// (2x2 supersampled) top screen with nearest texels: a quarter of the fill
	C3D_Tex rt_tex;
	C3D_RenderTarget *rt;
	// glows and steam, culled on the CPU once they have faded out (they are big)
	struct { float x, y, z, size; u16 idx[6]; } quad[64];
	int nquad;
	u16 *ib_sprites;
	u32 sprites_drawn;
	unsigned frame;
	u64 ticks, anim_ticks;
	float gpu_ms;
	unsigned eyes, frames;
	room_pose drawn;                    // the last frame's camera (room_scene_anchor_screen)
	bool drawn_ok;
} S;

// the boxes' mesh generation: new with every load and room style (outlives S, which a game clears)

// the room style asked for (room_scene_style); empty: the file's first
static char want_id[9];

// The last bookcase, kept across games (the room is released before one): the room opened
// after a game has its cubbies at once, while the library is still being read in again.
static room_section last_sec[ROOM_CUBBIES];
static int last_n, last_total;

static void library_apply(const room_section *sec, int n)
{
	if (sec != last_sec) memcpy(last_sec, sec, n * sizeof(*sec));
	last_n = n;
	// the library's slot (2) carries its focus glow; the cubbies take the room style's light
	room_case_plan_make(&S.f, last_sec, n, &S.plan);
	S.case_n = room_case_mesh(&S.f, last_sec, &S.plan, 2, S.case_vb, CASE_MAX, S.case_first, &S.case_backs);
	GSPGPU_FlushDataCache(S.case_vb, S.case_n * sizeof(room_vertex));
}

static bool tv_maps(void);
static unsigned tv_serial;
static void tv_discard(void);
static u16 *tv_map;   // the TV's glass (room_scene_tv)
static u8 *tv_k;
const room_file *room_scene_file(void) { return S.ok ? &S.f : NULL; }
const room_case_plan *room_scene_case_plan(void) { return S.ok && S.plan.cubbies ? &S.plan : NULL; }

// RGBA5551/RGB565, rows top first -> PICA 8x8 tiles in Morton order (first row
// in memory = the top, t = 1, as savestates.c uploads its thumbnails)
static void tile16(u16 *dst, const u16 *src, int w, int h)
{
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			u32 i = (((y >> 3) * (w >> 3) + (x >> 3)) << 6) |
				(x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
			dst[i] = src[y * w + x];
		}
}

static bool texture(C3D_Tex *t, const u16 *px, int w, int h, GPU_TEXCOLOR fmt)
{
	if (!C3D_TexInit(t, w, h, fmt)) return false;
	tile16(t->data, px, w, h);
	C3D_TexFlush(t);
	C3D_TexSetFilter(t, GPU_NEAREST, GPU_NEAREST);
	C3D_TexSetWrap(t, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	return true;
}

static void *read_file(const char *path, size_t *size)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) return NULL;
	fseek(fp, 0, SEEK_END);
	long n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	void *p = n > 0 ? memalign(16, (size_t)n) : NULL;
	if (p && fread(p, 1, (size_t)n, fp) != (size_t)n) { free(p); p = NULL; }
	fclose(fp);
	*size = (size_t)n;
	return p;
}

static void part_range(const char *name, u32 *first, u32 *count)
{
	const room_bbr_part *p = room_part(&S.f, name);
	*first = p ? p->first : 0;
	*count = p ? p->count : 0;
}

// The room's vertices once (positions and uvs) and every room style's colours as a second vertex
// buffer, and every style's atlas (tiled once; styles with the same atlas share it): a style change
// only picks other ones
static bool styles_make(void)
{
	const room_bbr_header *h = S.f.head;
	S.nstyles = S.f.nstyle < ROOM_STYLES ? (int)S.f.nstyle : ROOM_STYLES;
	if (!(S.vb = linearAlloc(h->nvert * sizeof(room_vertex)))) return false;
	memcpy(S.vb, S.f.vdata, h->nvert * sizeof(room_vertex));
	GSPGPU_FlushDataCache(S.vb, h->nvert * sizeof(room_vertex));
	for (int i = 0; i < S.nstyles; i++) {
		if (!(S.cols[i] = linearAlloc(h->nvert * 4))) return false;
		memcpy(S.cols[i], room_style_colours(&S.f, i), h->nvert * 4);
		GSPGPU_FlushDataCache(S.cols[i], h->nvert * 4);
		// position and uv from the shared buffer (+ 4 bytes of padding: 0xC), the colour from the style's
		BufInfo_Init(&S.buf_style[i]);
		BufInfo_Add(&S.buf_style[i], S.vb, sizeof(room_vertex), 3, 0xC10);
		BufInfo_Add(&S.buf_style[i], S.cols[i], 4, 1, 0x2);
		for (int j = 0; j < i && !S.atlas[i]; j++)
			if (S.f.style[j].atlas == S.f.style[i].atlas) S.atlas[i] = S.atlas[j];
		const room_bbr_tex *at = &S.f.tex[S.f.style[i].atlas];
		if (!S.atlas[i]) {
			if (!texture(&S.atlas_tex[i], (const u16 *)((const u8 *)S.file + at->offset), at->w, at->h, GPU_RGBA5551)) return false;
			S.atlas[i] = &S.atlas_tex[i];
		}
	}
	return S.nstyles > 0;
}

// The glows (one range after the file's indices, culled per frame once faded out) and the
// steam's puffs (moved on the CPU)
static bool sprites_make(u32 gf, u32 gc)
{
	const room_bbr_header *h = S.f.head;
	S.sprite_first = h->nidx;
	memcpy(S.ib + S.sprite_first, S.f.idx + gf, gc * 2);
	S.sprite_count = gc;
	for (u32 i = 0; i + 6 <= S.sprite_count && S.nquad < 64; i += 6) {   // two triangles per sprite
		const room_vertex *c = &S.vb[S.ib[S.sprite_first + i]];
		S.quad[S.nquad].x = c->x; S.quad[S.nquad].y = c->y; S.quad[S.nquad].z = c->z; S.quad[S.nquad].size = c->w;
		memcpy(S.quad[S.nquad++].idx, S.ib + S.sprite_first + i, 12);
	}
	S.ib_sprites = linearAlloc(64 * 6 * 2);
	if (!S.ib_sprites) return false;
	{
		u32 first, count;
		part_range("steam", &first, &count);
		for (u32 i = 0; i + 6 <= count && S.nanim < ANIM_MAX; i += 6) {   // its four corners, in corner order
			const u16 *q = S.f.idx + first + i;
			int got = 0;
			for (int k = 0; k < 6; k++) {
				room_vertex v = S.vb[q[k]];
				int corner = v.a & 3;
				if (got & (1 << corner)) continue;
				got |= 1 << corner;
				S.anim[S.nanim].v[corner] = v;
				S.anim[S.nanim].idx[corner] = q[k];
			}
			if (got != 15) continue;
			const room_vertex *c = &S.anim[S.nanim].v[0];
			float hh = sinf(floorf(c->x) * 12.9898f + floorf(c->z) * 78.233f + i) * 43758.5453f;
			S.anim[S.nanim++].ph = hh - floorf(hh);   // the mock's h1(floor(p.xz)), per puff
		}
	}
	S.anim_ib = S.nanim ? linearAlloc(S.nanim * 6 * 2) : NULL;
	for (int i = 0; i < 2 && S.anim_ib; i++) {
		S.anim_vb[i] = linearAlloc(S.nanim * 4 * sizeof(room_vertex));
		if (!S.anim_vb[i]) return false;
		BufInfo_Init(&S.buf_anim[i]);
		BufInfo_Add(&S.buf_anim[i], S.anim_vb[i], sizeof(room_vertex), 3, 0x210);
	}
	for (int i = 0; i < S.nanim && S.anim_ib; i++) {
		static const u16 tri[6] = { 0, 1, 2, 0, 2, 3 };
		for (int k = 0; k < 6; k++) S.anim_ib[i * 6 + k] = (u16)(i * 4 + tri[k]);
	}
	if (S.anim_ib) GSPGPU_FlushDataCache(S.anim_ib, S.nanim * 6 * 2);
	return true;
}

// The final pass's dither pattern (8x8 RGBA8, repeated: one texel a pixel) and screen quad;
// without memory for them the room is drawn without the pass
static void final_pass_make(void)
{
	S.post_vb = C3D_TexInit(&S.dither, 8, 8, GPU_RGBA8) ? linearAlloc(4 * sizeof(room_vertex)) : NULL;
	S.post_ib = S.post_vb ? linearAlloc(6 * 2) : NULL;
	if (!S.post_ib) {   // no memory: the room is drawn without the pass
		if (S.post_vb) linearFree(S.post_vb);
		if (S.dither.data) C3D_TexDelete(&S.dither);
		S.post_vb = NULL;
		memset(&S.dither, 0, sizeof(S.dither));
	} else {
		u32 pat[64];
		room_dither_texture(pat);
		for (int y = 0; y < 8; y++)
			for (int x = 0; x < 8; x++)
				((u32 *)S.dither.data)[(x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)] = pat[y * 8 + x];
		C3D_TexFlush(&S.dither);
		C3D_TexSetFilter(&S.dither, GPU_NEAREST, GPU_NEAREST);
		C3D_TexSetWrap(&S.dither, GPU_REPEAT, GPU_REPEAT);
		// x, y in -100..100 over the screen (the projection below), uv: 400 x 240 texels (x 16)
		static const int8_t corner[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
		for (int i = 0; i < 4; i++)
			S.post_vb[i] = (room_vertex){ (int16_t)(corner[i][0] * 100), (int16_t)(corner[i][1] * 100), 0, 0,
				(int16_t)((corner[i][0] + 1) * 200 * 16), (int16_t)((1 - corner[i][1]) * 120 * 16), 128, 128, 128, 255 };
		static const u16 tri[6] = { 0, 1, 2, 0, 2, 3 };
		memcpy(S.post_ib, tri, sizeof(tri));
		GSPGPU_FlushDataCache(S.post_vb, 4 * sizeof(room_vertex));
		GSPGPU_FlushDataCache(S.post_ib, sizeof(tri));
	}
}

// The vertices, indices, colours and atlases are the GPU's copies now: keep only the file's tables
// (header, textures, parts, anchors, styles) and every style's probes, about 5 KB of 400
static bool keep_tables(void)
{
	size_t tables = S.f.vdata - (const u8 *)S.file, probes = 0;
	for (unsigned i = 0; i < S.f.nstyle; i++) probes += S.f.style[i].nprobe * sizeof(room_probe);
	u8 *keep = memalign(16, tables + probes);
	if (!keep) return false;
	memcpy(keep, S.file, tables);
	ptrdiff_t moved = keep - (u8 *)S.file;
	room_bbr_style *st = (room_bbr_style *)((u8 *)S.f.style + moved);
	size_t at = tables;
	for (unsigned i = 0; i < S.f.nstyle; i++) {
		size_t n = st[i].nprobe * sizeof(room_probe);
		memcpy(keep + at, (const u8 *)S.file + st[i].probes, n);
		st[i].probes = (u32)at;
		st[i].colours = 0;   // in linear memory now (S.cols); room_style_colours is for loading only
		at += n;
	}
	S.f.head = (const void *)((const u8 *)S.f.head + moved);
	S.f.tex = (const void *)((const u8 *)S.f.tex + moved);
	S.f.part = (const void *)((const u8 *)S.f.part + moved);
	S.f.anchor = (const void *)((const u8 *)S.f.anchor + moved);
	S.f.style = st;
	S.f.vdata = NULL;
	S.f.idx = NULL;
	S.f.size = tables + probes;
	free(S.file);
	S.file = keep;
	room_style_use(&S.f, S.style);   // its probes in the kept copy
	return true;
}

static bool load(void)
{
	u64 t0 = svcGetSystemTick();
	size_t size;
	char err[64];
	S.file = read_file(ROOM_PATH, &size);
	if (!S.file || !room_parse(&S.f, S.file, size, err, sizeof(err))) {
		log_printf("room: %s: %s\n", ROOM_PATH, S.file ? err : "missing");
		return false;
	}
	const room_bbr_header *h = S.f.head;
	if (h->version != 3 || !room_part(&S.f, "static") || !room_part(&S.f, "a_card") || !room_anchor(&S.f, "pose.home")) {
		log_printf("room: not a room file (v%u, no room parts)\n", (unsigned)h->version);
		return false;
	}
	// the room range runs from `static` to the end of `a_card` (file order = draw order)
	const room_bbr_part *first = room_part(&S.f, "static"), *last = room_part(&S.f, "a_card");
	S.room_first = first->first;
	S.room_count = last->first + last->count - first->first;
	part_range("tv_screen", &S.tv_first, &S.tv_count);
	// the glows as one range after the file's indices (culled per frame once faded out)
	u32 gf, gc;
	part_range("glow", &gf, &gc);
	if (!styles_make()) return false;
	S.ib = linearAlloc((h->nidx + gc) * 2);
	S.case_vb = linearAlloc(CASE_MAX * sizeof(room_vertex));
	for (int i = 0; i < 2; i++) S.box_vb[i] = linearAlloc(BOX_VERTS * sizeof(room_vertex));
	if (!S.box_vb[0] || !S.box_vb[1]) return false;
	for (int i = 0; i < 2; i++) if (!(S.lamp_vb[i] = linearAlloc(LAMP_VERTS * sizeof(room_vertex)))) return false;
	if (!S.ib || !S.case_vb) return false;
	memcpy(S.ib, S.f.idx, h->nidx * 2);
	if (!sprites_make(gf, gc)) return false;
	GSPGPU_FlushDataCache(S.ib, (h->nidx + gc) * 2);
	int st = room_style_find(&S.f, want_id);
	S.style = st >= 0 && st < S.nstyles ? st : 0;
	room_style_use(&S.f, S.style);
	u16 *px = malloc(ROOM_SHELF_W * ROOM_SHELF_H * 2);
	if (!px) return false;
	room_shelf_texture(px);
	bool shelf = texture(&S.shelf, px, ROOM_SHELF_W, ROOM_SHELF_H, GPU_RGBA5551);
	// a switched-off TV: one dark texel colour (8x8, the smallest texture)
	for (int i = 0; i < 64; i++) px[i] = (3 << 11) | (5 << 5) | 5;
	bool blank = texture(&S.blank, px, 8, 8, GPU_RGB565);
	free(px);
	if (!shelf || !blank) return false;
	// The extras below are optional: without memory for one the room is drawn without it.
	{ const float *post = room_anchor(&S.f, "post"); room_dither_set(post ? post[0] : 1); }
	final_pass_make();
	// New 3DS only: the room menu draws the Old's top screen at 1x straight away (and the
	// shelf wants the VRAM); other screens get the room supersampled there
	if (g_new3ds && C3D_TexInitVRAM(&S.rt_tex, 256, 512, GPU_RGB565)) {
		S.rt = C3D_RenderTargetCreateFromTex(&S.rt_tex, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH16);
		if (S.rt) C3D_TexSetFilter(&S.rt_tex, GPU_NEAREST, GPU_NEAREST);
		else C3D_TexDelete(&S.rt_tex);
	}
	if (!S.rt && g_new3ds) log_printf("room: no VRAM for the 400x240 target (%uK free): drawn supersampled\n", (unsigned)(vramSpaceFree() / 1024));

	S.dvlb = DVLB_ParseFile((u32 *)room_shbin, room_shbin_size);
	if (!S.dvlb) return false;
	shaderProgramInit(&S.prog);
	shaderProgramSetVsh(&S.prog, &S.dvlb->DVLE[0]);
	shaderInstance_s *vs = S.prog.vertexShader;
	S.u_proj = shaderInstanceGetUniformLocation(vs, "proj");
	S.u_texscale = shaderInstanceGetUniformLocation(vs, "texscale");
	S.u_right = shaderInstanceGetUniformLocation(vs, "right");
	S.u_up = shaderInstanceGetUniformLocation(vs, "up");
	S.u_eye = shaderInstanceGetUniformLocation(vs, "eye");
	S.u_corner = shaderInstanceGetUniformLocation(vs, "corner");
	S.u_pal = shaderInstanceGetUniformLocation(vs, "pal");
	S.u_tint = shaderInstanceGetUniformLocation(vs, "tint");
	S.u_sprite = shaderInstanceGetUniformLocation(vs, "sprite");
	S.u_fadek = shaderInstanceGetUniformLocation(vs, "fadek");
	S.u_lscale = shaderInstanceGetUniformLocation(vs, "lscale");
	AttrInfo_Init(&S.attr);
	AttrInfo_AddLoader(&S.attr, 0, GPU_SHORT, 4);
	AttrInfo_AddLoader(&S.attr, 1, GPU_SHORT, 2);
	AttrInfo_AddLoader(&S.attr, 2, GPU_UNSIGNED_BYTE, 4);
	BufInfo_Init(&S.buf_case);
	BufInfo_Add(&S.buf_case, S.case_vb, sizeof(room_vertex), 3, 0x210);
	for (int i = 0; i < 2; i++) {
		BufInfo_Init(&S.buf_box[i]);
		BufInfo_Add(&S.buf_box[i], S.box_vb[i], sizeof(room_vertex), 3, 0x210);
		BufInfo_Init(&S.buf_lamp[i]);
		BufInfo_Add(&S.buf_lamp[i], S.lamp_vb[i], sizeof(room_vertex), 3, 0x210);
	}
	BufInfo_Init(&S.buf_post);
	BufInfo_Add(&S.buf_post, S.post_vb, sizeof(room_vertex), 3, 0x210);
	if (!keep_tables()) return false;
	if (last_n) library_apply(last_sec, last_n);   // the bookcase it had before the game
	log_printf("room: %s %.8s v%u loaded in %lu ms: %u vertices, %u triangles, %u styles (%.8s), %u probes, linear %uK free, heap largest %uK, VRAM %uK free\n",
		ROOM_PATH, h->style, (unsigned)h->version, (unsigned long)((svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000)), (unsigned)h->nvert,
		(unsigned)h->nidx / 3, S.f.nstyle, S.f.style[S.style].id, S.f.nprobe,
		(unsigned)(linearSpaceFree() / 1024), (unsigned)(bb_mem_heap_largest() / 1024), (unsigned)(vramSpaceFree() / 1024));
	return true;
}

bool room_scene_open(void)
{
	if (S.tried) return S.ok;
	S.tried = true;
	S.ok = load();
	if (!S.ok) room_scene_release();
	else room_walls_open(&S.f);   // the poster and the corkboard (room_walls.c)
	return S.ok;
}

void room_scene_style(const char *id)
{
	if (id) snprintf(want_id, sizeof(want_id), "%.8s", id);
	if (!S.ok) return;
	int i = room_style_find(&S.f, id);
	if (i < 0 || i >= S.nstyles) i = 0;
	if (i == S.style) return;
	S.style = i;
	room_style_use(&S.f, i);                       // its probes and a new light generation: the real boxes are made again (draw_boxes)
	if (last_n) library_apply(last_sec, last_n);   // the cubbies take the new light
	room_walls_restyle(&S.f);                      // the walls too
	log_printf("room: style %.8s\n", S.f.style[i].id);
}

void room_scene_release(void)
{
	tv_serial++;
 tv_discard();
	room_walls_release();
	if (S.dvlb) { shaderProgramFree(&S.prog); DVLB_Free(S.dvlb); }
	if (S.rt) { C3D_RenderTargetDelete(S.rt); C3D_TexDelete(&S.rt_tex); }
	if (S.ib_sprites) linearFree(S.ib_sprites);
	for (int i = 0; i < ROOM_STYLES; i++) {
		if (S.atlas_tex[i].data) C3D_TexDelete(&S.atlas_tex[i]);
		if (S.cols[i]) linearFree(S.cols[i]);
	}
	if (S.tv.data) C3D_TexDelete(&S.tv);
	// Immutable maps live for the process: deferred TV jobs may still read them.
	if (S.shelf.data) C3D_TexDelete(&S.shelf);
	if (S.blank.data) C3D_TexDelete(&S.blank);
	if (S.dither.data) C3D_TexDelete(&S.dither);
	if (S.post_vb) linearFree(S.post_vb);
	if (S.post_ib) linearFree(S.post_ib);
	if (S.ib) linearFree(S.ib);
	if (S.case_vb) linearFree(S.case_vb);
	for (int i = 0; i < 2; i++) if (S.anim_vb[i]) linearFree(S.anim_vb[i]);
	if (S.anim_ib) linearFree(S.anim_ib);
	if (S.vb) linearFree(S.vb);
	for (int i = 0; i < 2; i++) if (S.box_vb[i]) linearFree(S.box_vb[i]);
	for (int i = 0; i < 2; i++) if (S.lamp_vb[i]) linearFree(S.lamp_vb[i]);
	free(S.file);
	memset(&S, 0, sizeof(S));   // the next room_scene_open loads again
}

// Favorites first, then every system with games, newest release first (the
// bookcase's fixed order; systems without games get no section).
void room_scene_library(void)
{
	if (!S.ok) return;
	room_section sec[ROOM_CUBBIES];
	int n = 0;
	sec[n++] = (room_section){ ROOM_SPINE_MIXED, playlog_favourite_count(), 20 };   // Favorites first
	const bb_system_t *order[BB_MAX_SYSTEMS];
	int ns = (int)bb_system_display_order(order, BB_MAX_SYSTEMS, true);   // newest release first
	for (int i = 0; i < ns && n < ROOM_CUBBIES; i++) {
		const bb_package *p = order[i]->package;
		sec[n++] = (room_section){ p && p->style == BB_PACKAGE_CASE ? ROOM_SPINE_JEWEL : ROOM_SPINE_CARTON,
			bb_system_library_count(order[i]), p ? p->depth : 16 };
	}
	last_total = bb_system_total();
	library_apply(sec, n);
	log_printf("room: bookcase %d sections in %d of %d cubbies (%d games), %d vertices\n", n, S.plan.used, S.plan.cubbies,
		last_total, S.case_n);
}

// The bookcase follows the library: made again once its count has been still for half a
// second (systems publish game by game), never from a library still filling up again after
// a game (fewer games than the bookcase has, before the index is complete, keep it).
void room_scene_library_follow(bool now)
{
	static unsigned seen, still, made = ~0u;
	unsigned key = bb_index_generation() + (unsigned)bb_system_total() * 7919u;
	still = key == seen ? still + 1 : 0;
	seen = key;
	if (!S.ok || (key == made && S.plan.cubbies)) return;
	bool filling = last_n && bb_system_total() < last_total && !bb_index_complete();
	if ((now || still >= 30) && (bb_index_complete() || bb_system_total()>0) && !filling) {
		made = key;
		room_scene_library();
	}
}

// ---------------------------------------------------------------- the TV

// The CRT look, baked once: the picture bulges on the curved glass, every other
// line darker, the corners fall off, a little glow lifts the lights (UI mock R9).
// The picture fills 256x192 of the 256x256 texture (the TV's uvs: 4:3 at the top).
// The glass's shape is worked out once (tv_map: where each texel looks in the picture,
// tv_k: its falloff and scanline), so a new picture is a lookup per texel: a few ms on
// the Old 3DS instead of a hitch. Initialization runs on the index worker,
// including after games; the menu only queues owned pictures.
static bool tv_maps(void)
{
	if (tv_map) return true;
	u64 start=svcGetSystemTick();
	tv_map = malloc(256 * 192 * 2);
	tv_k = malloc(256 * 192);
	if (!tv_map || !tv_k) { free(tv_map); free(tv_k); tv_map = NULL; tv_k = NULL; return false; }
	for (int y = 0; y < 192; y++)
		for (int x = 0; x < 256; x++) {
			float u = (x + .5f) / 128 - 1, v = (y + .5f) / 96 - 1;
			float cu = u * (1 + .07f * v * v), cv = v * (1 + .09f * u * u);   // barrel: the curved glass
			int i = y * 256 + x;
			if (fabsf(cu) > 1 || fabsf(cv) > 1) tv_map[i] = 0xffff;
			else {
				int sx = (int)((cu + 1) * 128), sy = (int)((cv + 1) * 96);
				tv_map[i] = (u16)((sy > 191 ? 191 : sy) * 256 + (sx > 255 ? 255 : sx));
			}
			float k = (y & 1 ? .8f : 1) * (1 - .32f * (u * u + v * v) * .5f);   // scanlines, falloff
			tv_k[i] = (u8)(k * 255);
		}
	log_printf("room: tv_maps worker %lu us\n",(unsigned long)((svcGetSystemTick()-start)/(SYSCLOCK_ARM11/1000000)));
	return true;
}

static u16 rgb565i(int r, int g, int b)   // 0..255 each
{
	r = r > 255 ? 255 : r; g = g > 255 ? 255 : g; b = b > 255 ? 255 : b;
	return (u16)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
}

// Worker-only CPU bake; the single index worker initializes immutable maps.
// Both the Continue probe and cover/snow jobs use the identical pixel operations.
bool room_scene_tv_bake(const u16 *px, u16 *tiled, u16 *card)
{
 if (!tv_maps()) return false;
 u16 *out = calloc(256 * 256, 2);
 if (!out) return false;
	uint32_t seed = 0x2545f491u;
	for (int i = 0; i < 256 * 192; i++) {
		int k = tv_k[i], r, g, b;
		if (px) {
			u16 m = tv_map[i];
			if (m == 0xffff) continue;
			u16 c = px[m];
			r = (c >> 11) * 527 >> 6; g = ((c >> 5) & 63) * 259 >> 6; b = (c & 31) * 527 >> 6;   // to 0..255
			int avg = (r + g + b) / 3, glow = avg > 140 ? (avg - 140) * 57 >> 8 : 0;          // the bright parts bloom a little
			r = (r * 271 >> 8) + glow; g = (g * 271 >> 8) + glow; b = (b * 271 >> 8) + glow;
		} else {   // snow: a wrapping noise tile, moved per frame by the uv offset
			seed = seed * 1664525u + 1013904223u;
			r = g = b = 31 + (int)(140 * (seed >> 24) >> 8);
		}
		out[i] = rgb565i(r * k >> 8, g * k >> 8, (b * k >> 8) * 264 >> 8);
	}
	for (int i = 0; i < 64 * 256 && !px; i++) out[192 * 256 + i] = out[i];   // the snow wraps vertically too
	tile16(tiled, out, 256, 256);

 if (card && px) {
  memset(out, 0, 256 * 256 * 2);
  memcpy(out, px, 256 * 192 * 2);
  tile16(card, out, 256, 256);
 }
 free(out);
 return true;
}
void room_scene_tv_upload(const u16 *tiled, bool snow)
{
 if (!S.ok || !tiled) return;
 tv_serial++;
 if (!S.tv.data && !C3D_TexInit(&S.tv, 256, 256, GPU_RGB565)) return;
 memcpy(S.tv.data, tiled, 256 * 256 * 2);
 C3D_TexFlush(&S.tv);
 C3D_TexSetFilter(&S.tv, GPU_LINEAR, GPU_LINEAR);
 C3D_TexSetWrap(&S.tv, GPU_REPEAT, GPU_REPEAT);
 S.tv_static = snow; S.tv_ok = true;
}
bool room_scene_picture_upload(C3D_Tex *t, const u16 *tiled)
{
 if (!tiled || !C3D_TexInit(t, 256, 256, GPU_RGB565)) return false;
 memcpy(t->data, tiled, 256 * 256 * 2);
 C3D_TexFlush(t); C3D_TexSetFilter(t, GPU_LINEAR, GPU_LINEAR);
 return true;
}
typedef struct tv_job {
 unsigned serial; int done; bool ok, snow, source;
 u16 *pixels, *tiled, *texture;
 C3D_Tex tex; Tex3DS_SubTexture sub;
} tv_job;
static tv_job *tv_work, *tv_pending;
static void tv_job_free(tv_job *j) { if(j){free(j->pixels);free(j->tiled);free(j->texture);free(j);} }
static void tv_discard(void)
{
 tv_job_free(tv_pending);tv_pending=NULL;
 // Usually called after index shutdown; a failed open may have an active old job.
 if(tv_work && __atomic_load_n(&tv_work->done,__ATOMIC_ACQUIRE)){tv_job_free(tv_work);tv_work=NULL;}
}
void room_scene_tv_maps_release(void)
{
 tv_discard();
 if(tv_work)return; // still owned by a cancelled worker after a timed join
 free(tv_map);free(tv_k);tv_map=NULL;tv_k=NULL;
}
static void tv_resample(const C3D_Tex *t, const Tex3DS_SubTexture *sub, u16 *px);
static void tv_worker(void *arg)
{
 tv_job *j=arg;
 if (!bb_index_cancelled()) {
  if(j->source) tv_resample(&j->tex, &j->sub, j->pixels);
  j->ok=room_scene_tv_bake(j->snow?NULL:j->pixels,j->tiled,NULL);
 }
 __atomic_store_n(&j->done,1,__ATOMIC_RELEASE);
}
void room_scene_tv_poll(void)
{
 if(tv_work && __atomic_load_n(&tv_work->done,__ATOMIC_ACQUIRE)) {
  if(tv_work->ok && tv_work->serial==tv_serial) room_scene_tv_upload(tv_work->tiled,tv_work->snow);
  tv_job_free(tv_work);tv_work=NULL;
 }
 if(!tv_work && tv_pending) {
  if(tv_pending->serial!=tv_serial){tv_job_free(tv_pending);tv_pending=NULL;return;}
  if(bb_index_defer_visible(tv_worker,tv_pending)){tv_work=tv_pending;tv_pending=NULL;}
 }
}
static tv_job *tv_request(bool snow)
{
 tv_job *j=calloc(1,sizeof(*j));if(!j)return NULL;
 j->tiled=malloc(256*256*2);
 if(!snow)j->pixels=malloc(256*192*2);
 if(!j->tiled || (!snow && !j->pixels)){tv_job_free(j);return NULL;}
 j->snow=snow;return j;
}
static void tv_submit(tv_job *j)
{
 if(!j)return;
 j->serial=++tv_serial;
 tv_job_free(tv_pending);tv_pending=j;
 room_scene_tv_poll();
}
void room_scene_tv(const u16 *px)
{
 if(!S.ok)return;
 tv_job *j=tv_request(!px);if(!j)return;
 if(px)memcpy(j->pixels,px,256*192*2);
 tv_submit(j);
}

// The TV's picture (256 x 192, as the library keeps it) as a card's texture (256 x 256, the picture
// in its top rows): the home card shows what the TV shows. false: no memory, t unused.
bool room_scene_picture_tex(C3D_Tex *t, const u16 *px)
{
	u16 *full = calloc(256 * 256, 2);
	if (!full) return false;
	memcpy(full, px, 256 * 192 * 2);
	bool ok = texture(t, full, 256, 256, GPU_RGB565);
	free(full);
	if (ok) C3D_TexSetFilter(t, GPU_LINEAR, GPU_LINEAR);
	return ok;
}

// a tiled RGB565 texture's region (a thumbnail, a cover) into the TV, 4:3 fitted
static void tv_resample(const C3D_Tex *t, const Tex3DS_SubTexture *sub, u16 *px)
{
	const u16 *d = t->data;
	// aspect kept: a portrait cover gets black sides, a 4:3 picture fills the screen
	float aspect = sub->height ? (float)sub->width / sub->height : 4 / 3.0f, w = fminf(256, 192 * aspect), x0 = (256 - w) / 2;
	for (int y = 0; y < 192; y++)
		for (int x = 0; x < 256; x++) {
			if (x < x0 || x >= x0 + w) { px[y * 256 + x] = 0; continue; }
			float tu = sub->left + (sub->right - sub->left) * (x - x0 + .5f) / w, tv = sub->top + (sub->bottom - sub->top) * (y + .5f) / 192;
			int sx = (int)(tu * t->width), sy = (int)((1 - tv) * t->height);
			sx = sx < 0 ? 0 : sx >= t->width ? t->width - 1 : sx;
			sy = sy < 0 ? 0 : sy >= t->height ? t->height - 1 : sy;
			px[y * 256 + x] = d[(((sy >> 3) * (t->width >> 3) + (sx >> 3)) << 6) | (sx & 1) | ((sy & 1) << 1) | ((sx & 2) << 1) | ((sy & 2) << 2) | ((sx & 4) << 2) | ((sy & 4) << 3)];
		}
}
void room_scene_tv_tex(C3D_Tex *t, const Tex3DS_SubTexture *sub)
{
 if(!t || !sub || !t->data || t->fmt!=GPU_RGB565){room_scene_tv(NULL);return;}
 if(!S.ok)return;
 tv_job *j=tv_request(false);if(!j)return;
 // Cover textures belong to covers: capture owned bytes before returning to it.
 size_t bytes=(size_t)t->width*t->height*2;
 j->texture=malloc(bytes);if(!j->texture){tv_job_free(j);return;}
 memcpy(j->texture,t->data,bytes);j->tex=*t;j->tex.data=j->texture;
 j->sub=*sub;j->source=true;tv_submit(j);
}

// ---------------------------------------------------------------- drawing

static void set_vec(int loc, float x, float y, float z, float w) { C3D_FVUnifSet(GPU_VERTEX_SHADER, loc, x, y, z, w); }

static void slot_identity(int slot)
{
	set_vec(S.u_pal + slot * 3, 1, 0, 0, 0);
	set_vec(S.u_pal + slot * 3 + 1, 0, 1, 0, 0);
	set_vec(S.u_pal + slot * 3 + 2, 0, 0, 1, 0);
}

static void slot_tint(int slot, float k) { set_vec(S.u_tint + slot, k * 1.02f, k, k * .94f, 1); }

static void env_mesh(void)
{
	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
	C3D_TexEnvScale(env, C3D_RGB, GPU_TEVSCALE_4);   // colour x lscale.x x 4: 1.0 = room_light_unit, up to the range
	C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, 0, 0);
	C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
}

// the light on your hands from the room's probes: half their colour, a little warm fill,
// never darker than 0.62 (the room's cool corners must not turn them grey)
static void hands_light(const float pos[3], const float to_cam[3])
{
	float pr[3];
	if (!room_probe_light(&S.f, pos, to_cam, pr)) { room_hands_light(NULL); return; }
	for (int i = 0; i < 3; i++) pr[i] += S.lamp_now[i];   // the bookcase lamp, when one is on
	float lum = (pr[0] + pr[1] + pr[2]) / 3, c[3];
	static const float fill[3] = { .05f, .035f, .015f };
	for (int i = 0; i < 3; i++) c[i] = lum + .5f * (pr[i] - lum) + fill[i];
	float l = (c[0] + c[1] + c[2]) / 3, k = l < .62f ? .62f / fmaxf(l, .01f) : 1;
	for (int i = 0; i < 3; i++) c[i] *= k;
	room_hands_light(c);
}

// The real boxes of one section: per box its spine (textured or plain), top and sides,
// lit by the light in front of the cubby. Spines with a texture first, grouped by it, then the rest.
static int box_mesh(const room_library *lib, room_vertex *o, C3D_Tex *tex[3], int count[3])
{
	room_case_geo g;
	if (!room_case_section(&S.f, lib->cur, &g)) return 0;
	int n = 0;
	float light[3];
	room_cubby_light_f(&S.f, &g, light);   // 1.0 = neutral; room_lit_rgb puts it in the style's unit
	tex[0] = tex[1] = tex[2] = NULL;
	count[0] = count[1] = count[2] = 0;
	for (int pass = 0; pass < 3; pass++) {
		for (int i = 0; i < lib->nbox && i < BOX_MAX; i++) {
			const room_box *b = &lib->box[i];
			int k = !b->spine ? 2 : b->spine == tex[0] || !tex[0] ? 0 : b->spine == tex[1] || !tex[1] ? 1 : -1;
			if (pass < 2 && k == pass && !tex[k]) tex[k] = b->spine;
			float x0 = b->x - b->t / 2, x1 = b->x + b->t / 2, y0 = b->base, y1 = b->base + b->h, z0 = b->z, z1 = b->z + b->w;
			// one face: corners (x, y, z) a b c d, the texture's corners, a colour
			#define FACE(ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy, dz, tu0, tv0, tu1, tv1, col, k_) do { \
				const float P[4][3] = { { ax, ay, az }, { bx, by, bz }, { cx, cy, cz }, { dx, dy, dz } }; \
				const float U[4][2] = { { tu0, tv0 }, { tu1, tv0 }, { tu1, tv1 }, { tu0, tv1 } }; \
				static const int T[6] = { 0, 1, 2, 0, 2, 3 }; \
				u8 rgb[4][3]; \
				for (int q = 0; q < 4; q++) { \
					float add[3], l4[3]; \
					room_lamp_at(&g, lib->lamp, P[q][0], P[q][1], P[q][2], add); \
					for (int a = 0; a < 3; a++) add[a] += light[a]; \
					room_box_light(add, l4); \
					room_lit_rgb(&S.f, col, (k_) * b->bright, l4, rgb[q]); \
				} \
				for (int q = 0; q < 6; q++) \
					o[n++] = (room_vertex){ (int16_t)lroundf(P[T[q]][0]), (int16_t)lroundf(P[T[q]][1]), (int16_t)lroundf(P[T[q]][2]), 0, \
						(int16_t)lroundf(U[T[q]][0] * 32767), (int16_t)lroundf(U[T[q]][1] * 32767), rgb[T[q]][0], rgb[T[q]][1], rgb[T[q]][2], 255 }; \
				} while (0)
			if (pass < 2 && b->spine && k == pass) {   // its baked spine, upright: top of the box at v0
				FACE(x0, y1, z0, x1, y1, z0, x1, y0, z0, x0, y0, z0, b->u0, b->v0, b->u1, b->v1, 0xffffffff, 1.0f);
				count[pass] += 6;
			} else if (pass == 2) {
				int before = n;
				if (!b->spine || k < 0)
					FACE(x0, y1, z0, x1, y1, z0, x1, y0, z0, x0, y0, z0, 0, 0, 0, 0, b->color, 1.0f);
				FACE(x0, y1, z1, x1, y1, z1, x1, y1, z0, x0, y1, z0, 0, 0, 0, 0, b->side, .82f);   // top
				FACE(x0, y1, z1, x0, y1, z0, x0, y0, z0, x0, y0, z1, 0, 0, 0, 0, b->side, .62f);   // left
				FACE(x1, y1, z0, x1, y1, z1, x1, y0, z1, x1, y0, z0, 0, 0, 0, 0, b->side, .7f);    // right
				count[2] += n - before;
			}
			#undef FACE
		}
	}
	return n;
}

// the steam curls up from the mug (UI mock R10); once per frame into this frame's buffer
// a sine by table (the Old 3DS's sinf is slow): x in radians
#define SIN_N 1024
static float sin_tab[SIN_N];
static inline float fsin(float x)
{
	if (!sin_tab[SIN_N / 4]) for (int i = 0; i < SIN_N; i++) sin_tab[i] = sinf(i * 2 * (float)M_PI / SIN_N);
	return sin_tab[(int)(x * (SIN_N / (2 * (float)M_PI)) + SIN_N * 64) & (SIN_N - 1)];
}
static inline float fcos(float x) { return fsin(x + (float)M_PI / 2); }
static inline float wrap(float x, float m) { return x - m * floorf(x / m); }   // 0..m
static inline int16_t i16(float x) { return (int16_t)floorf(x + .5f); }

static int anim_build(void)
{
	if (!S.anim_ib) return -1;
	float t = (float)fmod((double)svcGetSystemTick() / SYSCLOCK_ARM11, 3600.0);   // one clock for the room and the Library
	int k = ++S.anim_frame & 1;
	room_vertex *o = S.anim_vb[k];
	for (int i = 0; i < S.nanim; i++) {
		const room_vertex *c = S.anim[i].v;
		float ph = S.anim[i].ph, x = c->x, y = c->y, size = c->w;
		float u = t * .32f + ph + y / 150;   // a puff rises 170 mm and fades, then starts again
		u -= floorf(u);
		y = y - wrap(y - 470, 50) + u * 170;
		x += fsin(u * 5 + ph * 6) * 14;
		float col = fsin(u * (float)M_PI);
		size *= .6f + u;
		col = col < 0 ? 0 : col;
		for (int q = 0; q < 4; q++) {
			room_vertex v = c[q];
			memcpy(&v.r, S.cols[S.style] + S.anim[i].idx[q] * 4, 3);   // the room style's colour
			v.x = i16(x); v.y = i16(y); v.w = i16(size);
			v.r = (u8)(v.r * col); v.g = (u8)(v.g * col); v.b = (u8)(v.b * col);
			o[i * 4 + q] = v;
		}
	}
	GSPGPU_FlushDataCache(o, S.nanim * 4 * sizeof(room_vertex));
	return k;
}

// the lamp's state as a key: its level in 1/32 steps (a change makes the lit meshes again)
static int lamp_key(float lamp)
{
	int l = (int)(lamp * 32 + .5f);
	return l <= 0 ? 0 : l;
}

// the open cubby's lit back and the lamp's face: made again when the cubby, the lamp or the light changes
// (the left eye makes it; double buffered: never written under the GPU)
static void lamp_make(const room_library *lib, bool first_eye)
{
	static int cur = -2, key = -1;
	static unsigned gen;
	int k = lamp_key(lib->lamp);
	if (cur == lib->cur && key == k && gen == S.f.gen && S.lamp_n) return;
	if (!first_eye) return;
	S.lamp_k = (S.lamp_k + 1) & 1;
	S.lamp_n = lib->cur >= 0 ? room_lamp_mesh(&S.f, lib->cur, lib->lamp, 2, S.lamp_vb[S.lamp_k], LAMP_VERTS) : 0;
	GSPGPU_FlushDataCache(S.lamp_vb[S.lamp_k], S.lamp_n * sizeof(room_vertex));
	cur = lib->cur; key = k; gen = S.f.gen;
}

// the far tiles: runs of sections with the same look (normal, highlighted, the one with boxes)
static void draw_case(const room_library *lib)
{
	float w = lib ? lib->wall : 0, lt[3] = { 1, 1, 1 };
	if (lib) room_lamp_case_tint(lib->lamp, lt);   // the lamp lifts the case's wood
	// the cubbies' wood backs: one draw, a little darker on the wall view; the open cubby's
	// back is the lamp's lit grid instead (room_lamp_mesh)
	set_vec(S.u_tint + 2, (1 - .12f * w) * lt[0], (1 - .12f * w) * lt[1], (1 - .1f * w) * lt[2], 1);
	int open = lib && lib->cur >= 0 && S.lamp_n && lib->cur < S.plan.cubbies ? lib->cur : -1;
	if (open < 0) {
		if (S.case_backs) C3D_DrawArrays(GPU_TRIANGLES, 0, S.case_backs);
	} else {
		if (open > 0) C3D_DrawArrays(GPU_TRIANGLES, 0, open * 6);
		if ((open + 1) * 6 < S.case_backs) C3D_DrawArrays(GPU_TRIANGLES, (open + 1) * 6, S.case_backs - (open + 1) * 6);
		set_vec(S.u_tint + 2, 1 - .12f * w, 1 - .12f * w, 1 - .1f * w, 1);
		C3D_SetBufInfo(&S.buf_lamp[S.lamp_k]);
		C3D_DrawArrays(GPU_TRIANGLES, 0, S.lamp_n);
		C3D_SetBufInfo(&S.buf_case);
	}
	set_vec(S.u_tint + 2, lt[0], lt[1], lt[2], 1);
	if (!lib) { C3D_DrawArrays(GPU_TRIANGLES, S.case_backs, S.case_n - S.case_backs); return; }
	// then the cubbies in runs of one look: the highlighted section's brighter, the one with
	// real boxes fading under them, the rest darker
	int n = S.plan.cubbies;
#define KIND(c) ((c) == lib->cur ? 2 : S.plan.owner[c] >= 0 && S.plan.owner[c] == lib->hsel ? 1 : 0)
	for (int i = 0; i < n;) {
		int kind = KIND(i), j = i + 1;
		while (j < n && KIND(j) == kind) j++;
		int first = S.case_first[i], count = S.case_first[j] - first;
		i = j;
		if (count <= 0) continue;
		if (kind == 1) set_vec(S.u_tint + 2, (1 + .38f * w) * lt[0], (1 + .3f * w) * lt[1], (1 + .18f * w) * lt[2], 1);
		else set_vec(S.u_tint + 2, (1 - .25f * w * (kind == 0)) * lt[0], (1 - .25f * w * (kind == 0)) * lt[1], (1 - .22f * w * (kind == 0)) * lt[2], 1);
		if (kind == 2) {   // fading out under its real boxes: alpha (1 - fade), no depth
			if (lib->fade > .99f) continue;
			C3D_TexEnv *env = C3D_GetTexEnv(0);
			C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_CONSTANT, 0);
			C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
			C3D_TexEnvColor(env, (u32)((1 - lib->fade) * 255) << 24);
			C3D_AlphaTest(true, GPU_GREATER, 0);
			C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ZERO, GPU_ONE);
			C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_COLOR);
			C3D_DrawArrays(GPU_TRIANGLES, first, count);
			env_mesh();
			C3D_AlphaTest(true, GPU_GREATER, 0x7f);
			C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
			C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
		} else
			C3D_DrawArrays(GPU_TRIANGLES, first, count);
	}
#undef KIND
	// every section's lamp (after the cubbies' runs: never faded under the boxes)
	set_vec(S.u_tint + 2, lt[0], lt[1], lt[2], 1);
	if (S.case_n > S.case_first[n]) C3D_DrawArrays(GPU_TRIANGLES, S.case_first[n], S.case_n - S.case_first[n]);
	set_vec(S.u_tint + 2, 1, 1, 1, 1);
}

// the real boxes: opaque once faded in, blended over the tile while they come
static void draw_boxes(const room_library *lib, bool first_eye)
{
	if (!lib || lib->cur < 0 || lib->fade <= .001f || !lib->nbox) return;
	static C3D_Tex *tex[3];
	static int count[3], n;
	static room_box last[BOX_MAX];
	static room_mesh_key key;
	int k = S.box_frame & 1;
	// made again when a box moved, after a style change or a new load (once per frame at most, both eyes draw it)
	int nb = lib->nbox < BOX_MAX ? lib->nbox : BOX_MAX;
	static int last_lamp = -1;
	int lamp = lamp_key(lib->lamp);   // the lamp fading up lights them again
	bool same = key.built && key.n == nb && !memcmp(last, lib->box, nb * sizeof(room_box)) && lamp == last_lamp;
	if (room_mesh_key_stale(&key, S.f.gen, lib->cur, nb, same)) {
		if (!first_eye) return;   // the right eye never draws a mesh the left eye has not made
		k = ++S.box_frame & 1;
		n = box_mesh(lib, S.box_vb[k], tex, count);
		GSPGPU_FlushDataCache(S.box_vb[k], n * sizeof(room_vertex));
		room_mesh_key_set(&key, S.f.gen, lib->cur, nb);
		memcpy(last, lib->box, nb * sizeof(room_box));
		last_lamp = lamp;
	}
	if (!n) return;
	C3D_SetBufInfo(&S.buf_box[k]);
	set_vec(S.u_texscale, 1.0f / 32767, 1.0f / 32767, 0, 0);
	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, 0, 0);
	C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
	C3D_TexEnvColor(env, (u32)(fminf(lib->fade, 1) * 255) << 24);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_CullFace(GPU_CULL_NONE);
	bool blend = lib->fade < .999f;
	if (blend) {
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ZERO, GPU_ONE);
		C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_COLOR);
	}
	for (int i = 0, first = 0; i < 3; first += count[i++]) {
		if (!count[i]) continue;
		if (tex[i]) C3D_TexBind(0, tex[i]);
		else {   // plain colours: the vertex colour alone
			C3D_TexEnv *e = C3D_GetTexEnv(0);
			C3D_TexEnvSrc(e, C3D_RGB, GPU_PRIMARY_COLOR, 0, 0);
			C3D_TexEnvFunc(e, C3D_RGB, GPU_REPLACE);
			C3D_TexEnvScale(e, C3D_RGB, GPU_TEVSCALE_4);   // as env_mesh: 1.0 = room_light_unit
		}
		C3D_DrawArrays(GPU_TRIANGLES, first, count[i]);
	}
	env_mesh();
	C3D_AlphaTest(true, GPU_GREATER, 0x7f);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
}

// 4 glows and steam: colour x texture colour, added
static void draw_sprites(bool first_eye)
{
	if (S.sprites_drawn || S.nanim) {
		C3D_TexEnv *env = C3D_GetTexEnv(0);
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_RGB, GPU_PRIMARY_COLOR, GPU_TEXTURE0, 0);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
		C3D_TexEnvSrc(env, C3D_Alpha, GPU_PRIMARY_COLOR, 0, 0);
		C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ZERO, GPU_ONE);
		C3D_BoolUnifSet(GPU_VERTEX_SHADER, S.u_sprite, true);
		if (S.sprites_drawn) C3D_DrawElements(GPU_TRIANGLES, S.sprites_drawn, C3D_UNSIGNED_SHORT, S.ib_sprites);
		static int ak = -1;
		static unsigned steam_frame;
		if (first_eye && (ak < 0 || ++steam_frame % 3 == 0)) {   // the steam: 20 times a second is plenty
			u64 a0 = svcGetSystemTick();
			ak = anim_build();
			S.anim_ticks += svcGetSystemTick() - a0;
		}
		if (ak >= 0) {
			C3D_SetBufInfo(&S.buf_anim[ak]);
			C3D_DrawElements(GPU_TRIANGLES, S.nanim * 6, C3D_UNSIGNED_SHORT, S.anim_ib);
			C3D_SetBufInfo(BUF_ROOM);
		}
		C3D_BoolUnifSet(GPU_VERTEX_SHADER, S.u_sprite, false);
	}
}

// 5 the final pass: + the PS1's dither matrix, then RGB565 precision. On the screen's own 8-bit
// target an AND quad masks it; the New's 400x240 target is RGB565, whose writes truncate: the add
// quad alone gives the same picture. Both eyes the same pattern.
static void final_pass(bool rgb565)
{
	if (!S.dither.data) return;
	float pm[16] = { 0, .01f, 0, 0, -.01f, 0, 0, 0, 0, 0, 0, -.5f, 0, 0, 0, 1 };   // the screen turned a quarter, as room_camera
	if (rgb565) for (int i = 0; i < 8; i++) pm[i] = -pm[i];   // the 400x240 target is turned the other way (draw)
	float px = rgb565 || video_top_plain() ? 1 : 2;   // supersampled: a pattern texel is 2x2 pixels
	C3D_BindProgram(&S.prog);
	C3D_SetAttrInfo(&S.attr);
	C3D_SetBufInfo(&S.buf_post);
	for (int i = 0; i < 4; i++) set_vec(S.u_proj + i, pm[i * 4], pm[i * 4 + 1], pm[i * 4 + 2], pm[i * 4 + 3]);
	slot_identity(0);
	set_vec(S.u_tint, 1, 1, 1, 1);
	C3D_BoolUnifSet(GPU_VERTEX_SHADER, S.u_sprite, false);
	set_vec(S.u_texscale, 1.0f / (16 * 8 * px), 1.0f / (16 * 8 * px), 0, 0);
	for (int i = 1; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, 0, 0);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
	C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, 0, 0);
	C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
	C3D_TexEnvColor(env, ROOM_DITHER_MASK);
	C3D_TexBind(0, &S.dither);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_CullFace(GPU_CULL_NONE);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ZERO, GPU_ONE);   // + the pattern (saturating)
	C3D_DrawElements(GPU_TRIANGLES, 6, C3D_UNSIGNED_SHORT, S.post_ib);
	if (rgb565) { C3D_SetBufInfo(BUF_ROOM); return; }
	env = C3D_GetTexEnv(0);   // again: citro3d uploads a stage's changes only when it is fetched after a draw
	C3D_TexEnvSrc(env, C3D_Both, GPU_CONSTANT, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	C3D_ColorLogicOp(GPU_LOGICOP_AND);   // dst & F8 FC F8: RGB565 precision
	C3D_DrawElements(GPU_TRIANGLES, 6, C3D_UNSIGNED_SHORT, S.post_ib);
	C3D_ColorLogicOp(GPU_LOGICOP_COPY);   // the op back to a plain write before blending is switched on again
	C3D_SetBufInfo(BUF_ROOM);
}

// One eye of the room at pose p. v (the main menu's motion) gives the places' focus
// glow; without it (the Library) none. t: seconds (the TV flicker).
static void draw(const room_pose *pp, const room_view *v, float t, const hands_clip *hands, float hands_t, const room_library *lib)
{
	if (!S.ok) return;
	u64 t0 = svcGetSystemTick();
	C2D_Flush();
	room_pose p = *pp;
	S.drawn = p;
	S.drawn_ok = true;
	float m[16], right[3], up[3], at[3];
	float eye = ui_eye * (ui_parallax / 1.5f) * p.eye * .5f;   // ui_parallax = slider x 1.5
	room_camera(&p, eye, m, right, up, at);
	bool first_eye = ui_eye <= 0;
	// the top screen at 1x (video_room_plain): drawn straight on it; else into the 400x240 target
	bool rt = S.rt && !video_top_plain();
	if (first_eye) {   // once per frame: the room style (the theme), the GPU time of the last one, the sprites still visible
		room_scene_style(theme()->id);
		S.gpu_ms += C3D_GetDrawingTime();
		S.frames++;
		S.sprites_drawn = 0;
		for (int i = 0; i < S.nquad; i++) {
			float dx = S.quad[i].x - p.pos[0], dy = S.quad[i].y - p.pos[1], dz = S.quad[i].z - p.pos[2];
			float fade = sqrtf(dx * dx + dy * dy + dz * dz) / (S.quad[i].size * 2.6f + 1) - .45f;   // as the shader
			if (fade > .004f) { memcpy(S.ib_sprites + S.sprites_drawn, S.quad[i].idx, 12); S.sprites_drawn += 6; }
		}
		GSPGPU_FlushDataCache(S.ib_sprites, S.sprites_drawn * 2);
		room_walls_frame();   // the walls' finished reads and cards (before this frame's draws)
	}
	if (rt) {   // its own 400x240 target, turned the other way (see the blit below)
		C3D_RenderTargetClear(S.rt, C3D_CLEAR_ALL, 0x000000ff, 0);
		C3D_FrameDrawOn(S.rt);
		C3D_SetViewport(0, 0, 240, 400);
		for (int i = 0; i < 8; i++) m[i] = -m[i];
	}

	C3D_BindProgram(&S.prog);
	C3D_SetAttrInfo(&S.attr);
	for (int i = 0; i < 4; i++) set_vec(S.u_proj + i, m[i * 4], m[i * 4 + 1], m[i * 4 + 2], m[i * 4 + 3]);
	set_vec(S.u_right, right[0], right[1], right[2], 0);
	set_vec(S.u_up, up[0], up[1], up[2], 0);
	set_vec(S.u_eye, at[0], at[1], at[2], 0);
	static const float corners[4][2] = { { -1, 1 }, { 1, 1 }, { 1, -1 }, { -1, -1 } };
	for (int i = 0; i < 4; i++) set_vec(S.u_corner + i, corners[i][0], corners[i][1], 0, 0);
	// the palette: static, five places (their focus glow), clock hands, the rest at rest
	for (int i = 0; i < 12; i++) { slot_identity(i); slot_tint(i, 1); }
	if (v) {
		for (int i = 0; i < ROOM_PLACES; i++) slot_tint(1 + i, room_view_tint(v, i));
		slot_tint(8, room_view_tint(v, ROOM_SETTINGS));
		slot_tint(9, room_view_tint(v, ROOM_TOGETHER));
		slot_tint(10, room_view_tint(v, ROOM_TOGETHER));
		slot_tint(11, room_view_tint(v, ROOM_SHARE));
		// Existing invitation mesh, one GPU palette translation, no asset rebuild.
		set_vec(S.u_pal + 11 * 3 + 1, 0, 1, 0, UI_MOTION_CARD_TRAVEL * v->invitation);
	}
	S.lamp_now[0] = S.lamp_now[1] = S.lamp_now[2] = 0;
	if (lib) {   // the bookcase lamp: the case's wood lifted, the open cubby lit, the box in the hands too
		float lt[3];
		room_lamp_case_tint(lib->lamp, lt);
		set_vec(S.u_tint + 2, lt[0], lt[1], lt[2], 1);
		lamp_make(lib, first_eye);
		room_case_geo g;
		if (lib->cur >= 0 && room_case_section(&S.f, lib->cur, &g))
			room_lamp_at(&g, lib->lamp, g.cx, g.base + 90, -160, S.lamp_now);
	}
	C3D_BoolUnifSet(GPU_VERTEX_SHADER, S.u_sprite, false);
	set_vec(S.u_fadek, 2.6f, -.45f, 1, 1);
	set_vec(S.u_lscale, S.f.lrange / 1020.0f, 0, 0, 0);   // vertex colour byte x range / 1020, x4 in env_mesh: 255 = the range
	for (int i = 1; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
	env_mesh();
	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
	C3D_AlphaTest(true, GPU_GREATER, 0x7f);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	C3D_CullFace(GPU_CULL_FRONT_CCW);

	// 1 the room
	const room_bbr_tex *at_tex = &S.f.tex[S.f.style[S.style].atlas];
	set_vec(S.u_texscale, 1.0f / (16 * at_tex->w), -1.0f / (16 * at_tex->h), 0, 1);
	C3D_SetBufInfo(BUF_ROOM);
	C3D_TexBind(0, S.atlas[S.style]);
	C3D_DrawElements(GPU_TRIANGLES, S.room_count, C3D_UNSIGNED_SHORT, S.ib + S.room_first);
	// 2 the TV picture: uv in a 128x128 picture whose 128x96 holds a 4:3 image
	// (the thumbnail is 256x192 in 256x256: the same scale)
	if (S.tv_count) {
		// Snow keeps its texture; a game picture has steady ambient lighting.
		float ou = S.tv_static ? (rand() & 255) / 256.0f : 0, ov = S.tv_static ? (rand() & 63) / 256.0f : 0;
		set_vec(S.u_texscale, 1.0f / (16 * 128), -1.0f / (16 * 128), ou, 1 - ov);
		float flick = 1.0f;
		slot_tint(1, (v ? room_view_tint(v, ROOM_CONTINUE) : 1) * flick);
		C3D_TexBind(0, S.tv_ok ? &S.tv : &S.blank);
		C3D_DrawElements(GPU_TRIANGLES, S.tv_count, C3D_UNSIGNED_SHORT, S.ib + S.tv_first);
	}
	// 3 the bookcase from far away
	if (S.case_n) {
		set_vec(S.u_texscale, 1.0f / (16 * ROOM_SHELF_W), -1.0f / (16 * ROOM_SHELF_H), 0, 1);
		C3D_CullFace(GPU_CULL_NONE);
		C3D_SetBufInfo(&S.buf_case);
		C3D_TexBind(0, &S.shelf);
		draw_case(lib);   // lit by the room style's probes
		C3D_SetBufInfo(BUF_ROOM);
	}
	draw_boxes(lib, first_eye);
	// the poster and the corkboard's cards (room_walls.c): their own textures and light
	env_mesh();
	room_walls_draw(S.u_texscale);
	C3D_SetBufInfo(BUF_ROOM);
	C3D_CullFace(GPU_CULL_FRONT_CCW);
	set_vec(S.u_texscale, 1.0f / (16 * at_tex->w), -1.0f / (16 * at_tex->h), 0, 1);
	C3D_TexBind(0, S.atlas[S.style]);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_COLOR);
	draw_sprites(first_eye);
	// your hands (the Settings picker): in the camera's space, in front of the scope
	if (hands && room_hands_open()) {
		float fw[3] = { p.look[0] - p.pos[0], p.look[1] - p.pos[1], p.look[2] - p.pos[2] };
		float l = sqrtf(fw[0] * fw[0] + fw[1] * fw[1] + fw[2] * fw[2]);
		for (int i = 0; i < 3; i++) fw[i] /= l > 0 ? l : 1;
		float place[12] = { right[0], up[0], fw[0], p.pos[0], right[1], up[1], fw[1], p.pos[1], right[2], up[2], fw[2], p.pos[2] };
		C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
		float at_hands[3], to_cam[3] = { -fw[0], -fw[1], -fw[2] };
		for (int i = 0; i < 3; i++) at_hands[i] = p.pos[i] + fw[i] * 350;
		hands_light(at_hands, to_cam);
		room_hands_draw(hands, hands_t, place, m, NULL);
		room_hands_light(NULL);
	}
	if (lib && lib->after) {   // the Library's hands and case, in this camera and depth
		C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
		lib->after(m, lib->ud);
	}
	final_pass(rt);
	// back to citro2d's state for the 2D drawn over the room
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
	C3D_CullFace(GPU_CULL_NONE);
	C2D_Prepare();
	if (rt) {
		// back on this eye's top target: the room's picture, each texel 2x2 pixels.
		// It was drawn turned so that a rotated subtexture puts it upright.
		static const Tex3DS_SubTexture sub = { 400, 240, 0.0f, 0.0f, 400 / 512.0f, 240 / 256.0f };
		video_top_rebind();
		C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
		C2D_DrawImageAt((C2D_Image){ &S.rt_tex, &sub }, 0, 0, 0.1f, NULL, 1, 1);
	}
	S.ticks += svcGetSystemTick() - t0;
	room_pace_add(ROOM_PACE_TOP, svcGetSystemTick() - t0);
	if (++S.eyes >= 600) {   // CPU time of the room's commands per eye, GPU time per frame
		log_printf("room: %.3f ms CPU per eye (%u eyes; steam %.3f), GPU %.2f ms per frame, %s, %s, %u triangles, linear %uK free, heap largest %uK\n",
			(double)S.ticks * 1000 / SYSCLOCK_ARM11 / S.eyes, S.eyes, (double)S.anim_ticks * 1000 / SYSCLOCK_ARM11 / S.eyes,
			S.frames ? S.gpu_ms / S.frames : 0,
			rt ? "400x240 target" : video_top_plain() ? "1x screen" : "supersampled", S.f.style[S.style].id,
			(unsigned)((S.room_count + S.tv_count + S.sprites_drawn) / 3 + S.case_n / 3),
			(unsigned)(linearSpaceFree() / 1024), (unsigned)(bb_mem_heap_largest() / 1024));
		S.ticks = S.anim_ticks = 0;
		S.eyes = S.frames = 0;
		S.gpu_ms = 0;
	}
}

void room_scene_draw(const room_view *v, const hands_clip *hands, float hands_t)
{
	if (!S.ok) return;
	room_pose p = room_view_pose(v);
	draw(&p, v, v->t, hands, hands_t, NULL);
}

bool room_scene_anchor_screen(const char *name, float *x, float *y, float *w)
{
	const float *a = S.ok && S.drawn_ok ? room_anchor(&S.f, name) : NULL;
	if (!a) return false;
	float m[16], right[3], up[3], at[3], c[2][4];
	room_camera(&S.drawn, 0, m, right, up, at);
	const float pt[2][3] = { { a[0], a[1], a[2] }, { a[0] + right[0] * a[3], a[1] + right[1] * a[3], a[2] + right[2] * a[3] } };
	for (int k = 0; k < 2; k++) {
		for (int i = 0; i < 4; i++) c[k][i] = m[i * 4] * pt[k][0] + m[i * 4 + 1] * pt[k][1] + m[i * 4 + 2] * pt[k][2] + m[i * 4 + 3];
		if (c[k][3] < 30) return false;
	}
	// the screen is turned a quarter: clip y is screen left, clip x screen up
	*x = 200 - 200 * c[0][1] / c[0][3];
	*y = 120 - 120 * c[0][0] / c[0][3];
	*w = hypotf(200 * (c[1][1] / c[1][3] - c[0][1] / c[0][3]), 120 * (c[1][0] / c[1][3] - c[0][0] / c[0][3]));
	return *x > 0 && *x < 400 && *y > 0 && *y < 240 && *w > 4;
}

void room_scene_hands_light(const float pos[3], const float to_cam[3])
{
	if (S.ok) hands_light(pos, to_cam);
}

void room_scene_draw_library(const room_pose *p, float t, const room_library *lib)
{
	draw(p, NULL, t, NULL, 0, lib);
}

// The room wrote depth: 2D drawn over it (titles, tags) must not be depth tested.
void room_scene_overlay_begin(void)
{
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
}

void room_scene_overlay_end(void)
{
	C2D_Flush();
	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
}
