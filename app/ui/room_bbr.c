// BreadBox room: the portable half (no SDK). Reads the BBR tables (v3: the room's styles), holds the
// main menu's camera motion and navigation (as the UI mock R10 does them) and
// builds the bookcase's far view. room_scene.c draws; tests/room checks this.
#include "room.h"
#include "ui_motion.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(room_bbr_header) == 32, "BBR1 header");
_Static_assert(sizeof(room_bbr_tex) == 24, "BBR1 texture entry");
_Static_assert(sizeof(room_bbr_part) == 48, "BBR1 part entry");
_Static_assert(sizeof(room_bbr_anchor) == 56, "BBR1 anchor entry");
_Static_assert(sizeof(room_vertex) == 16, "BBR vertex");
_Static_assert(sizeof(room_probe) == 32, "BBR probe");
_Static_assert(sizeof(room_bbr_style) == 24, "BBR3 style entry");

static bool fail(char *err, size_t cap, const char *what)
{
	if (err && cap) snprintf(err, cap, "%s", what);
	return false;
}

static unsigned light_gen;   // room_file.gen: never the same twice, across files too (a reload is a new light: room_parse uses style 0)

bool room_parse(room_file *f, const void *data, size_t size, char *err, size_t cap)
{
	memset(f, 0, sizeof(*f));
	const uint8_t *d = data;
	if (!d || size < sizeof(room_bbr_header) || ((uintptr_t)d & 3)) return fail(err, cap, "not a room file");
	const room_bbr_header *h = data;
	if (memcmp(h->magic, "BBR1", 4) || h->version < 1 || h->version > 3) return fail(err, cap, "not a BBR room file");
	// v1/v2: plain meshes (props.bbr; v2's probes and lightmap uv are skipped); v3: the room
	if (h->stride != (h->version == 2 ? 20 : 16)) return fail(err, cap, "unknown vertex format");
	f->stride = h->stride;
	unsigned skip = h->version == 2 ? h->flags : 0;   // v2: the header's flags field counts its probes
	f->nstyle = h->version == 3 ? h->flags : 0;
	if (h->version == 3 && !f->nstyle) return fail(err, cap, "a room file without styles");
	if (!h->nvert || h->nvert > 65536 || !h->nidx || h->nidx % 3) return fail(err, cap, "bad mesh size");
	size_t o = sizeof(*h);
	size_t tables = (size_t)h->ntex * sizeof(room_bbr_tex) + (size_t)h->npart * sizeof(room_bbr_part) +
		(size_t)h->nanchor * sizeof(room_bbr_anchor) + (size_t)skip * sizeof(room_probe) + (size_t)f->nstyle * sizeof(room_bbr_style);
	size_t mesh = (size_t)h->nvert * f->stride + (size_t)h->nidx * 2;
	if (o + tables + mesh > size) return fail(err, cap, "room file is cut short");
	f->head = h;
	f->tex = (const void *)(d + o); o += (size_t)h->ntex * sizeof(room_bbr_tex);
	f->part = (const void *)(d + o); o += (size_t)h->npart * sizeof(room_bbr_part);
	f->anchor = (const void *)(d + o); o += (size_t)h->nanchor * sizeof(room_bbr_anchor);
	o += (size_t)skip * sizeof(room_probe);
	f->style = (const void *)(d + o); o += (size_t)f->nstyle * sizeof(room_bbr_style);
	f->vdata = d + o; o += (size_t)h->nvert * f->stride;
	f->idx = (const void *)(d + o);
	f->size = size;
	for (unsigned i = 0; i < h->ntex; i++) {
		const room_bbr_tex *t = &f->tex[i];
		if (t->size && ((size_t)t->offset > size || t->size > size - t->offset)) return fail(err, cap, "texture outside the file");
		if (t->fmt != ROOM_TEX_RUNTIME && t->fmt != ROOM_TEX_RGBA5551 && t->fmt != ROOM_TEX_RGB565 && t->fmt != ROOM_TEX_RGBA4444)
			return fail(err, cap, "texture format");
		if (t->fmt != ROOM_TEX_RUNTIME && t->size != (uint32_t)t->w * t->h * 2) return fail(err, cap, "texture size");
	}
	for (unsigned i = 0; i < f->nstyle; i++) {
		const room_bbr_style *s = &f->style[i];
		size_t cols = (size_t)h->nvert * 4, probes = (size_t)s->nprobe * sizeof(room_probe);
		if ((s->colours & 3) || s->colours > size || cols > size - s->colours) return fail(err, cap, "style colours outside the file");
		if ((s->probes & 3) || s->probes > size || probes > size - s->probes) return fail(err, cap, "style probes outside the file");
		if (s->atlas >= h->ntex || f->tex[s->atlas].fmt != ROOM_TEX_RGBA5551 || !f->tex[s->atlas].size) return fail(err, cap, "style atlas");
		if (s->range != 0 && s->range != 2 && s->range != 4) return fail(err, cap, "style light range");
		unsigned w = f->tex[s->atlas].w, th = f->tex[s->atlas].h;
		if (w < 8 || w > 1024 || (w & (w - 1)) || th < 8 || th > 1024 || (th & (th - 1))) return fail(err, cap, "style atlas size");
		for (unsigned j = 0; j < i; j++)
			if (!strncmp(f->style[j].id, s->id, sizeof(s->id))) return fail(err, cap, "two styles with one id");
	}
	for (unsigned i = 0; i < h->npart; i++) {
		const room_bbr_part *p = &f->part[i];
		if (p->first > h->nidx || p->count > h->nidx - p->first || p->tex >= h->ntex)
			return fail(err, cap, "part outside the mesh");
	}
	for (unsigned i = 0; i < h->nidx; i++)
		if (f->idx[i] >= h->nvert) return fail(err, cap, "index outside the mesh");
	if (f->nstyle) room_style_use(f, 0);
	return true;
}

room_vertex room_vertex_at(const room_file *f, unsigned i)
{
	room_vertex v;
	memcpy(&v, f->vdata + (size_t)i * f->stride, sizeof(v));
	return v;
}

int room_style_find(const room_file *f, const char *id)
{
	for (unsigned i = 0; id && i < f->nstyle; i++)
		if (!strncmp(f->style[i].id, id, sizeof(f->style[i].id))) return (int)i;
	return -1;
}

float room_light_unit(const room_file *f) { return f->lrange > 2 ? 255 / f->lrange : 128; }

bool room_style_use(room_file *f, int i)
{
	if (i < 0 || (unsigned)i >= f->nstyle) return false;
	const uint8_t *base = (const uint8_t *)f->head;
	f->cur = (unsigned)i;
	f->gen = ++light_gen;   // what was lit with the previous style is stale
	f->probe = (const void *)(base + f->style[i].probes);
	f->nprobe = f->style[i].nprobe;
	f->lrange = f->style[i].range ? f->style[i].range : 2;
	return true;
}

const uint8_t *room_style_colours(const room_file *f, int i)
{
	if (i < 0 || (unsigned)i >= f->nstyle || !f->vdata) return NULL;   // after load the file keeps only its tables (room_scene.c)
	return (const uint8_t *)f->head + f->style[i].colours;
}

bool room_probe_light(const room_file *f, const float pos[3], const float dir[3], float out[3])
{
	out[0] = out[1] = out[2] = 1;
	int best[4] = { -1, -1, -1, -1 };
	float bd[4] = { 1e30f, 1e30f, 1e30f, 1e30f };
	for (unsigned i = 0; i < f->nprobe; i++) {
		const room_probe *p = &f->probe[i];
		int sum = 0;
		for (int a = 0; a < 6; a++) sum += p->cube[a][0] + p->cube[a][1] + p->cube[a][2];
		if (sum < 18) continue;   // buried in furniture: black all round
		float dx = p->pos[0] - pos[0], dy = p->pos[1] - pos[1], dz = p->pos[2] - pos[2], d = dx * dx + dy * dy + dz * dz;
		for (int k = 0; k < 4; k++)
			if (d < bd[k]) {
				for (int m = 3; m > k; m--) { bd[m] = bd[m - 1]; best[m] = best[m - 1]; }
				bd[k] = d; best[k] = (int)i;
				break;
			}
	}
	if (best[0] < 0) return false;
	float l = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]), n[3];
	for (int k = 0; k < 3; k++) n[k] = l > 0 ? dir[k] / l : (k == 2 ? -1 : 0);
	float acc[3] = { 0, 0, 0 }, wsum = 0;
	for (int k = 0; k < 4 && best[k] >= 0; k++) {
		const room_probe *p = &f->probe[best[k]];
		float w = 1 / (sqrtf(bd[k]) + 1);
		for (int c = 0; c < 3; c++) {   // the ambient cube along n: squared components pick the faces
			float e = n[0] * n[0] * p->cube[n[0] >= 0 ? 0 : 1][c] + n[1] * n[1] * p->cube[n[1] >= 0 ? 2 : 3][c] + n[2] * n[2] * p->cube[n[2] >= 0 ? 4 : 5][c];
			acc[c] += w * e * f->lrange / 255;   // stored light / range
		}
		wsum += w;
	}
	for (int c = 0; c < 3; c++) out[c] = acc[c] / wsum;
	return true;
}

void room_probe_rgb(const room_file *f, const float pos[3], const float dir[3], float k, uint8_t rgb[3])
{
	float l[3];
	room_probe_light(f, pos, dir, l);
	for (int c = 0; c < 3; c++) {
		float x = l[c] * k * room_light_unit(f);
		rgb[c] = (uint8_t)(x > 255 ? 255 : x < 0 ? 0 : x + .5f);
	}
}

const room_bbr_part *room_part(const room_file *f, const char *name)
{
	for (unsigned i = 0; f->head && i < f->head->npart; i++)
		if (!strncmp(f->part[i].name, name, sizeof(f->part[i].name))) return &f->part[i];
	return NULL;
}

const float *room_anchor(const room_file *f, const char *name)
{
	for (unsigned i = 0; f->head && i < f->head->nanchor; i++)
		if (!strncmp(f->anchor[i].name, name, sizeof(f->anchor[i].name))) return f->anchor[i].v;
	return NULL;
}

const room_bbr_tex *room_tex(const room_file *f, const char *name)
{
	for (unsigned i = 0; f->head && i < f->head->ntex; i++)
		if (!strncmp(f->tex[i].name, name, sizeof(f->tex[i].name))) return &f->tex[i];
	return NULL;
}

// ---------------------------------------------------------------- places

const char *const room_place_id[ROOM_PLACES] = { "continue", "library", "settings", "together", "share" };
const char *const room_place_group[ROOM_PLACES] = { "p_tv", "p_lib", "p_set", "p_tog", "p_share" };

// The mock's map: the TV, bookcase and scope along the wall, the cushions and
// the coffee table in front of them.
static const signed char nav_map[ROOM_PLACES][4] = {
	//               up             down           left           right
	[ROOM_CONTINUE] = { -1,            ROOM_TOGETHER, ROOM_SETTINGS, ROOM_LIBRARY },
	[ROOM_LIBRARY]  = { -1,            ROOM_SHARE,    ROOM_CONTINUE, ROOM_SETTINGS },
	[ROOM_SETTINGS] = { -1,            ROOM_SHARE,    ROOM_LIBRARY,  ROOM_CONTINUE },
	[ROOM_TOGETHER] = { ROOM_CONTINUE, -1,            ROOM_SHARE,    ROOM_SHARE },
	[ROOM_SHARE]    = { ROOM_LIBRARY,  -1,            ROOM_TOGETHER, ROOM_TOGETHER },
};

int room_nav(int place, int dir)
{
	if ((unsigned)place >= ROOM_PLACES || (unsigned)dir > ROOM_RIGHT) return -1;
	return nav_map[place][dir];
}

int room_cycle(int place, int step)
{
	if ((unsigned)place >= ROOM_PLACES) return ROOM_CONTINUE;
	return ((place + step) % ROOM_PLACES + ROOM_PLACES) % ROOM_PLACES;
}

// ---------------------------------------------------------------- camera

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

bool room_pose_of(const room_file *f, const char *id, room_pose *out)
{
	char name[16];
	snprintf(name, sizeof(name), "pose.%s", id);
	const float *v = room_anchor(f, name);
	if (!v) return false;
	*out = (room_pose){ { v[0], v[1], v[2] }, { v[3], v[4], v[5] }, v[6], v[7], v[8] };
	return true;
}

room_pose room_mix(const room_pose *a, const room_pose *b, float u)
{
	float k = ui_motion_ease(u);
	room_pose p;
	for (int i = 0; i < 3; i++) {
		p.pos[i] = lerpf(a->pos[i], b->pos[i], k);
		p.look[i] = lerpf(a->look[i], b->look[i], k);
	}
	p.F = 1 / lerpf(1 / a->F, 1 / b->F, k);
	p.eye = lerpf(a->eye, b->eye, k);
	p.conv = expf(lerpf(logf(a->conv), logf(b->conv), k));
	return p;
}

#define CAM_NEAR 30.0f
#define CAM_FAR 9000.0f

void room_camera(const room_pose *p, float eye, float out[16], float right[3], float up[3], float at[3])
{
	room_camera_ex(p, eye, 120, out, right, up, at);
}

void room_camera_ex(const room_pose *p, float eye, float row, float out[16], float right[3], float up[3], float at[3])
{
	float d[3] = { p->look[0] - p->pos[0], p->look[1] - p->pos[1], p->look[2] - p->pos[2] };
	float yaw = atan2f(d[0], d[2]), pitch = atan2f(d[1], sqrtf(d[0] * d[0] + d[2] * d[2]));
	float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
	float R[3] = { cy, 0, -sy }, U[3] = { -sp * sy, cp, -sp * cy }, W[3] = { cp * sy, sp, cp * cy };
	// camera space rows: x' = R.(q - pos) - eye, y' = U.(q - pos), z' = W.(q - pos)
	float xc[4], yc[4], zc[4];
	for (int i = 0; i < 3; i++) { xc[i] = R[i]; yc[i] = U[i]; zc[i] = W[i]; }
	xc[3] = -(R[0] * p->pos[0] + R[1] * p->pos[1] + R[2] * p->pos[2]) - eye;
	yc[3] = -(U[0] * p->pos[0] + U[1] * p->pos[1] + U[2] * p->pos[2]);
	zc[3] = -(W[0] * p->pos[0] + W[1] * p->pos[1] + W[2] * p->pos[2]);
	// 400x240 picture: screen x = 200 + F x'/z' + eye F/conv, y = 120 - F y'/z'.
	// Depth as citro3d's projections: near -> -1, far -> 0 (z in -w..0).
	float A = (CAM_FAR + CAM_NEAR) / (CAM_FAR - CAM_NEAR), B = -2 * CAM_FAR * CAM_NEAR / (CAM_FAR - CAM_NEAR);
	float kx = p->F / 200, ks = eye * p->F / (p->conv * 200), ky = p->F / 120, kc = (120 - row) / 120;
	for (int i = 0; i < 4; i++) {
		float X = kx * xc[i] + ks * zc[i], Y = ky * yc[i] + kc * zc[i];
		float Z = (.5f * A - .5f) * zc[i] + (i == 3 ? .5f * B : 0);
		// the top screen is a 240x400 framebuffer turned a quarter: clip x = y, clip y = -x
		out[i] = Y;
		out[4 + i] = -X;
		out[8 + i] = Z;
		out[12 + i] = zc[i];
	}
	if (right) memcpy(right, R, sizeof(R));
	if (up) memcpy(up, U, sizeof(U));
	if (at) for (int i = 0; i < 3; i++) at[i] = p->pos[i] + R[i] * eye;
}

// ---------------------------------------------------------------- the main menu's motion

static room_pose pose_or_home(const room_file *f, const char *id)
{
	room_pose p;
	if (room_pose_of(f, id, &p)) return p;
	room_pose_of(f, "home", &p);
	return p;
}

// The home view leans a little towards the focused place.
static room_pose home_pose(const room_view *v)
{
	room_pose b = pose_or_home(v->file, "home");
	if (v->nofocus) return b;
	char name[24];
	snprintf(name, sizeof(name), "focus.%s", room_place_id[v->focus]);
	const float *a = room_anchor(v->file, name);
	if (!a) return b;
	room_pose p = b;
	p.pos[0] = lerpf(b.pos[0], a[0], .025f);
	p.look[0] = lerpf(b.look[0], a[0], .05f);
	p.look[1] = lerpf(b.look[1], a[1], .035f);
	p.look[2] = lerpf(b.look[2], a[2], .025f);
	return p;
}

// a place's poses and anchors: the five places, then the walls (pose.poster, pose.board)
static const char *place_name(int place) { return place == ROOM_POSTER ? "poster" : place == ROOM_BOARD ? "board" : room_place_id[place]; }

static void settle_home(room_view *v)
{
	room_pose h = home_pose(v);
	memcpy(v->att, h.pos, sizeof(h.pos));
	memcpy(v->att + 3, h.look, sizeof(h.look));
	memset(v->attv, 0, sizeof(v->attv));
}

void room_view_init(room_view *v, const room_file *f, int focus, int at_place)
{
	memset(v, 0, sizeof(*v));
	v->file = f;
	v->focus = (unsigned)focus < ROOM_PLACES ? focus : ROOM_CONTINUE;
	v->place = -1;
	v->t0 = -10;
	settle_home(v);
	if ((unsigned)at_place < ROOM_PLACES) {   // back from a place's own screen: glide home from there
		v->place = at_place;
		v->from = pose_or_home(f, room_place_id[at_place]);
		v->mode = ROOM_BACK;
		v->t0 = v->t;
	}
}

static room_pose place_pose(const room_view *v, int place)
{
	if (place == ROOM_LIBRARY && v->lib_on) return v->lib;
	return pose_or_home(v->file, place_name(place));
}

void room_view_set_library(room_view *v, const room_pose *p)
{
	v->lib = *p;
	v->lib_on = true;
	if (v->mode == ROOM_BACK && v->place == ROOM_LIBRARY && v->t == v->t0) v->from = *p;
}

room_pose room_view_pose(const room_view *v)
{
	room_pose home = pose_or_home(v->file, "home");
	switch (v->mode) {
	case ROOM_GO: {
		room_pose to = place_pose(v, v->place);
		return room_mix(&v->from, &to, (v->t - v->t0) / ROOM_GLIDE);
	}
	case ROOM_BACK: {
		room_pose to = home_pose(v);
		return room_mix(&v->from, &to, (v->t - v->t0) / ROOM_GLIDE);
	}
	case ROOM_AT:
		return place_pose(v, v->place);
	default:
		memcpy(home.pos, v->att, sizeof(home.pos));
		memcpy(home.look, v->att + 3, sizeof(home.look));
		if (v->turn > 0) {   // the Quit: the look point swings round the camera (a small yaw, pitch kept)
			float a = UI_MOTION_QUIT_RADIANS * ui_motion_ease(v->turn), c = cosf(a), s = sinf(a);
			float dx = home.look[0] - home.pos[0], dz = home.look[2] - home.pos[2];
			home.look[0] = home.pos[0] + dx * c + dz * s;
			home.look[2] = home.pos[2] - dx * s + dz * c;
		}
		return home;
	}
}

void room_view_step(room_view *v, float dt)
{
	v->t += dt;
	if (v->mode == ROOM_HOME) {   // a soft spring towards the leaning home view
		room_pose h = home_pose(v);
		float target[6] = { h.pos[0], h.pos[1], h.pos[2], h.look[0], h.look[1], h.look[2] };
		for (int i = 0; i < 6; i++) {
			ui_motion_settle(&v->att[i], &v->attv[i], target[i], UI_MOTION_SETTLE_RATE, dt);
		}
	}
	if (v->mode == ROOM_GO && v->t - v->t0 >= ROOM_GLIDE) { v->mode = ROOM_AT; v->t0 = v->t; }
	if (v->mode == ROOM_BACK && v->t - v->t0 >= ROOM_GLIDE) {
		v->mode = ROOM_HOME;
		v->place = -1;
		settle_home(v);
	}
}

void room_view_go(room_view *v, int place)
{
	if ((unsigned)place > ROOM_BOARD) return;
	v->from = room_view_pose(v);   // from where the camera really is
	if (place < ROOM_PLACES) v->focus = place;   // a wall keeps the focus it was looked at from
	v->place = place;
	v->mode = ROOM_GO;
	v->t0 = v->t;
}

void room_view_back(room_view *v)
{
	if (v->place < 0) return;
	v->from = room_view_pose(v);
	v->mode = ROOM_BACK;
	v->t0 = v->t;
}

bool room_view_arrived(const room_view *v) { return v->mode == ROOM_AT; }
void room_view_turn(room_view *v, float k) { v->turn = k < 0 ? 0 : k > 1 ? 1 : k; }

float room_view_tint(const room_view *v, int place)
{
	int lit = v->mode == ROOM_HOME ? v->focus : v->place;
	if (place != lit || (v->nofocus && v->mode == ROOM_HOME)) return 1;
	float u = (v->t - v->t0) / ROOM_GLIDE;
	switch (v->mode) {
	case ROOM_HOME: return 1.16f;
	case ROOM_GO: return 1 + .16f * ui_motion_ease(u);
	case ROOM_BACK: return 1 + .16f * (1 - ui_motion_ease(u));
	default: return 1;
	}
}

// ---------------------------------------------------------------- the bookcase, far away

static int put_quad(room_vertex *o, float x0, float x1, float y0, float y1, float z,
	float u0, float v0, float u1, float v1, int slot, const uint8_t rgb[3])
{
	uint8_t r = rgb[0], g = rgb[1], b = rgb[2];
	room_vertex c[4] = {
		{ (int16_t)lroundf(x0), (int16_t)lroundf(y1), (int16_t)lroundf(z), (int16_t)slot, (int16_t)lroundf(u0 * 16), (int16_t)lroundf(v0 * 16), r, g, b, 255 },
		{ (int16_t)lroundf(x1), (int16_t)lroundf(y1), (int16_t)lroundf(z), (int16_t)slot, (int16_t)lroundf(u1 * 16), (int16_t)lroundf(v0 * 16), r, g, b, 255 },
		{ (int16_t)lroundf(x1), (int16_t)lroundf(y0), (int16_t)lroundf(z), (int16_t)slot, (int16_t)lroundf(u1 * 16), (int16_t)lroundf(v1 * 16), r, g, b, 255 },
		{ (int16_t)lroundf(x0), (int16_t)lroundf(y0), (int16_t)lroundf(z), (int16_t)slot, (int16_t)lroundf(u0 * 16), (int16_t)lroundf(v1 * 16), r, g, b, 255 },
	};
	static const int tri[6] = { 0, 2, 1, 0, 3, 2 };
	for (int i = 0; i < 6; i++) o[i] = c[tri[i]];
	return 6;
}

int room_case_sections(const room_file *f)
{
	const float *c = room_anchor(f, "case");
	return c && c[2] > 0 && c[4] > 0 ? (int)c[2] * (int)c[4] : 0;
}

bool room_case_section(const room_file *f, int i, room_case_geo *g)
{
	const float *c = room_anchor(f, "case");
	if (!c || i < 0 || i >= room_case_sections(f)) return false;
	int bays = (int)c[2], rows = (int)c[4];
	float x0 = c[0], bay = c[1], gap = c[3], upr = c[7], board = c[8];
	g->row = rows - 1 - i / bays;
	g->bay = i % bays;
	g->xl = x0 + g->bay * bay;
	g->xr = g->xl + bay;
	g->xin0 = g->xl + upr / 2;
	g->inner = bay - upr;
	g->cx = g->xl + bay / 2;
	g->base = c[9] + g->row * gap;   // c[9]: the bottom row's floor (the plinth under it)
	g->top = g->base + gap - board;
	return true;
}

void room_cubby_light(const room_file *f, const room_case_geo *g, float k, uint8_t rgb[3])
{
	const float at[3] = { g->cx, (g->base + g->top) / 2, -60 }, toward[3] = { 0, 0, -1 };
	room_probe_rgb(f, at, toward, k, rgb);
}

void room_cubby_light_f(const room_file *f, const room_case_geo *g, float light[3])
{
	const float at[3] = { g->cx, (g->base + g->top) / 2, -60 }, toward[3] = { 0, 0, -1 };
	room_probe_light(f, at, toward, light);
}

void room_box_light(const float in[3], float out[3])
{
	float y = .299f * in[0] + .587f * in[1] + .114f * in[2];
	if (y > ROOM_BOX_KNEE) y = ROOM_BOX_KNEE + (y - ROOM_BOX_KNEE) * .25f;   // bright but never washed out
	out[0] = out[1] = out[2] = y;
}

void room_lit_rgb(const room_file *f, uint32_t c, float k, const float light[3], uint8_t out[3])
{
	float u = room_light_unit(f);
	for (int i = 0; i < 3; i++) {
		float x = ((c >> (8 * i)) & 255) * k * light[i] * u / 255;
		out[i] = (uint8_t)(x > 255 ? 255 : x);
	}
}

// ---------------------------------------------------------------- the bookcase lamp

// A strip light under the board's front edge, the cubby's width:
// an even neutral wash from above, a little brighter at the top, falling off past the strip's ends
#define LAMP_I 1.14f                     // its strength at the spines, 1.0 = neutral light
#define LAMP_REACH 300.0f                // mm: half its strength this far from it
static const float lamp_col[3] = { 1, .97f, .92f };

void room_lamp_at(const room_case_geo *g, float lamp, float x, float y, float z, float out[3])
{
	out[0] = out[1] = out[2] = 0;
	if (lamp <= 0) return;
	float x0 = g->xin0 + 10, x1 = g->xin0 + g->inner - 10;
	float ex = x < x0 ? x0 - x : x > x1 ? x - x1 : 0, along = 1 / (1 + ex * ex / (90.0f * 90.0f));
	float dy = y - (g->top - 8), dz = z - 13, d = sqrtf(dy * dy + dz * dz) + 1;
	float down = -dy / d;
	float i = lamp * LAMP_I * along * (.35f + .65f * (down < 0 ? 0 : down)) / (1 + d * d / (LAMP_REACH * LAMP_REACH));
	for (int a = 0; a < 3; a++) out[a] = i * lamp_col[a];
}

void room_lamp_case_tint(float lamp, float out[3])
{
	// the case's wood only a little lighter (the wood is orange: more would turn it hot)
	for (int a = 0; a < 3; a++) out[a] = 1 + lamp * .10f;
}

// one quad with a colour per corner (corners as put_quad: x0 y1, x1 y1, x1 y0, x0 y0)
static int put_quad4(room_vertex *o, float x0, float x1, float y0, float y1, float z0, float z1,
	float u0, float v0, float u1, float v1, int slot, const uint8_t rgb[4][3])
{
	const float P[4][3] = { { x0, y1, z1 }, { x1, y1, z1 }, { x1, y0, z0 }, { x0, y0, z0 } };
	const float U[4][2] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
	static const int tri[6] = { 0, 2, 1, 0, 3, 2 };
	for (int i = 0; i < 6; i++) {
		int c = tri[i];
		o[i] = (room_vertex){ (int16_t)lroundf(P[c][0]), (int16_t)lroundf(P[c][1]), (int16_t)lroundf(P[c][2]), (int16_t)slot,
			(int16_t)lroundf(U[c][0] * 16), (int16_t)lroundf(U[c][1] * 16), rgb[c][0], rgb[c][1], rgb[c][2], 255 };
	}
	return 6;
}

static void lit_bytes(const room_file *f, const float light[3], const float add[3], float k, uint8_t out[3])
{
	float u = room_light_unit(f);
	for (int a = 0; a < 3; a++) {
		float x = (light[a] + add[a]) * k * u;
		out[a] = (uint8_t)(x > 255 ? 255 : x < 0 ? 0 : x + .5f);
	}
}

#define LAMP_NX 8
#define LAMP_NY 4
#define BACK_K .58f   // a cubby's back: darker than the light in front of it (room_case_mesh)
#define WHITE_U 36.0f   // a white texel patch of the far-view texture (room_shelf_texture)
#define WHITE_V 220.0f

int room_lamp_mesh(const room_file *f, int cubby, float lamp, int slot, room_vertex *out, int cap)
{
	const float *c = room_anchor(f, "case");
	room_case_geo g;
	if (!c || !room_case_section(f, cubby, &g)) return 0;
	float board = c[8], depth = c[6] > 40 ? c[6] : 200, z = depth - 8, y1 = g.base + c[3] - board;
	float light[3];
	room_cubby_light_f(f, &g, light);
	int nv = 0;
	// the back: the same quad as room_case_mesh's, cut in cells, each corner lit
	for (int j = 0; j < LAMP_NY; j++)
		for (int i = 0; i < LAMP_NX && nv + 6 <= cap; i++) {
			float xa = g.xl + (g.xr - g.xl) * i / LAMP_NX, xb = g.xl + (g.xr - g.xl) * (i + 1) / LAMP_NX;
			float ya = g.base + (y1 - g.base) * j / LAMP_NY, yb = g.base + (y1 - g.base) * (j + 1) / LAMP_NY;
			float ua = 128 + .5f + 127.0f * i / LAMP_NX, ub = 128 + .5f + 127.0f * (i + 1) / LAMP_NX;
			float va = 64 + .5f + 63.0f * (LAMP_NY - j - 1) / LAMP_NY, vb = 64 + .5f + 63.0f * (LAMP_NY - j) / LAMP_NY;   // v0 at the top
			const float cx[4] = { xa, xb, xb, xa }, cy[4] = { yb, yb, ya, ya };
			uint8_t rgb[4][3];
			for (int q = 0; q < 4; q++) {
				float add[3];
				room_lamp_at(&g, lamp, cx[q], cy[q], z, add);
				lit_bytes(f, light, add, BACK_K, rgb[q]);
			}
			nv += put_quad4(out + nv, xa, xb, ya, yb, z, z, ua, va, ub, vb, slot, rgb);
		}
	// the strip's underside, from dark to lit
	if (nv + 6 <= cap) {
		float on = lamp < 0 ? 0 : lamp > 1 ? 1 : lamp, glow[3];
		static const float lit[3] = { 1.9f, 1.9f, 1.8f }, off[3] = { .25f, .22f, .2f }, zero[3] = { 0, 0, 0 };
		for (int a = 0; a < 3; a++) glow[a] = off[a] + on * (lit[a] - off[a]);
		uint8_t rgb[4][3];
		for (int q = 0; q < 4; q++) lit_bytes(f, glow, zero, 1, rgb[q]);
		nv += put_quad4(out + nv, g.xin0 + 10, g.xin0 + g.inner - 10, g.top - 8.5f, g.top - 8.5f, 20, 6, WHITE_U, WHITE_V, WHITE_U + 1, WHITE_V + 1, slot, rgb);
	}
	return nv;
}

// every section's strip light, switched off: an aluminium strip under the board's front edge
static int lamp_fixture(const room_file *f, const room_case_geo *g, int slot, room_vertex *o, int cap)
{
	if (cap < 12) return 0;
	float light[3];
	room_cubby_light_f(f, g, light);
	static const float alu[3] = { .62f, .64f, .68f }, zero[3] = { 0, 0, 0 };
	float l2[3] = { light[0] * alu[0], light[1] * alu[1], light[2] * alu[2] };
	uint8_t front[4][3], under[4][3];
	for (int q = 0; q < 4; q++) { lit_bytes(f, l2, zero, 1.2f, front[q]); lit_bytes(f, l2, zero, .35f, under[q]); }
	int n = put_quad4(o, g->xin0 + 8, g->xin0 + g->inner - 8, g->top - 9, g->top, 6, 6, WHITE_U, WHITE_V, WHITE_U + 1, WHITE_V + 1, slot, front);
	n += put_quad4(o + n, g->xin0 + 8, g->xin0 + g->inner - 8, g->top - 9, g->top - 9, 20, 6, WHITE_U, WHITE_V, WHITE_U + 1, WHITE_V + 1, slot, under);
	return n;
}

#define WALL_RISE 40.0f   // mm the Library's wall view looks down from

room_pose room_wall_pose(const room_file *f, int owned, float hy)
{
	room_pose p = pose_or_home(f, "library");
	const float *c = room_anchor(f, "case");
	if (!c) return p;
	// Close to the shelves: the case fills the screen (F px over 400 px), ~2.5 rows in
	// view. The camera looks down a little (RISE), so the bottom row sees widest: there the screen's edge
	// is 2 mm inside the end uprights' outer faces, else the room beside the case shows in the last column
	// (the 1 px light line at the right edge; the 2 mm also covers stereo's offset)
	float gap = c[3], base = c[9], top = base + c[4] * gap, x = c[0] + c[1] * c[2] / 2;
	float half = c[1] * c[2] / 2 + c[7] / 2 - 2, D = half * 300 / 200;
	for (int i = 0; i < 4; i++) D = half * 300 / 200 * (D - 120.0f / 300 * WALL_RISE) / sqrtf(D * D + WALL_RISE * WALL_RISE);
	room_case_geo g;
	float last = owned > 0 && room_case_section(f, owned - 1, &g) ? g.base : base;
	float lo = fminf(last + 2 * gap, top - 2 * gap);
	// at most so high that the view's top edge is 30 mm over the case (its top row's labels whole)
	float hi = top + 30 - 120.0f / 300 * D, y = clampf(hy + gap / 2, fminf(fmaxf(base + 2 * gap, lo), hi), hi);
	p = (room_pose){ { x, y + WALL_RISE, -D }, { x, y, 0 }, 300, 40, D };
	return p;
}

room_pose room_section_pose(const room_file *f, float camx, float base, float hmax, float pk, float speed)
{
	(void)f;
	// Close up: the shelves are near, with whole spines in view.
	(void)speed;
	float d0 = fmaxf(325, 540 * (hmax + 26) / 240), D = lerpf(d0, 660, pk);
	float y = base + hmax / 2 + 4 + 20 * pk;
	return (room_pose){ { camx, y + 8, -D }, { camx, y, 0 }, 600, 12, D };
}

static float section_mm(const room_section *s) { return s->games * (s->thickness + 2) + 2; }

void room_case_plan_make(const room_file *f, const room_section *s, int n, room_case_plan *p)
{
	memset(p, 0, sizeof(*p));
	memset(p->owner, -1, sizeof(p->owner));
	memset(p->decor, -1, sizeof(p->decor));
	room_case_geo g;
	int cubbies = room_case_sections(f);
	if (cubbies > ROOM_CUBBIES) cubbies = ROOM_CUBBIES;
	if (!cubbies || !room_case_section(f, 0, &g)) return;
	p->cubbies = cubbies;
	if (n > cubbies) n = cubbies;
	// One cubby per system, in the case's
	// order from the top left: Favorites, then the systems newest first. All of a system's
	// boxes are in its cubby (more than it holds: they slide behind its uprights)
	for (int i = 0; i < n; i++) {
		p->first[i] = i;
		p->count[i] = 1;
		p->owner[i] = (signed char)i;
		p->fill[i] = s[i].games ? clampf(section_mm(&s[i]) / g.inner, 0, 1) : 0;
	}
	p->nsec = p->used = n;
	// the cubbies nobody uses: something of the room (kept for the systems to come); the `post` anchor's y = 0
	// leaves them empty
	const float *post = room_anchor(f, "post");
	float keep = post ? post[1] : 1;   // the share of empty cubbies that get decor (1: all, .5: every other, 0: none)
	static const signed char order[ROOM_DECORS] = { ROOM_DECOR_BOOKS, ROOM_DECOR_PHOTO, ROOM_DECOR_PLANT, ROOM_DECOR_CASSETTES, ROOM_DECOR_TROPHY };
	for (int c = n, k = 0; c < cubbies; c++, k++)
		if ((int)((k + 1) * keep + .001f) > (int)(k * keep + .001f)) p->decor[c] = order[(k + k / ROOM_DECORS) % ROOM_DECORS];   // neighbours differ
}
// A cubby its system does not fill: where its bookend stands (the right end of its boxes), mm
bool room_case_bookend(const room_file *f, const room_case_plan *p, int sec, int *cubby, float *x)
{
	if (sec < 0 || sec >= p->nsec || p->count[sec] < 1) return false;
	int last = p->first[sec] + p->count[sec] - 1;
	if (p->fill[last] >= .999f || p->fill[p->first[sec]] <= 0) return false;   // full, or empty (Favorites' card)
	for (int c = p->first[sec]; c <= last; c++) {
		room_case_geo g;
		bool full = p->fill[c] >= .999f;
		if (full && p->fill[c + 1] > 0) continue;   // c < last here: the last one is not full
		int at = full ? c + 1 : c;                   // full to its upright: the next cubby's left
		if (!room_case_section(f, at, &g)) return false;
		*cubby = at;
		*x = fminf(g.xin0 + g.inner * (full ? 0 : p->fill[c]) + 4, g.xin0 + g.inner - 5);
		return true;
	}
	return false;
}

// tiles of the far-view texture (128 x 64 each): column, row
#define TILE_U(col) ((col) * 128.0f)
#define TILE_V(row) ((row) * 64.0f)
static const unsigned char decor_tile[ROOM_DECORS][2] = { { 1, 2 }, { 1, 3 }, { 0, 4 }, { 1, 4 }, { 0, 5 } };
enum { BACK_COL = 1, BACK_ROW = 1, STAR_COL = 0, STAR_ROW = 2 };

int room_case_mesh(const room_file *f, const room_section *s, const room_case_plan *p, int slot, room_vertex *out, int cap, int *first, int *backs)
{
	const float *c = room_anchor(f, "case");
	if (first) for (int i = 0; i <= ROOM_CUBBIES; i++) first[i] = 0;
	if (backs) *backs = 0;
	if (!c || !p->cubbies) return 0;
	float board = c[8], depth = c[6] > 40 ? c[6] : 200;
	int nv = 0;
	// the backs: plain wood, darker than the light in front of the cubby
	for (int i = 0; i < p->cubbies && nv + 6 <= cap; i++) {
		room_case_geo g;
		room_case_section(f, i, &g);
		uint8_t l[3];
		room_cubby_light(f, &g, .58f, l);
		nv += put_quad(out + nv, g.xl, g.xr, g.base, g.base + c[3] - board, depth - 8, TILE_U(BACK_COL) + .5f, TILE_V(BACK_ROW) + .5f,
			TILE_U(BACK_COL) + 127.5f, TILE_V(BACK_ROW) + 63.5f, slot, l);
	}
	if (backs) *backs = nv;
	for (int i = 0; i < p->cubbies; i++) {
		if (first) first[i] = nv;
		if (nv + 18 > cap) continue;
		room_case_geo g;
		room_case_section(f, i, &g);
		uint8_t l[3];
		room_cubby_light(f, &g, 1, l);
		float top = g.base + c[3] - board;
		int sec = p->owner[i];
		if (sec >= 0 && s[sec].games > 0 && p->fill[i] > 0) {   // its boxes, cropped to how full the cubby is
			float fill = p->fill[i];
			int st = (unsigned)s[sec].style < ROOM_SPINE_STYLES ? s[sec].style : ROOM_SPINE_MIXED;
			float u = (st % 2) * 128.0f, v = (st / 2) * 64.0f, w = 128 * fill;
			bool flip = i % 2;   // every other cubby mirrors its tile: neighbours don't repeat
			nv += put_quad(out + nv, g.xin0, g.xin0 + g.inner * fill, g.base, top, 6,
				flip ? u + w : u, v + .02f, flip ? u : u + w, v + 64, slot, l);
		} else if (sec >= 0 && !s[sec].games && p->first[sec] == i) {   // an empty Favorites: a card asking for a star
			nv += put_quad(out + nv, g.xin0, g.xin0 + g.inner, g.base, top, 30, TILE_U(STAR_COL), TILE_V(STAR_ROW) + .02f,
				TILE_U(STAR_COL) + 128, TILE_V(STAR_ROW) + 64, slot, l);
		} else if (sec < 0 && p->decor[i] >= 0) {   // a cubby nobody uses: something of the room
			const unsigned char *t = decor_tile[(int)p->decor[i]];
			bool flip = (i / 2) % 2;
			float u0 = TILE_U(t[0]), u1 = u0 + 128;
			nv += put_quad(out + nv, g.xin0, g.xin0 + g.inner, g.base, top, 40, flip ? u1 : u0, TILE_V(t[1]) + .02f,
				flip ? u0 : u1, TILE_V(t[1]) + 64, slot, l);
		}
		int bc;
		float bx;
		if (sec >= 0 && s[sec].games && room_case_bookend(f, p, sec, &bc, &bx) && bc == i)   // the cubby's bookend: dark wood, upright
			nv += put_quad(out + nv, bx - 5, bx + 5, g.base, g.base + 110, 8, .5f, 208.02f, 15.5f, 256, slot, l);
		if (sec >= 0 && p->first[sec] == i) {   // the section's plate: on its first cubby, on the board's front
			float cx = g.cx;
			nv += put_quad(out + nv, cx - 80, cx + 80, g.base - board + 3, g.base - 3, -2.5f, .5f, 192.5f, 127.5f, 207.5f, slot, l);
		}
	}
	if (first) for (int i = p->cubbies; i <= ROOM_CUBBIES; i++) first[i] = nv;
	// after every cubby (drawn whatever the camera does): each section's lamp, switched off
	for (int sec = 0; sec < p->nsec; sec++) {
		room_case_geo g;
		if (p->count[sec] > 0 && room_case_section(f, p->first[sec], &g)) nv += lamp_fixture(f, &g, slot, out + nv, cap - nv);
	}
	return nv;
}

// ---------------------------------------------------------------- the far-view texture

static uint16_t px5551(int r, int g, int b, int a)
{
	r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
	return (uint16_t)((r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 | (a ? 1 : 0));
}

static uint32_t rng_next(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }

// One 128x64 tile: a section's inner width (356 mm) by its height (168 mm),
// boxes standing spine-out; above the boxes the tile is clear (the shelf shows).
// Physical sizes: cartons 23 x 125 mm, jewel cases 125 mm high and 10.5 (North America /
// Japan), 17 (PAL) or 25 mm (two or more discs) thick. No numbers on any spine.
static void spine_tile(uint16_t *px, int x0, int y0, int style, uint32_t seed)
{
	const float mm = 128.0f / 356.0f;
	for (int y = 0; y < 64; y++)
		for (int x = 0; x < 128; x++) px[(y0 + y) * ROOM_SHELF_W + x0 + x] = 0;
	int x = 1;
	while (x < 127) {
		bool carton = style == ROOM_SPINE_CARTON || (style == ROOM_SPINE_MIXED && rng_next(&seed) % 3 == 0);
		int kind = rng_next(&seed) % 10;                  // cases: 0-6 North America, 7-8 PAL, 9 Japan
		bool pal = !carton && kind >= 7 && kind < 9, jp = !carton && kind == 9;
		bool multi = !carton && rng_next(&seed) % 8 == 0;  // a double case now and then
		float t = carton ? 23 : multi ? 25 : pal ? 17 : 10.5f;
		int w = (int)lroundf(t * mm), h = (int)lroundf(125 * 64 / 168.0f);
		if (w < 3) w = 3;
		if (x + w > 127) break;
		int top = 64 - h;
		int br, bg, bb;     // body
		if (carton) {       // one colour from the cover
			static const uint8_t hue[6][3] = { { 196, 60, 48 }, { 52, 92, 170 }, { 230, 180, 40 }, { 60, 140, 80 }, { 120, 70, 160 }, { 220, 110, 40 } };
			const uint8_t *k = hue[rng_next(&seed) % 6];
			br = k[0]; bg = k[1]; bb = k[2];
		} else if (jp) { br = 238; bg = 238; bb = 232; }                  // JP: white spine card
		else if (pal && rng_next(&seed) % 3 == 0) { br = 27; bg = 47; bb = 106; }   // PAL: dark blue
		else { br = bg = 22; bb = 26; }                                   // black
		bool light = carton && br * 3 + bg * 6 + bb > 1500;
		for (int yy = top; yy < 64; yy++)
			for (int xx = x; xx < x + w; xx++) {
				int r = br, g = bg, b = bb;
				if (xx == x) { r = r * 3 / 5; g = g * 3 / 5; b = b * 3 / 5; }   // edge
				if (!carton && !jp && yy < top + 4 && xx > x && xx < x + w - 1) { r = g = b = 210; }   // the logo block
				if (jp && yy < top + 3 && xx > x && xx < x + w - 1) { r = g = b = 60; }              // its small dark logo
				if (pal && yy >= 59 && yy <= 60) { r = g = b = 214; }                                // PAL foot band
				px[(y0 + yy) * ROOM_SHELF_W + x0 + xx] = px5551(r, g, b, 1);
			}
		// the title: a broken line down the middle of the spine
		int tx = x + w / 2, ink = jp || light ? 30 : 236;
		int t0 = carton ? top + 4 : top + 7, t1 = carton ? top + h * 80 / 100 : 58;
		for (int yy = t0; yy < t1; yy++)
			if ((yy + (int)(seed >> 3)) % 7 < 5 || yy < t0 + 3)
				px[(y0 + yy) * ROOM_SHELF_W + x0 + tx] = px5551(ink, ink, ink, 1);
		if (carton)   // GAME BOY ADVANCE at the foot, faint
			for (int yy = top + h * 85 / 100; yy < top + h * 96 / 100; yy++)
				px[(y0 + yy) * ROOM_SHELF_W + x0 + tx] = px5551((br + ink) / 2, (bg + ink) / 2, (bb + ink) / 2, 1);
		x += w + 1;
	}
}

// ---- the decor and the cubby's back, painted in a tile: x 0..127 left to right, y 0..63 up
// from the board (2.8 mm a texel)
typedef struct { uint16_t *px; int x0, y0; } tile_t;
static void dot(tile_t t, int x, int y, int r, int g, int b)
{
	if (x < 0 || x > 127 || y < 0 || y > 63) return;
	t.px[(t.y0 + 63 - y) * ROOM_SHELF_W + t.x0 + x] = px5551(r, g, b, 1);
}
static void box(tile_t t, int x0, int y0, int x1, int y1, int r, int g, int b)   // x0..x1-1, y0..y1-1
{
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++) {
			int k = x == x0 ? 80 : x == x1 - 1 ? 88 : y == y1 - 1 ? 112 : 100;   // edges darker, top lit
			dot(t, x, y, r * k / 100, g * k / 100, b * k / 100);
		}
}
static void disc(tile_t t, float cx, float cy, float rad, int r, int g, int b)
{
	for (int y = (int)(cy - rad); y <= (int)(cy + rad); y++)
		for (int x = (int)(cx - rad); x <= (int)(cx + rad); x++)
			if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= rad * rad) dot(t, x, y, r, g, b);
}
static float hash1(int i) { float x = sinf(i * 127.1f + 311.7f) * 43758.5453f; return x - floorf(x); }

static void paint_back(tile_t t)   // planks of warm wood, darker under the board and at the sides
{
	for (int y = 0; y < 64; y++)
		for (int x = 0; x < 128; x++) {
			int plank = x / 32;
			float grain = .9f + .1f * sinf(x * 1.7f + sinf(y * .21f + plank * 2.1f) * 2.4f) + .05f * (hash1(plank * 7 + y / 9) - .5f);
			float k = grain * (x % 32 == 0 ? .7f : 1) * (.55f + .45f * clampf((63 - y) / 20.0f, 0, 1)) * (.8f + .2f * clampf(fminf(x, 127 - x) / 14.0f, 0, 1));
			dot(t, x, y, (int)(120 * k), (int)(78 * k), (int)(46 * k));
		}
}
static void paint_books(tile_t t)   // a stack lying down, three standing
{
	static const int col[6][3] = { { 150, 44, 40 }, { 44, 82, 142 }, { 200, 160, 64 }, { 62, 112, 72 }, { 112, 60, 120 }, { 190, 110, 50 } };
	int y = 0;
	for (int i = 0; i < 4; i++) {
		int h = 5 + i % 2 * 2, w = 62 - i * 5 + (i == 2 ? 6 : 0), x = 10 + (i * 3) % 7;
		const int *c = col[i];
		box(t, x, y, x + w, y + h, c[0], c[1], c[2]);
		for (int xx = x + 4; xx < x + w - 4; xx++) dot(t, xx, y + h / 2, 220, 190, 110);   // its title band
		y += h;
	}
	for (int i = 0; i < 3; i++) {
		int x = 86 + i * 9, h = 40 + (i * 5) % 9;
		const int *c = col[3 + i];
		box(t, x, 0, x + 8, h, c[0], c[1], c[2]);
		for (int yy = 6; yy < h - 6; yy += 2) dot(t, x + 4, yy, 230, 220, 190);
	}
}
static void paint_plant(tile_t t)   // a terracotta pot, leaves fanning up
{
	for (int i = 0; i < 11; i++) {
		float a = -1.1f + i * .22f + (hash1(i) - .5f) * .2f, len = 22 + hash1(i + 20) * 16;
		for (float s = 0; s < 1; s += .02f) {
			float x = 64 + sinf(a) * len * s + sinf(a) * s * s * 8, y = 14 + cosf(a) * len * s - s * s * 6 * fabsf(a);
			float w = 2.6f * (1 - fabsf(s - .5f) * 1.4f);
			int g = 110 + (int)(50 * s) - i % 3 * 12;
			disc(t, x, y, w, 46 + i % 2 * 10, g, 52);
		}
	}
	for (int y = 0; y < 15; y++) {
		int half = 10 + y / 3;
		for (int x = 64 - half; x < 64 + half; x++) {
			int k = y >= 12 ? 120 : x < 64 - half + 2 ? 75 : 100;
			dot(t, x, y, 182 * k / 100, 92 * k / 100, 52 * k / 100);
		}
	}
}
static void paint_photo(tile_t t)   // a framed landscape and a candle
{
	box(t, 40, 0, 84, 34, 72, 46, 26);
	for (int y = 3; y < 31; y++)
		for (int x = 43; x < 81; x++) {
			float k = (y - 3) / 28.0f;
			int r = (int)(232 - 110 * k), g = (int)(186 - 30 * k), b = (int)(140 + 60 * k);
			if (y < 13 + (x < 62 ? (x - 43) * 12 / 19 : (81 - x) * 12 / 19)) { r = 70; g = 92; b = 112; }   // the mountain
			if (y < 8) { r = 92; g = 120; b = 70; }                                                        // the meadow
			dot(t, x, y, r, g, b);
		}
	disc(t, 72, 25, 2.5f, 255, 236, 180);
	box(t, 94, 0, 99, 12, 232, 222, 200);   // the candle
	disc(t, 96.5f, 14, 1.6f, 255, 200, 80);
}
static void paint_trophy(tile_t t)
{
	box(t, 50, 0, 78, 6, 70, 44, 26);
	box(t, 58, 2, 70, 4, 220, 180, 80);
	box(t, 62, 6, 66, 14, 210, 160, 50);
	for (int y = 14; y < 32; y++) {
		int half = 4 + (y - 14) * 5 / 9;
		for (int x = 64 - half; x < 64 + half; x++) {
			int k = x < 64 - half / 2 ? 120 : x > 64 + half / 2 ? 70 : 100;
			dot(t, x, y, 230 * k / 100, 180 * k / 100, 60 * k / 100);
		}
	}
	for (int s = 0; s < 2; s++)
		for (int y = 20; y < 29; y++) {
			int x = s ? 64 + 13 + (y - 24) * (y - 24) / 6 : 64 - 14 - (y - 24) * (y - 24) / 6;
			dot(t, x, y, 200, 150, 40);
		}
}
static void paint_cassettes(tile_t t)   // game boxes lying in a pile, one standing
{
	static const int col[3][3] = { { 34, 34, 40 }, { 160, 40, 36 }, { 40, 60, 130 } };
	for (int i = 0; i < 3; i++) {
		int x = 18 + (i * 5) % 9, y = i * 7, w = 54 - i * 4;
		box(t, x, y, x + w, y + 7, col[i][0], col[i][1], col[i][2]);
		box(t, x + 6, y + 2, x + 22, y + 5, 214, 210, 200);   // the label
	}
	box(t, 84, 0, 104, 36, 60, 64, 160);
	box(t, 87, 8, 101, 28, 226, 220, 206);
}
static void paint_star(tile_t t)   // a card, propped up: "star a game"
{
	box(t, 46, 0, 82, 30, 238, 228, 200);
	for (int x = 46; x < 82; x++) dot(t, x, 0, 150, 130, 100);
	for (int y = 0; y < 30; y++)
		for (int x = 46; x < 82; x++) {
			float dx = x + .5f - 64, dy = y + .5f - 15, a = atan2f(dy, dx), r = sqrtf(dx * dx + dy * dy);
			float edge = 9.5f * (.5f + .5f * powf(fabsf(cosf(a * 2.5f - 1.5708f * 2.5f)), 3));   // five points, one up
			if (r < edge) {
				bool rim = r > edge - 1.2f;
				dot(t, x, y, rim ? 160 : 242, rim ? 110 : 192, rim ? 30 : 62);
			}
		}
}

void room_shelf_texture(uint16_t *px)
{
	memset(px, 0, ROOM_SHELF_W * ROOM_SHELF_H * 2);
	for (int s = 0; s < ROOM_SPINE_STYLES; s++) spine_tile(px, (s % 2) * 128, (s / 2) * 64, s, 0x9e3779b9u * (s + 1));
	// the brass plate under each section (128 x 16 at 0,192)
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 128; x++) {
			bool edge = x == 0 || y == 0 || x == 127 || y == 15;
			int l = edge ? 120 : 200 - y * 4 + ((x + y) % 9 == 0 ? 20 : 0);
			px[(192 + y) * ROOM_SHELF_W + x] = px5551(l, l * 3 / 4, l / 3, 1);
		}
	// a bookend seen edge on (16 x 48 at 0,208): oiled wood, lit from the left, grain down it
	for (int y = 0; y < 48; y++)
		for (int x = 0; x < 16; x++) {
			int k = x < 3 ? 190 : x > 13 ? 92 : 150 - (x + y / 6) % 5 * 6 + ((y * 7 + x * 3) % 13 == 0 ? -18 : 0);
			if (y < 2) k += 30;   // its top catches the light
			px[(208 + y) * ROOM_SHELF_W + x] = px5551(k, k * 2 / 3, k * 3 / 8, 1);
		}
	for (int y = 216; y < 224; y++)   // a white patch (WHITE_U, WHITE_V): plain faces take their vertex colour
		for (int x = 32; x < 40; x++) px[y * ROOM_SHELF_W + x] = px5551(255, 255, 255, 1);
#define AT(col, row) ((tile_t){ px, (col) * 128, (row) * 64 })
	paint_back(AT(BACK_COL, BACK_ROW));
	paint_star(AT(STAR_COL, STAR_ROW));
	void (*const paint[ROOM_DECORS])(tile_t) = { paint_books, paint_plant, paint_photo, paint_trophy, paint_cassettes };
	for (int d = 0; d < ROOM_DECORS; d++) paint[d](AT(decor_tile[d][0], decor_tile[d][1]));
#undef AT
}

// ---------------------------------------------------------------- the boxes' mesh cache

bool room_mesh_key_stale(const room_mesh_key *k, unsigned gen, int cur, int n, bool same_boxes)
{
	return !k->built || k->gen != gen || k->cur != cur || k->n != n || !same_boxes;
}

void room_mesh_key_set(room_mesh_key *k, unsigned gen, int cur, int n)
{
	*k = (room_mesh_key){ gen, cur, n, true };
}

// ---------------------------------------------------------------- the final pass

// the PlayStation GPU's dither matrix (added to 8-bit colour before it drops to 5 bits a channel)
static const signed char dither_matrix[4][4] = { { -4, 0, -3, 1 }, { 2, -2, 3, -1 }, { -3, 1, -4, 0 }, { 3, -1, 2, -2 } };

// Room lighting: the dither's strength from the room file's `post` anchor (x: 0 = round only, 1 = the PS1's)
static float dither_k = 1;
void room_dither_set(float k) { dither_k = k < 0 ? 0 : k > 1 ? 1 : k; }

void room_dither_texture(uint32_t px[64])
{
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 8; x++) {
			uint32_t rb = (uint32_t)(dither_matrix[y & 3][x & 3] * dither_k + 4.5f), g = rb / 2;   // 0..7 of red's and blue's 8-step, 0..3 of green's 4
			px[y * 8 + x] = rb << 24 | g << 16 | rb << 8 | 255;
		}
}

uint8_t room_dither_channel(int x, int y, int c, uint8_t v)
{
	int add = (int)(dither_matrix[y & 3][x & 3] * dither_k + 4.5f), sum = v + (c == 1 ? add / 2 : add);
	unsigned mask = (ROOM_DITHER_MASK >> (8 * c)) & 255;
	return (uint8_t)((sum > 255 ? 255 : sum) & mask);
}
