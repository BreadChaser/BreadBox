// The hands on the GPU (room.h room_hands_*): romfs:/room/hands.bbh (the rigged
// hand, its clips) and props.bbr (the jewel case and the GBA box they hold).
// One skinned draw per hand (17 bone matrices in the vertex shader, smooth
// normals computed once at load), the case's parts as rigid draws. Skin tone and
// gloves come from the settings. Menu-only: released before a game.
#include "app_policy.h"
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>
#include <citro2d.h>
#include <stddef.h>
#include "app_log.h"
#include "app_config.h"
#include "hand_styles.h"
#include "hands.h"
#include "ui_motion.h"
#include "room.h"
#include "bb_save.h"
#include "hand_shbin.h"

#define HANDS_PATH (room_show_hands ? "romfs:/room/hands.bbh" : "romfs:/room/case-motion.bbh")
#define PROPS_PATH "romfs:/room/props.bbr"
#define CASE_TEX 256

typedef struct { int16_t x, y, z, w, u, v; uint8_t r, g, b, a; int8_t nx, ny, nz, pad; } hand_vertex;
_Static_assert(sizeof(hand_vertex) == 20, "hand vertex");
// the case parts: position, uv, colour. The GPU's stride must be exactly what its
// loaders read (real hardware hangs on a longer one; Azahar does not), so the
// file's 20-byte v2 vertices are copied without their lightmap uv.
typedef struct { int16_t x, y, z, w, u, v; uint8_t r, g, b, a; } prop_vertex;
_Static_assert(sizeof(prop_vertex) == 16, "prop vertex");

static struct {
	bool tried, ok;
	void *file, *props_file;
	hands_file f;
	room_file props;
	hand_vertex *vb;
	prop_vertex *pvb;
	u16 *ib, *pib;
	unsigned bare_first, bare_count, mitt_first, mitt_count;
	C3D_Tex style, case_tex;
	int case_cover;               // the box colour the case texture was made for (-2 none yet)
	uint32_t (*faces)[3];         // per prop part: indices of its front, back and spine (first, in that order)
	int style_tex;                // the file's sleeve texture for this room
	DVLB_s *dvlb;
	shaderProgram_s prog;
	C3D_AttrInfo attr_hand, attr_prop;
	C3D_BufInfo buf_hand, buf_prop;
	int u_proj, u_texscale, u_glove, u_key, u_bones, u_rigid, u_warm;
	u64 ticks;
	unsigned draws;
	const hands_clip *sample_clip;
	float sample_t, sample_now;
	hands_frame sampled;
} H = { .case_cover = -2 };
static int case_band = -1, case_making = -2;   // the case texture being made (case_texture_step)

const hands_file *room_hands_file(void) { return H.ok ? &H.f : NULL; }

// the hands' file read ahead in parts (room_hands_preload_step)
// the hands file read by the later thread (bb_save.h): the menu thread never waits for the SD card
static struct { bb_later_file job, props; bool on; } pre;

static void *read_all(const char *path, size_t *size)
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

// rows top first -> PICA 8x8 tiles, Morton order (as room_scene.c)
static void tile(u16 *dst, const u16 *src, int w, int h)
{
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			dst[(((y >> 3) * (w >> 3) + (x >> 3)) << 6) | (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)] = src[y * w + x];
}

// In five parts, one a frame ahead of time (room_hands_preload_step; all of it took one 17 ms
// frame in Azahar, about twice that on the console), or all at once (room_hands_open).
static float prepare_ms = 1.5f;
void room_hands_prepare_ms(float ms) { prepare_ms = ms; }
static int load_parts;
static u64 load_ticks;
static bb_later_task cpu;
static bool cpu_ok;
// parsed on the later thread right after their reads (bb_later_file.after): 5 ms in Azahar
static bool hands_parsed, props_parsed;
static void parse_hands(bb_later_file *r) { char e[64]; hands_parsed = hands_parse(&H.f, r->buf, r->size, e, sizeof(e)); }
static void parse_props(bb_later_file *r) { char e[64]; props_parsed = room_parse(&H.props, r->buf, r->size, e, sizeof(e)); }
static void cpu_part(void *unused)   // on the later thread: no GPU calls, only memory of its own
{
	(void)unused;
	int8_t (*n)[4] = malloc(H.f.nvert * 4);
	cpu_ok = n != NULL;
	if (!n) return;
	hands_normals(&H.f, n);
	for (unsigned i = 0; i < H.f.nvert; i++) {
		hand_vertex *v = &H.vb[i];
		memcpy(v, H.f.vert + i * 16, 16);
		v->nx = n[i][0]; v->ny = n[i][1]; v->nz = n[i][2]; v->pad = 0;
	}
	free(n);
	memcpy(H.ib, H.f.idx, H.f.nidx * 2);
	tile(H.style.data, H.f.tex[H.style_tex].px, H.f.tex[H.style_tex].w, H.f.tex[H.style_tex].h);
}
// The case's front and back show the box's cover and its spine the shelf's own baked spine,
// drawn from those textures as they are (room_hands_draw): the same picture the shelf shows,
// at the cover's full size once it has come. Their triangles (by the UI mock's case layout,
// case_band_make) go first in each part: front, back, spine, then the rest
enum { FACE_FRONT_, FACE_BACK_, FACE_SPINE_, FACE_REST_ };
static int face_of(const prop_vertex *a, const prop_vertex *b, const prop_vertex *c)
{
	float u = (a->u + b->u + c->u) / (3.0f * 16), v = (a->v + b->v + c->v) / (3.0f * 16);
	if (v < 128) return u < 128 ? FACE_FRONT_ : FACE_BACK_;
	return u < 16 ? FACE_SPINE_ : FACE_REST_;
}
static bool split_faces(void)
{
	unsigned np = H.props.head->npart, most = 0;
	H.faces = calloc(np ? np : 1, sizeof(*H.faces));
	for (unsigned i = 0; i < np; i++) if (H.props.part[i].count > most) most = H.props.part[i].count;
	u16 *tmp = malloc((most ? most : 1) * 2);
	if (!H.faces || !tmp) { free(tmp); return false; }
	for (unsigned i = 0; i < np; i++) {
		const room_bbr_part *p = &H.props.part[i];
		u16 *ix = H.pib + p->first;
		unsigned n = 0;
		for (int f = FACE_FRONT_; f <= FACE_REST_; f++) {
			unsigned before = n;
			for (unsigned k = 0; k + 2 < p->count; k += 3)
				if (face_of(&H.pvb[ix[k]], &H.pvb[ix[k + 1]], &H.pvb[ix[k + 2]]) == f) { memcpy(tmp + n, ix + k, 6); n += 3; }
			if (f < FACE_REST_) H.faces[i][f] = n - before;
		}
		memcpy(ix, tmp, n * 2);
	}
	free(tmp);
	return true;
}
// The case shells' outer sides (a jewel case's tray and lid, a carton and its flap) point at the
// mock layout's white texels, which the hands' light and tint turned pure white in the hand:
// they get their own texels (CASE_SIDE), made in the box's side colour. The cartridge and the
// disc keep the white ones. The file's side vertices are their own (no other face uses them).
#define CASE_SIDE_U 96
static void case_sides(void)
{
	static const char *const shells[] = { "tray", "lid", "box", "flap" };
	for (unsigned i = 0; i < H.props.head->npart; i++) {
		const room_bbr_part *p = &H.props.part[i];
		const char *n = p->name, *u = memchr(n, '_', sizeof(p->name));
		bool shell = false;
		for (unsigned k = 0; u && k < sizeof(shells) / sizeof(*shells); k++)
			if (!strncmp(u + 1, shells[k], sizeof(p->name) - (size_t)(u + 1 - n))) shell = true;
		if (!shell) continue;
		for (unsigned k = 0; k < p->count; k++) {
			prop_vertex *v = &H.pvb[H.pib[p->first + k]];
			if (v->u >= 16 * 16 && v->u < 32 * 16 && v->v >= 128 * 16 && v->v < 144 * 16)
				v->u += (CASE_SIDE_U - 16) * 16;
		}
	}
}
static bool load_part(int part)
{
	u64 t0 = svcGetSystemTick();
	bool ok = true;
	switch (part) {
	case 0: {
	size_t size, psize;
	char err[64];
	if (pre.on && bb_later_wait(&pre.job)) { H.file = pre.job.buf; size = pre.job.size; }   // read ahead (now, if not yet)
	else { free(pre.job.buf); H.file = read_all(HANDS_PATH, &size); hands_parsed = false; }
	pre.job.buf = NULL;
	if (!H.file || !(hands_parsed || hands_parse(&H.f, H.file, size, err, sizeof(err)))) {
		log_printf("hands: %s: %s\n", HANDS_PATH, H.file ? err : "missing");
		return false;
	}
	if (pre.on && bb_later_wait(&pre.props)) { H.props_file = pre.props.buf; psize = pre.props.size; }
	else { free(pre.props.buf); H.props_file = read_all(PROPS_PATH, &psize); props_parsed = false; }
	pre.props.buf = NULL;
	if (!H.props_file || !(props_parsed || room_parse(&H.props, H.props_file, psize, err, sizeof(err)))) {
		log_printf("hands: %s: %s\n", PROPS_PATH, H.props_file ? err : "missing");
		return false;
	}
	if (!room_show_hands) break;   // compact case/camera tracks: no hand meshes or rest solver
	if (!hands_has_sleeve(&H.f)) {   // tex[0] below must be available
		log_printf("hands: %s: no sleeve texture\n", HANDS_PATH);
		return false;
	}
	if (!hands_prepare(&H.f)) return false;   // the index rests: once, not per frame
	unsigned a, b, c, d, e, g;
	if (!hands_part(&H.f, "hand", &a, &b) || !hands_part(&H.f, "hand_sleeve", &c, &d) || !hands_part(&H.f, "hand_mitt", &e, &g)) {
		log_printf("hands: parts missing\n");
		return false;
	}
	H.bare_first = a; H.bare_count = c + d - a;     // hand .. hand_sleeve
	H.mitt_first = c; H.mitt_count = e + g - c;     // hand_sleeve .. hand_mitt
	// the memory here, the arithmetic (smooth normals, the sleeve texture's tiles: 9 + 8 ms
	// in Azahar) on the later thread (bb_save.h); part 1 takes it up
	H.vb = linearAlloc(H.f.nvert * sizeof(hand_vertex));
	H.ib = linearAlloc(H.f.nidx * 2);
	H.style_tex = 0;   // the file's one sleeve texture (the room's)
	if (!H.vb || !H.ib || !C3D_TexInit(&H.style, H.f.tex[H.style_tex].w, H.f.tex[H.style_tex].h, GPU_RGBA5551)) return false;
	cpu = (bb_later_task){ cpu_part, NULL, 0 };
	bb_later_do(&cpu);
	break;
	}
	case 1:   // the mesh with smooth normals and the sleeve texture, from the later thread
	if (!room_show_hands) break;
	bb_later_finish(&cpu);
	if (!cpu_ok) return false;
	GSPGPU_FlushDataCache(H.vb, H.f.nvert * sizeof(hand_vertex));
	GSPGPU_FlushDataCache(H.ib, H.f.nidx * 2);
	C3D_TexFlush(&H.style);
	break;
	case 2:   // the props: vertices and indices as they are
	H.pvb = linearAlloc(H.props.head->nvert * sizeof(prop_vertex));
	H.pib = linearAlloc(H.props.head->nidx * 2);
	if (!H.pvb || !H.pib) return false;
	for (u32 i = 0; i < H.props.head->nvert; i++) {
		room_vertex v = room_vertex_at(&H.props, i);
		H.pvb[i] = (prop_vertex){ v.x, v.y, v.z, v.w, v.u, v.v, v.r, v.g, v.b, v.a };
	}
	memcpy(H.pib, H.props.idx, H.props.head->nidx * 2);
	case_sides();
	if (!split_faces()) return false;
	GSPGPU_FlushDataCache(H.pvb, H.props.head->nvert * sizeof(prop_vertex));
	GSPGPU_FlushDataCache(H.pib, H.props.head->nidx * 2);
	break;
	case 3: {
	if (room_show_hands) C3D_TexSetFilter(&H.style, GPU_NEAREST, GPU_NEAREST);
	if (!C3D_TexInit(&H.case_tex, CASE_TEX, CASE_TEX, GPU_RGB565)) return false;
	C3D_TexSetFilter(&H.case_tex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&H.case_tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	break;
	}
	case 4: {
	H.dvlb = DVLB_ParseFile((u32 *)hand_shbin, hand_shbin_size);
	if (!H.dvlb) return false;
	shaderProgramInit(&H.prog);
	shaderProgramSetVsh(&H.prog, &H.dvlb->DVLE[0]);
	shaderInstance_s *vs = H.prog.vertexShader;
	H.u_proj = shaderInstanceGetUniformLocation(vs, "proj");
	H.u_texscale = shaderInstanceGetUniformLocation(vs, "texscale");
	H.u_glove = shaderInstanceGetUniformLocation(vs, "glove");
	H.u_key = shaderInstanceGetUniformLocation(vs, "key");
	H.u_warm = shaderInstanceGetUniformLocation(vs, "warm");
	H.u_bones = shaderInstanceGetUniformLocation(vs, "bones");
	H.u_rigid = shaderInstanceGetUniformLocation(vs, "rigid");
	AttrInfo_Init(&H.attr_hand);
	AttrInfo_AddLoader(&H.attr_hand, 0, GPU_SHORT, 4);
	AttrInfo_AddLoader(&H.attr_hand, 1, GPU_SHORT, 2);
	AttrInfo_AddLoader(&H.attr_hand, 2, GPU_UNSIGNED_BYTE, 4);
	AttrInfo_AddLoader(&H.attr_hand, 3, GPU_BYTE, 4);
	AttrInfo_Init(&H.attr_prop);
	AttrInfo_AddLoader(&H.attr_prop, 0, GPU_SHORT, 4);
	AttrInfo_AddLoader(&H.attr_prop, 1, GPU_SHORT, 2);
	AttrInfo_AddLoader(&H.attr_prop, 2, GPU_UNSIGNED_BYTE, 4);
	AttrInfo_AddFixed(&H.attr_prop, 3);
	BufInfo_Init(&H.buf_hand);
	if (room_show_hands) BufInfo_Add(&H.buf_hand, H.vb, sizeof(hand_vertex), 4, 0x3210);
	BufInfo_Init(&H.buf_prop);
	BufInfo_Add(&H.buf_prop, H.pvb, sizeof(prop_vertex), 3, 0x210);
	break;
	}
	}
	load_ticks += svcGetSystemTick() - t0;
	if (ok && part == 4)
		log_printf("hands: %s loaded in %lu ms of work: %u vertices, %u clips, %u bones\n", HANDS_PATH,
			(unsigned long)(load_ticks / (SYSCLOCK_ARM11 / 1000)), H.f.nvert, H.f.nclip, H.f.nbone);
	return ok;
}
static bool load_next(void) { return load_part(load_parts++); }
static bool load(void)
{
	while (load_parts < 5)
		if (!load_next()) return false;
	return true;
}

// Ahead of time, a little per frame: the files read by the later thread (bb_save.h), the load
// a part a frame, then the index rests ~1.5 ms a frame.
bool room_hands_preload_step(void)
{
	hands_prepare_mitten = g_cfg.hand_glove == HAND_MITTENS;
	if (H.tried) {   // the index rests ahead, ~1.5 ms a frame (room_hands_prepare_ms)
		if (!H.ok || !room_show_hands) return true;
		static u64 step_ticks;   // the last keyframe's cost: one more only if it fits
		u64 t0 = svcGetSystemTick(), budget = (u64)(SYSCLOCK_ARM11 / 1000 * prepare_ms);
		for (bool first = true; first || svcGetSystemTick() - t0 + step_ticks <= budget; first = false) {
			u64 s0 = svcGetSystemTick();
			if (hands_prepare_step(&H.f)) return true;
			step_ticks = svcGetSystemTick() - s0;
		}
		return false;
	}
	if (!pre.on) {
		pre.on = true;
		hands_parsed = props_parsed = false;
		pre.job = (bb_later_file){ HANDS_PATH, NULL, 0, 0, parse_hands };
		pre.props = (bb_later_file){ PROPS_PATH, NULL, 0, 0, parse_props };
		bb_later_read(&pre.job);
		bb_later_read(&pre.props);
		return false;
	}
	int st = bb_later_state(&pre.props);
	if (st != BB_LATER_DONE && st != BB_LATER_FAILED) return false;
	if (room_show_hands && load_parts == 1 && !bb_later_done(&cpu)) return false;   // its arithmetic still on the later thread
	if (!load_next()) { H.tried = true; room_hands_release(); }   // as room_hands_open on a failure
	else if (load_parts >= 5) { H.tried = true; H.ok = true; }
	return false;
}

bool room_hands_open(void)
{
	if (H.tried) return H.ok;
	H.tried = true;
	H.ok = load();
	if (!H.ok) room_hands_release();
	return H.ok;
}

void room_hands_release(void)
{
	if (room_show_hands && load_parts == 1) bb_later_finish(&cpu);   // its thread writes H's memory: done first
	if (pre.on) {
		bb_later_cancel(&pre.job);
		bb_later_cancel(&pre.props);
		free(pre.job.buf);
		free(pre.props.buf);
	}
	memset(&pre, 0, sizeof(pre));
	if (H.dvlb) { shaderProgramFree(&H.prog); DVLB_Free(H.dvlb); }
	if (H.style.data) C3D_TexDelete(&H.style);
	if (H.case_tex.data) C3D_TexDelete(&H.case_tex);
	if (H.vb) linearFree(H.vb);
	if (H.ib) linearFree(H.ib);
	if (H.pvb) linearFree(H.pvb);
	if (H.pib) linearFree(H.pib);
	free(H.faces);
	hands_free(&H.f);
	free(H.file);
	free(H.props_file);
	memset(&H, 0, sizeof(H));
	hands_parsed = props_parsed = false;
	load_parts = 0;
	load_ticks = 0;
	H.case_cover = -2;
	case_making = -2;
}

// ---------------------------------------------------------------- the case's texture

static u16 c565(u32 c) { return (u16)(((c & 0xff) >> 3) << 11 | (((c >> 8) & 0xff) >> 2) << 5 | ((c >> 16) & 0xff) >> 3); }

// The UI mock's case layout (256 x 256): front 0,0 128x128; back 128,0; spine 0,128 16x128;
// white 16,128; the tray inside 32,128 64x64; tan 32,192 32x32; the shells' sides 96,128 16x16.
// Made a band of 8 rows (one row of PICA tiles) at a time, straight into the texture: the
// shelf makes the chosen box's case ahead (room_hands_case_warm) a few bands a frame, so
// taking it out costs nothing (it took one 34 ms frame on the New 3DS, 84 ms on the Old).
static void case_band_make(const room_hands_case *b, int band)
{
	u16 *d = H.case_tex.data, body = c565(b->color ? b->color : 0xff282830), side = b->side ? c565(b->side) : body;
	for (int y = band * 8; y < band * 8 + 8; y++) {
		u16 *tile_row = d + ((y >> 3) * (CASE_TEX >> 3) << 6);
		int ty = ((y & 1) << 1) | ((y & 2) << 2) | ((y & 4) << 3);
		for (int x = 0; x < CASE_TEX; x++) {
			u16 c = body;
			if (y < 128 || x < 16) {
				// front, back, spine: drawn from the cover and the shelf's spine (split_faces); body here for a box without
			} else if (x < 32 && y < 144) c = 0xffff;
			else if (x >= CASE_SIDE_U && x < CASE_SIDE_U + 16 && y < 144) c = side;
			else if (x >= 32 && x < 96 && y < 192) c = 0x2104;
			else if (x >= 32 && x < 64 && y >= 192 && y < 224) c = c565(0xff8eb4c9);
			tile_row[((x >> 3) << 6) | (x & 1) | ((x & 2) << 1) | ((x & 4) << 2) | ty] = c;
		}
	}
}
// budget 0: all of it now. Only the box's colours are in it (the cover and the spine are not)
static int case_key(const room_hands_case *b) { return (int)((b->color & 0xffffff) ^ (b->side * 2654435761u)) & 0x7fffffff; }
static bool case_texture_step(const room_hands_case *b, u64 budget)
{
	if (H.case_cover == case_key(b)) return true;
	if (!H.case_tex.data) return false;
	if (case_making != case_key(b)) { case_making = case_key(b); case_band = 0; H.case_cover = -2; }
	u64 t0 = svcGetSystemTick();
	while (case_band < CASE_TEX / 8) {
		case_band_make(b, case_band++);
		if (budget && svcGetSystemTick() - t0 >= budget) break;
	}
	if (case_band < CASE_TEX / 8) return false;
	C3D_TexFlush(&H.case_tex);
	H.case_cover = case_key(b);
	case_making = -2;
	return true;
}
static void case_texture(const room_hands_case *b)
{
	if (H.case_cover == case_key(b)) return;
	u64 t0 = svcGetSystemTick();
	int from = case_making == case_key(b) ? case_band : 0;
	case_texture_step(b, 0);
	log_printf("hands: case %d made at take-out from band %d in %.1f ms\n", b->cover_id, from, (double)(svcGetSystemTick() - t0) * 1000 / SYSCLOCK_ARM11);
}
bool room_hands_case_warm(const room_hands_case *b, u64 budget)
{
	return !H.ok || case_texture_step(b, budget ? budget : 1);
}

// ---------------------------------------------------------------- drawing

static void set_vec(int loc, float x, float y, float z, float w) { C3D_FVUnifSet(GPU_VERTEX_SHADER, loc, x, y, z, w); }
static void set_bone(int k, const float m[12])
{
	for (int r = 0; r < 3; r++) set_vec(H.u_bones + k * 3 + r, m[r * 4], m[r * 4 + 1], m[r * 4 + 2], m[r * 4 + 3]);
}

// a part of the cases' file by its role: the name's last word ("tray", "lid", "box", "flap", "cart")
static const room_bbr_part *prop_part(const char *role);
static const room_bbr_part *prop_part(const char *role)
{
	for (unsigned i = 0; i < H.props.head->npart; i++) {
		const char *n = H.props.part[i].name, *u = memchr(n, '_', sizeof(H.props.part[i].name));
		if (u && !strncmp(u + 1, role, sizeof(H.props.part[i].name) - (size_t)(u + 1 - n))) return &H.props.part[i];
	}
	return NULL;
}

// texture coordinates (texels x 16 of the case layout) onto a sub-texture: the layout's
// rectangle (u0, v0, w x h texels) to the sub-texture's corners
static void map_to(const Tex3DS_SubTexture *t, float u0, float v0, float w, float h)
{
	float su = (t->right - t->left) / (w * 16), sv = (t->bottom - t->top) / (h * 16);
	set_vec(H.u_texscale, su, sv, t->left - u0 * 16 * su, t->top - v0 * 16 * sv);
}
static void draw_prop(const char *role, const float place[12], const float obj[12], const float scale[3], const room_hands_case *box)
{
	const room_bbr_part *p = prop_part(role);
	if (!p || !p->count) return;
	float s[12] = { scale[0], 0, 0, 0, 0, scale[1], 0, 0, 0, 0, scale[2], 0 }, m[12];
	hands_mul34(obj, s, m);
	hands_mul34(place, m, m);
	set_bone(0, m);
	const uint32_t *f = H.faces[p - H.props.part];
	const u16 *ix = H.pib + p->first;
	unsigned art = f[FACE_FRONT_] + f[FACE_BACK_], spine_end = art + f[FACE_SPINE_];
	bool cover = box->cover && box->cover_sub && art, spine = box->spine && f[FACE_SPINE_];
	if (cover) {   // the cover; the back: the shelf's generated one, else the cover darker
		// Each pass takes the stage with C3D_GetTexEnv, which marks it for upload: 2490 reset it with
		// C3D_TexEnvInit on the pointer it already had, which citro3d does not upload again, so the
		// back's darkening stayed on for the next draws (the held PS1 cover at 0.59 on 2490)
		for (int pass = 0; pass < 2; pass++) {
			bool back = pass == 0, made = back && box->back;
			unsigned n = f[back ? FACE_BACK_ : FACE_FRONT_];
			C3D_TexEnv *env = C3D_GetTexEnv(1);
			C3D_TexEnvInit(env);
			if (back && !made) {
				C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, 0);
				C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
				C3D_TexEnvColor(env, 0xff969696);
			}
			if (!n) continue;
			C3D_TexBind(0, made ? box->back : box->cover);
			if (made) map_to(&box->back_sub, 128, 0, 128, 128);
			else map_to(box->cover_sub, back ? 128 : 0, 0, 128, 128);
			C3D_DrawElements(GPU_TRIANGLES, n, C3D_UNSIGNED_SHORT, ix + (back ? f[FACE_FRONT_] : 0));
		}
	}
	if (spine) {   // the spine: the shelf's own bake, the same picture as on the shelf
		C3D_TexBind(0, box->spine);
		map_to(&box->spine_sub, 0, 128, 16, 128);
		C3D_DrawElements(GPU_TRIANGLES, f[FACE_SPINE_], C3D_UNSIGNED_SHORT, ix + art);
	}
	// the rest from the case texture (and front / back / spine of a box without that picture)
	C3D_TexBind(0, &H.case_tex);
	set_vec(H.u_texscale, 1.0f / (16 * CASE_TEX), -1.0f / (16 * CASE_TEX), 0, 1);
	unsigned from = cover ? art : 0, to = spine ? art : spine_end;   // what of front / back / spine is not live
	if (to > from) C3D_DrawElements(GPU_TRIANGLES, to - from, C3D_UNSIGNED_SHORT, ix + from);
	if (p->count > spine_end) C3D_DrawElements(GPU_TRIANGLES, p->count - spine_end, C3D_UNSIGNED_SHORT, ix + spine_end);
}

static float warm[3] = { 1, .96f, .9f };
void room_hands_light(const float rgb[3])
{
	static const float usual[3] = { 1, .96f, .9f };
	memcpy(warm, rgb ? rgb : usual, sizeof(warm));
}

static const hands_track *track;
static float track_now;
void room_hands_use_track(const hands_track *k, float now) { track = k; track_now = now; }
static void sample(const hands_clip *c, float t, hands_frame *fr)
{
	// The camera and both eyes share one sample. With hands off, the compact
	// file has only camera/prop keys: no hand sampling or finger corrections.
	if (H.sample_clip != c || H.sample_t != t || H.sample_now != track_now) {
		if (track && track->c == c) hands_track_sample(&H.f, track, track_now, &H.sampled);
		else hands_sample(&H.f, c, t, &H.sampled);
		H.sample_clip = c; H.sample_t = t; H.sample_now = track_now;
	}
	*fr = H.sampled;
}

// warm skin tones (UI mock), multiplied into the skin-masked texels; the light one is capped at 1
static const float skins[HAND_SKINS][3] = { { 1, 1, .95f }, { .96f, .8f, .65f }, { .72f, .52f, .38f }, { .47f, .33f, .24f } };

float room_hands_draw(const hands_clip *c, float t, const float place[12], const float proj[16], const room_hands_case *box)
{
	if (!H.ok || !c) return 0;
	u64 t0 = svcGetSystemTick();
	hands_frame fr;
	if (!room_show_hands && box && !strncmp(c->name, "open_", 5)) {
		// Default launch: the steady cover dissolves; no disembodied case opening.
		float fade = ui_motion_ease(t / fmaxf(hands_clip_seconds(&H.f, c), .001f));
		const hands_clip *hold = hands_clip_for(&H.f, "hold", c->box, false);
		if (hold) sample(hold, 0, &fr);
		else sample(c, t, &fr);
		fr.fade = fade;
	} else sample(c, t, &fr);
	if (box && box->yaw_mode != HANDS_YAW_NONE)   // the turn in the hand goes on the way pressed (hands_for_shelf)
		hands_case_turn(&fr, box->yaw_mode == HANDS_YAW_SET ? box->yaw - hands_case_yaw(&fr) : box->yaw);
	bool mitten = g_cfg.hand_glove == HAND_MITTENS;
	C2D_Flush();
	C3D_BindProgram(&H.prog);
	for (int i = 0; i < 4; i++) set_vec(H.u_proj + i, proj[i * 4], proj[i * 4 + 1], proj[i * 4 + 2], proj[i * 4 + 3]);
	set_vec(H.u_key, -.42f, .72f, -.55f, 0);   // from the upper left, in front
	// the shader's colour output clamps at 1.0 before the combiner's scale: warm x 0.56 (its brightest side) must stay
	// under it, or the hands lose their shading (Day's probes reach 3.9). Brighter light: half the colour, x4.
	float wmax = fmaxf(warm[0], fmaxf(warm[1], warm[2]));
	int wscale = wmax * .56f > 1 ? 4 : 2;
	set_vec(H.u_warm, warm[0] * 2 / wscale, warm[1] * 2 / wscale, warm[2] * 2 / wscale, 1);
	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	C3D_CullFace(GPU_CULL_NONE);
	for (int i = 1; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
	// the case: its parts placed by the clip, scaled from the clip's box to this one
	if (box && (fr.objects & (1u << HANDS_CASE)) && c->box != HANDS_BOX_NONE) {
		case_texture(box);
		C3D_TexEnv *env = C3D_GetTexEnv(0);
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
		C3D_TexEnvScale(env, C3D_RGB, GPU_TEVSCALE_2);
		C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, 0, 0);
		C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
		C3D_TexEnvColor(env, 0xffffffff);
		C3D_SetAttrInfo(&H.attr_prop);
		C3D_FixedAttribSet(3, 0, 1, 0, 0);
		C3D_SetBufInfo(&H.buf_prop);
		C3D_BoolUnifSet(GPU_VERTEX_SHADER, H.u_rigid, true);
		float sc[3] = { box->thw[0] / c->thw[0], box->thw[1] / c->thw[1], box->thw[2] / c->thw[2] };
		const float *obj = fr.obj[HANDS_CASE];
		if (hands_box_is_case(c->box)) {   // a jewel case (any thickness): tray and lid
			draw_prop("tray", place, obj, sc, box);
			draw_prop("lid", place, (fr.objects & (1u << HANDS_LID)) ? fr.obj[HANDS_LID] : obj, sc, box);
		} else {                          // a carton: box, flap, the cartridge inside
			draw_prop("box", place, obj, sc, box);
			draw_prop("flap", place, (fr.objects & (1u << HANDS_FLAP)) ? fr.obj[HANDS_FLAP] : obj, sc, box);
			if (fr.objects & (1u << HANDS_CART)) draw_prop("cart", place, fr.obj[HANDS_CART], sc, box);
		}
	}
	// the hands: texture x light x 2 x lerp(1, skin, texture alpha); the glove's tile by offset
	if (room_show_hands && (fr.shown[0] || fr.shown[1])) {
		const float *sk = skins[(unsigned)g_cfg.hand_skin < HAND_SKINS ? g_cfg.hand_skin : 1];
		int gl = (unsigned)g_cfg.hand_glove < HAND_GLOVES ? g_cfg.hand_glove : 0;
		u32 one_minus_skin = (u32)((1 - sk[0]) * 255) | (u32)((1 - sk[1]) * 255) << 8 | (u32)((1 - sk[2]) * 255) << 16 | 0xff000000u;
		C3D_TexEnv *env = C3D_GetTexEnv(0);   // a (1 - skin)
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, 0);
		C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_ALPHA, GPU_TEVOP_RGB_SRC_COLOR, 0);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
		C3D_TexEnvColor(env, one_minus_skin);
		C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, 0, 0);
		C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
		env = C3D_GetTexEnv(1);   // (1 - a (1 - skin)) x texture
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_TEXTURE0, 0);
		C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, 0);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
		env = C3D_GetTexEnv(2);   // x light x 2 (x 4 when warm was halved)
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_PRIMARY_COLOR, 0);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
		C3D_TexEnvScale(env, C3D_RGB, wscale == 4 ? GPU_TEVSCALE_4 : GPU_TEVSCALE_2);
		C3D_SetAttrInfo(&H.attr_hand);
		C3D_SetBufInfo(&H.buf_hand);
		C3D_BoolUnifSet(GPU_VERTEX_SHADER, H.u_rigid, false);
		set_vec(H.u_texscale, 1.0f / (16 * H.style.width), -1.0f / (16 * H.style.height), 0, 1);
		set_vec(H.u_glove, H.f.glove[gl][0] * 16, H.f.glove[gl][1] * 16, 0, 0);   // the file's own table (v3)
		C3D_TexBind(0, &H.style);
		for (int h = 0; h < 2; h++) {
			if (!fr.shown[h]) continue;
			float M[HANDS_MAX_BONES][12];
			hands_pose(&H.f, &fr, h, mitten, M);
			for (unsigned k = 0; k < H.f.nbone; k++) {
				float w[12];
				hands_mul34(place, M[k], w);
				set_bone((int)k, w);
			}
			C3D_DrawElements(GPU_TRIANGLES, mitten ? H.mitt_count : H.bare_count, C3D_UNSIGNED_SHORT,
				H.ib + (mitten ? H.mitt_first : H.bare_first));
		}
	}
	for (int i = 0; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
	C2D_Prepare();
	H.ticks += svcGetSystemTick() - t0;
	if (++H.draws >= 300) {
		log_printf("hands: %.3f ms CPU per draw (decode, kinematics, uniforms; %u draws)\n", (double)H.ticks * 1000 / SYSCLOCK_ARM11 / H.draws, H.draws);
		H.ticks = 0;
		H.draws = 0;
	}
	return fr.fade;
}

// The camera of a clip frame (its space) with this frame's look: for the shelf.
bool room_hands_camera(const hands_clip *c, float t, float cam[7])
{
	if (!H.ok || !c) return false;
	hands_frame fr;
	// The flight camera and case were authored together. Keep them synchronized;
	// freezing only the camera lets the case cross the near plane during pickup.
	const hands_clip *hold = !room_show_hands && !strncmp(c->name, "open_", 5)
		? hands_clip_for(&H.f, "hold", c->box, false) : NULL;
	if (hold) sample(hold, 0, &fr);
	else sample(c, t, &fr);
	memcpy(cam, fr.cam, sizeof(fr.cam));
	return true;
}
