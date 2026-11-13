// The hands, portable half (hands.h): BBH1 tables, clip sampling, forward
// kinematics, smooth normals, and which clip the bookshelf is in.
#include "hands.h"
#include "ui_motion.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static int16_t rds16(const uint8_t *p) { return (int16_t)rd16(p); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }
static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

static bool fail(char *err, size_t cap, const char *what)
{
	if (err && cap) snprintf(err, cap, "%s", what);
	return false;
}

bool hands_parse(hands_file *f, const void *data, size_t size, char *err, size_t cap)
{
	memset(f, 0, sizeof(*f));
	const uint8_t *d = data;
	if (!d || size < 48 || memcmp(d, "BBH1", 4) || ((uintptr_t)d & 3)) return fail(err, cap, "not a BBH1 file");
	f->data = d; f->size = size;
	f->version = rd16(d + 4); f->fps = rd16(d + 6);
	f->ntex = rd16(d + 8); f->npart = rd16(d + 10); f->nbone = rd16(d + 12); f->nclip = rd16(d + 14);
	f->nvert = rd32(d + 16); f->nidx = rd32(d + 20);
	uint32_t ov = rd32(d + 24), oi = rd32(d + 28), ot = rd32(d + 32), op = rd32(d + 36), ob = rd32(d + 40), oc = rd32(d + 44);
	if (!f->fps || f->nbone < 2 || f->nbone > HANDS_MAX_BONES || f->ntex > 8 || f->npart > 8 || !f->nclip || !f->nvert || f->nvert > 65536)
		return fail(err, cap, "unsupported hand file");
	if ((uint64_t)ov + 16ull * f->nvert > size || (uint64_t)oi + 2ull * f->nidx > size || (uint64_t)ot + 16ull * f->ntex > size ||
	    (uint64_t)op + 32ull * f->npart > size || (uint64_t)ob + 40ull * f->nbone > size || (uint64_t)oc + 48ull * f->nclip > size ||
	    (ov & 3) || (oi & 1) || (oc & 3))
		return fail(err, cap, "hand file cut short");
	f->vert = d + ov;
	f->idx = (const uint16_t *)(d + oi);
	for (unsigned i = 0; i < f->nidx; i++)
		if (f->idx[i] >= f->nvert) return fail(err, cap, "index outside the mesh");
	for (unsigned i = 0; i < f->nvert; i++) {
		int b = rds16(f->vert + i * 16 + 6);
		if (b < 0 || b >= (int)f->nbone) return fail(err, cap, "vertex bone out of range");
	}
	for (unsigned i = 0; i < f->ntex; i++) {
		const uint8_t *t = d + ot + i * 16;
		memcpy(f->tex[i].style, t, 8);
		f->tex[i].w = rd16(t + 8); f->tex[i].h = rd16(t + 10);
		uint32_t off = rd32(t + 12);
		if ((uint64_t)off + 2ull * f->tex[i].w * f->tex[i].h > size || (off & 1)) return fail(err, cap, "texture outside the file");
		f->tex[i].px = (const uint16_t *)(d + off);
	}
	for (unsigned i = 0; i < f->npart; i++) {
		const uint8_t *p = d + op + i * 32;
		memcpy(f->part[i].name, p, 12);
		f->part[i].first = rd32(p + 12); f->part[i].count = rd32(p + 16);
		if (f->part[i].first + f->part[i].count > f->nidx) return fail(err, cap, "part outside the mesh");
	}
	for (unsigned i = 0; i < f->nbone; i++) {
		const uint8_t *b = d + ob + i * 40;
		for (int k = 0; k < 3; k++) {
			f->bone[i].head[k] = rdf(b + k * 4);
			f->bone[i].flex[k] = rdf(b + 12 + k * 4);
			f->bone[i].spread[k] = rdf(b + 24 + k * 4);
		}
		f->bone[i].parent = (int8_t)b[36];
		if (i >= 2 && (f->bone[i].parent < 0 || f->bone[i].parent >= (int)i)) return fail(err, cap, "bone order");
	}
	// v3: the glove table follows the bones' three anchors; v2: the 128x64 texture's tiles
	static const float v2_gloves[6][2] = { { 0, 0 }, { 32, 0 }, { 64, 0 }, { 0, 32 }, { 32, 32 }, { 64, 32 } };
	memcpy(f->glove, v2_gloves, sizeof(f->glove));
	if (f->version >= 3) {
		uint64_t og = (uint64_t)ob + 40ull * f->nbone + 40 * 3;
		if (og + 24 > size) return fail(err, cap, "glove table");
		for (int g = 0; g < 6; g++) { f->glove[g][0] = rds16(d + og + g * 4); f->glove[g][1] = rds16(d + og + g * 4 + 2); }
	}
	f->clip = (const hands_clip *)(d + oc);
	unsigned joints = 2 * (f->nbone - 2);
	for (unsigned i = 0; i < f->nclip; i++) {
		const hands_clip *c = &f->clip[i];
		unsigned want = 20;
		for (int h = 0; h < 2; h++) if (c->mask & (1u << h)) want += 2 * (24 + joints);
		for (int o = 0; o < HANDS_OBJECTS; o++) if (c->mask & (4u << o)) want += 24;
		if (!c->frames || c->frame_bytes != want || (uint64_t)c->offset + (uint64_t)c->frame_bytes * c->frames > size)
			return fail(err, cap, "clip layout");
	}
	return true;
}

const hands_clip *hands_clip_named(const hands_file *f, const char *name, bool mitten)
{
	char want[24];
	for (int pass = mitten ? 0 : 1; pass < 2; pass++) {
		snprintf(want, sizeof(want), pass ? "%s" : "%s_m", name);
		for (unsigned i = 0; f->clip && i < f->nclip; i++)
			if (!strncmp(f->clip[i].name, want, sizeof(f->clip[i].name))) return &f->clip[i];
	}
	return NULL;
}

const hands_clip *hands_clip_for(const hands_file *f, const char *action, int box, bool mitten)
{
	size_t n = strlen(action);
	const hands_clip *bare = NULL;
	for (unsigned i = 0; f->clip && i < f->nclip; i++) {
		const hands_clip *c = &f->clip[i];
		if (c->box != box || strncmp(c->name, action, n) || c->name[n] != '_') continue;
		if ((c->flags & 1) == mitten) return c;
		if (!(c->flags & 1)) bare = c;
	}
	if (!bare && (box == HANDS_BOX_CASE_PAL || box == HANDS_BOX_CASE_MULTI)) return hands_clip_for(f, action, HANDS_BOX_CASE, mitten);
	return bare;
}

float hands_clip_seconds(const hands_file *f, const hands_clip *c) { return c ? (float)c->frames / f->fps : 0; }
// A loop's last key is its first again (round 8: hold, card hold, picker idle): one period is
// frames - 1 keys, and the curve's neighbours wrap across the seam
float hands_loop_seconds(const hands_file *f, const hands_clip *c)
{
	return c && c->loop && c->frames > 1 ? (float)(c->frames - 1) / f->fps : hands_clip_seconds(f, c);
}

bool hands_has_sleeve(const hands_file *f)
{
	return f->ntex > 0 && f->tex[0].w && f->tex[0].h && f->tex[0].px;
}

bool hands_part(const hands_file *f, const char *name, unsigned *first, unsigned *count)
{
	for (unsigned i = 0; i < f->npart; i++)
		if (!strncmp(f->part[i].name, name, 12)) { *first = f->part[i].first; *count = f->part[i].count; return true; }
	return false;
}

// ---------------------------------------------------------------- frames

static void m34_read(const uint8_t *p, float m[12])
{
	for (int k = 0; k < 12; k++) {
		int16_t v = rds16(p + k * 2);
		m[k] = k % 4 == 3 ? v / 4.0f : v / 16384.0f;
	}
}

static void decode(const hands_file *f, const hands_clip *c, int i, hands_frame *o)
{
	if (i < 0) i = 0;
	if (i >= c->frames) i = c->frames - 1;
	const uint8_t *p = f->data + c->offset + (size_t)c->frame_bytes * i;
	memset(o, 0, sizeof(*o));
	o->fade = p[1] / 255.0f;
	for (int k = 0; k < 6; k++) o->cam[k] = rds16(p + 4 + k * 2) / 4.0f;
	o->cam[6] = rds16(p + 16);
	p += 20;
	unsigned joints = 2 * (f->nbone - 2);
	for (int h = 0; h < 2; h++) {
		if (!(c->mask & (1u << h))) continue;
		o->shown[h] = (f->data[c->offset + (size_t)c->frame_bytes * i] >> h) & 1;
		m34_read(p, o->palm[h]); p += 24;
		m34_read(p, o->sleeve[h]); p += 24;
		for (unsigned k = 0; k < joints; k++) o->joint[h][k] = rds16(p + k * 2) / 8192.0f;
		p += joints * 2;
	}
	for (int k = 0; k < HANDS_OBJECTS; k++) {
		if (!(c->mask & (4u << k))) continue;
		m34_read(p, o->obj[k]); p += 24;
		o->objects |= 1u << k;
	}
}

// re-orthonormalise the 3x3 of an interpolated matrix (columns), keeping its handedness
static void ortho(float m[12], float scale[3])
{
	float x[3] = { m[0], m[4], m[8] }, y[3] = { m[1], m[5], m[9] }, z0[3] = { m[2], m[6], m[10] };
	float l = sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
	scale[0] = l;
	if (l < 1e-6f) return;
	for (int k = 0; k < 3; k++) x[k] /= l;
	float d = x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
	for (int k = 0; k < 3; k++) y[k] -= x[k] * d;
	l = sqrtf(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
	scale[1] = l;
	if (l < 1e-6f) return;
	for (int k = 0; k < 3; k++) y[k] /= l;
	float z[3] = { x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0] };
	float dot = z[0] * z0[0] + z[1] * z0[1] + z[2] * z0[2];
	scale[2] = fabsf(dot);
	float s = dot < 0 ? -1 : 1;
	for (int k = 0; k < 3; k++) { m[k * 4] = x[k]; m[k * 4 + 1] = y[k]; m[k * 4 + 2] = z[k] * s; }
}

// rotation matrix (columns x y z in a 3x4 row-major) <-> unit quaternion (w x y z)
static void m34_quat(const float m[12], float q[4])
{
	float tr = m[0] + m[5] + m[10];
	if (tr > 0) {
		float s = sqrtf(tr + 1) * 2;
		q[0] = s / 4; q[1] = (m[9] - m[6]) / s; q[2] = (m[2] - m[8]) / s; q[3] = (m[4] - m[1]) / s;
	} else if (m[0] > m[5] && m[0] > m[10]) {
		float s = sqrtf(1 + m[0] - m[5] - m[10]) * 2;
		q[0] = (m[9] - m[6]) / s; q[1] = s / 4; q[2] = (m[1] + m[4]) / s; q[3] = (m[2] + m[8]) / s;
	} else if (m[5] > m[10]) {
		float s = sqrtf(1 + m[5] - m[0] - m[10]) * 2;
		q[0] = (m[2] - m[8]) / s; q[1] = (m[1] + m[4]) / s; q[2] = s / 4; q[3] = (m[6] + m[9]) / s;
	} else {
		float s = sqrtf(1 + m[10] - m[0] - m[5]) * 2;
		q[0] = (m[4] - m[1]) / s; q[1] = (m[2] + m[8]) / s; q[2] = (m[6] + m[9]) / s; q[3] = s / 4;
	}
}
static void quat_m34(const float q[4], float m[12])
{
	float w = q[0], x = q[1], y = q[2], z = q[3];
	m[0] = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y - w * z); m[2] = 2 * (x * z + w * y);
	m[4] = 2 * (x * y + w * z); m[5] = 1 - 2 * (x * x + z * z); m[6] = 2 * (y * z - w * x);
	m[8] = 2 * (x * z - w * y); m[9] = 2 * (y * z + w * x); m[10] = 1 - 2 * (x * x + y * y);
}
static float det33(const float m[12])
{
	return m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
}
// a towards b by k: the rotation along the shorter way (a mirrored hand keeps its mirror), the position lerped
static void mix34(float a[12], const float b[12], float k, bool keep_scale)
{
	float A[12], B[12];
	memcpy(A, a, sizeof(A));
	memcpy(B, b, sizeof(B));
	// Keep the authored package dimensions as well as its rotation. Exact keys
	// retain these scales; fractional keys must do the same (cartons are square).
	float sa[3] = {1, 1, 1}, sb[3] = {1, 1, 1};
	ortho(A, sa);
	ortho(B, sb);
	bool mirror = det33(A) < 0;
	if (mirror) for (int r = 0; r < 3; r++) { A[r * 4 + 2] = -A[r * 4 + 2]; B[r * 4 + 2] = -B[r * 4 + 2]; }
	float qa[4], qb[4], q[4];
	m34_quat(A, qa);
	m34_quat(B, qb);
	float d = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
	if (d < 0) { d = -d; for (int i = 0; i < 4; i++) qb[i] = -qb[i]; }
	// normalised lerp: between 30 Hz keys the same as a slerp, and no acosf (libm's layout)
	(void)d;
	float wa = 1 - k, wb = k, l = 0;
	for (int i = 0; i < 4; i++) { q[i] = wa * qa[i] + wb * qb[i]; l += q[i] * q[i]; }
	l = sqrtf(l);
	for (int i = 0; i < 4; i++) q[i] /= l > 0 ? l : 1;
	float t[3] = { a[3] + (b[3] - a[3]) * k, a[7] + (b[7] - a[7]) * k, a[11] + (b[11] - a[11]) * k };
	quat_m34(q, a);
	if (mirror) for (int r = 0; r < 3; r++) a[r * 4 + 2] = -a[r * 4 + 2];
	if (keep_scale) for (int col = 0; col < 3; col++) {
		float scale = sa[col] + (sb[col] - sa[col]) * k;
		for (int row = 0; row < 3; row++) a[row * 4 + col] *= scale;
	}
	a[3] = t[0]; a[7] = t[1]; a[11] = t[2];
}

static void rest_apply(hands_frame *fr, int h, const float r[2]);
static void rest_compute(const hands_file *f, const hands_clip *c, const hands_frame *fr, int h, float out[2]);

// Keyframes decoded and fixed (hands_rest_index) once: a frame samples two, both
// eyes and the camera sample the same ones, and they change at 30 Hz.
#define KCACHE 12   // a frame samples four keys, a clip change eight
static struct { const uint8_t *data; const hands_clip *c; int i; hands_frame fr; } kcache[KCACHE];
static int knext;
static void keyframe(const hands_file *f, const hands_clip *c, int i, hands_frame *o)
{
	if (i < 0) i = 0;
	if (i >= c->frames) i = c->frames - 1;
	for (int k = 0; k < KCACHE; k++)
		if (kcache[k].c == c && kcache[k].i == i && kcache[k].data == f->data) { *o = kcache[k].fr; return; }
	decode(f, c, i, o);
	if (f->rest) {   // computed once per keyframe: ahead (hands_prepare_step), else the first time it is shown
		unsigned at = f->rest_first[c - f->clip] + (unsigned)i;
		if (!f->rest_key[at]) {
			for (int h = 0; h < 2; h++) rest_compute(f, c, o, h, f->rest[at][h]);
			f->rest_key[at] = 1;
		}
		for (int h = 0; h < 2; h++) rest_apply(o, h, f->rest[at][h]);
	} else if (c->mask & 3)
		hands_rest_index(f, c, o);
	kcache[knext].data = f->data; kcache[knext].c = c; kcache[knext].i = i; kcache[knext].fr = *o;
	knext = (knext + 1) % KCACHE;
}

void hands_frame_mix(hands_frame *a, const hands_frame *b, float k)
{
	if (k <= 0) return;
	if (k >= 1) { *a = *b; return; }
	a->fade += (b->fade - a->fade) * k;
	for (int j = 0; j < 7; j++) a->cam[j] += (b->cam[j] - a->cam[j]) * k;
	for (int h = 0; h < 2; h++) {
		if (!b->shown[h]) { if (k > .5f) a->shown[h] = false; continue; }
		if (!a->shown[h]) {   // a hand coming in: it is only in b
			memcpy(a->palm[h], b->palm[h], sizeof(a->palm[h]));
			memcpy(a->sleeve[h], b->sleeve[h], sizeof(a->sleeve[h]));
			memcpy(a->joint[h], b->joint[h], sizeof(a->joint[h]));
			a->shown[h] = true;
			continue;
		}
		mix34(a->palm[h], b->palm[h], k, false);
		mix34(a->sleeve[h], b->sleeve[h], k, false);
		for (unsigned j = 0; j < 2 * HANDS_MAX_BONES; j++) a->joint[h][j] += (b->joint[h][j] - a->joint[h][j]) * k;
	}
	for (int o = 0; o < HANDS_OBJECTS; o++) {
		uint32_t bit = 1u << o;
		if (a->objects & b->objects & bit) mix34(a->obj[o], b->obj[o], k, true);
		else if (b->objects & bit) { memcpy(a->obj[o], b->obj[o], sizeof(a->obj[o])); a->objects |= bit; }
	}
}

// Catmull-Rom through four keys: the motion's speed changes smoothly from key to key
// (linear interpolation of 30 Hz keys changes it 30 times a second: a judder at 60 Hz)
static float cr(float p0, float p1, float p2, float p3, float t)
{
	return .5f * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (3 * p1 - p0 - 3 * p2 + p3) * t * t * t);
}
static void cr_pos(float *o, const float *a, const float *p1, const float *p2, const float *b, float t)
{
	static const int at[3] = { 3, 7, 11 };
	for (int i = 0; i < 3; i++) o[at[i]] = cr(a[at[i]], p1[at[i]], p2[at[i]], b[at[i]], t);
}

void hands_sample(const hands_file *f, const hands_clip *c, float t, hands_frame *out)
{
	float x = t * f->fps;
	if (x < 0) x = 0;
	int n = c->frames, period = c->loop && n > 2 ? n - 1 : 0;   // a loop: key n - 1 is key 0
	if (period) x = fmodf(x, (float)period);
	int i = (int)x;
	float k = x - i;
	keyframe(f, c, i, out);
	if ((!period && i + 1 >= n) || k < 1e-5f) return;
	hands_frame b, a, d, p1 = *out;
	if (period) {
		keyframe(f, c, (i + 1) % period, &b);
		keyframe(f, c, (i + period - 1) % period, &a);
		keyframe(f, c, (i + 2) % period, &d);
	} else {
		keyframe(f, c, i + 1, &b);
		keyframe(f, c, i > 0 ? i - 1 : 0, &a);
		keyframe(f, c, i + 2 < n ? i + 2 : n - 1, &d);
	}
	hands_frame_mix(out, &b, k);   // rotations slerped, then the positions on the curve
	for (int j = 0; j < 7; j++) out->cam[j] = cr(a.cam[j], p1.cam[j], b.cam[j], d.cam[j], k);
	for (int h = 0; h < 2; h++) {
		if (!(a.shown[h] && p1.shown[h] && b.shown[h] && d.shown[h])) continue;
		cr_pos(out->palm[h], a.palm[h], p1.palm[h], b.palm[h], d.palm[h], k);
		cr_pos(out->sleeve[h], a.sleeve[h], p1.sleeve[h], b.sleeve[h], d.sleeve[h], k);
		for (unsigned j = 0; j < 2 * (f->nbone - 2); j++) out->joint[h][j] = cr(a.joint[h][j], p1.joint[h][j], b.joint[h][j], d.joint[h][j], k);
	}
	for (int o = 0; o < HANDS_OBJECTS; o++)
		if (a.objects & p1.objects & b.objects & d.objects & (1u << o)) cr_pos(out->obj[o], a.obj[o], p1.obj[o], b.obj[o], d.obj[o], k);
}

void hands_track_set(hands_track *k, const hands_clip *c, float t, float now)
{
	if (c != k->c) {
		k->from = k->c;
		k->from_t = k->t;
		k->since = now;
		k->c = c;
	}
	k->t = t;
}

void hands_track_sample(const hands_file *f, const hands_track *k, float now, hands_frame *out)
{
	memset(out, 0, sizeof(*out));
	if (!k->c) return;
	hands_sample(f, k->c, k->t, out);
	float u = (now - k->since) / HANDS_BLEND;
	if (!k->from || u >= 1 || u < 0) return;
	hands_frame a;
	float d = hands_clip_seconds(f, k->from), ft = k->from_t + (now - k->since);   // the last clip runs on
	if (k->from->loop) d = hands_loop_seconds(f, k->from);
	ft = k->from->loop && d > 0 ? fmodf(ft, d) : fminf(ft, d - 1e-3f);
	hands_sample(f, k->from, ft, &a);
	float w = ui_motion_ease(u);
	hands_frame_mix(&a, out, w);
	*out = a;
}

// ---------------------------------------------------------------- kinematics

void hands_mul34(const float a[12], const float b[12], float o[12])
{
	float r[12];
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 4; j++)
			r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j];
		r[i * 4 + 3] += a[i * 4 + 3];
	}
	memcpy(o, r, sizeof(r));
}

static void axis_angle(const float ax[3], float a, float m[12])
{
	float l = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
	float x = l > 0 ? ax[0] / l : 1, y = l > 0 ? ax[1] / l : 0, z = l > 0 ? ax[2] / l : 0, c = cosf(a), s = sinf(a), t = 1 - c;
	float r[12] = { t * x * x + c, t * x * y - s * z, t * x * z + s * y, 0,
		t * x * y + s * z, t * y * y + c, t * y * z - s * x, 0,
		t * x * z - s * y, t * y * z + s * x, t * z * z + c, 0 };
	memcpy(m, r, sizeof(r));
}

void hands_pose(const hands_file *f, const hands_frame *fr, int hand, bool mitten, float out[][12])
{
	float joint[2 * HANDS_MAX_BONES];
	memcpy(joint, fr->joint[hand], sizeof(joint));
	// mittens: the pouch rides the middle finger; the other fingers follow its curl
	if (mitten && f->nbone >= 17)
		for (int j = 0; j < 3; j++) {
			float flex = joint[(8 + j - 2) * 2];
			joint[(5 + j - 2) * 2] = joint[(11 + j - 2) * 2] = joint[(14 + j - 2) * 2] = flex;
		}
	memcpy(out[0], fr->sleeve[hand], sizeof(out[0]));
	memcpy(out[1], fr->palm[hand], sizeof(out[1]));
	for (unsigned k = 2; k < f->nbone; k++) {
		float fl = joint[(k - 2) * 2], sp = joint[(k - 2) * 2 + 1], L[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
		if (fl) axis_angle(f->bone[k].flex, fl, L);
		if (sp) { float S[12]; axis_angle(f->bone[k].spread, sp, S); hands_mul34(S, L, L); }
		// about the joint: T(head) L T(-head)
		const float *h = f->bone[k].head;
		for (int i = 0; i < 3; i++) L[i * 4 + 3] = h[i] - (L[i * 4] * h[0] + L[i * 4 + 1] * h[1] + L[i * 4 + 2] * h[2]);
		hands_mul34(out[f->bone[k].parent], L, out[k]);
	}
}

// The index finger's chain (bones 5-7) posed alone from the palm: points along
// it (4 per bone, then the tip) in the case's frame (x thickness, y up from the
// bottom, z width). The tip: the distal bone's head plus the middle phalanx again.
#define INDEX_POINTS 13
static void index_points(const hands_file *f, const hands_frame *fr, int h, const float joint[6], float out[INDEX_POINTS][3])
{
	float M[3][12];
	const float *parent = fr->palm[h];
	for (int b = 0; b < 3; b++) {
		int k = 5 + b;
		float L[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
		if (joint[b * 2]) axis_angle(f->bone[k].flex, joint[b * 2], L);
		if (joint[b * 2 + 1]) { float S[12]; axis_angle(f->bone[k].spread, joint[b * 2 + 1], S); hands_mul34(S, L, L); }
		const float *hd = f->bone[k].head;
		for (int i = 0; i < 3; i++) L[i * 4 + 3] = hd[i] - (L[i * 4] * hd[0] + L[i * 4 + 1] * hd[1] + L[i * 4 + 2] * hd[2]);
		hands_mul34(parent, L, M[b]);
		parent = M[b];
	}
	const float *cm = fr->obj[HANDS_CASE];
	int n = 0;
	for (int b = 0; b < 3; b++)
		for (int i = 0; i < 4 + (b == 2); i++) {
			float u = i / 4.0f, p[3], w[3];
			for (int k = 0; k < 3; k++) {
				float a = f->bone[5 + b].head[k], e = b < 2 ? f->bone[6 + b].head[k] : 2 * f->bone[7].head[k] - f->bone[6].head[k];
				p[k] = a + (e - a) * u;
			}
			for (int r = 0; r < 3; r++) w[r] = M[b][r * 4] * p[0] + M[b][r * 4 + 1] * p[1] + M[b][r * 4 + 2] * p[2] + M[b][r * 4 + 3] - cm[r * 4 + 3];
			for (int k = 0; k < 3; k++) out[n][k] = cm[k] * w[0] + cm[4 + k] * w[1] + cm[8 + k] * w[2];
			n++;
		}
}

// distance from a point to the case's box (0 inside)
static float box_distance(const float q[3], const float thw[3])
{
	float dx = fmaxf(fabsf(q[0]) - thw[0] / 2, 0), dy = fmaxf(fmaxf(-q[1], q[1] - thw[1]), 0), dz = fmaxf(fabsf(q[2]) - thw[2] / 2, 0);
	return sqrtf(dx * dx + dy * dy + dz * dz);
}

#define FINGER 6.0f   // the finger's half thickness (mm)
// the index's change for one hand of a keyframe: spread added, curl toward the middle finger
static void rest_compute(const hands_file *f, const hands_clip *c, const hands_frame *fr, int h, float out[2]);
static void rest_apply(hands_frame *fr, int h, const float r[2])
{
	if (!r[0] && !r[1]) return;
	fr->joint[h][(5 - 2) * 2 + 1] += r[0];
	for (int b = 0; b < 3; b++) {
		float *jf = &fr->joint[h][(5 + b - 2) * 2];
		*jf += (fr->joint[h][(8 + b - 2) * 2] - *jf) * r[1];
	}
}

void hands_rest_index(const hands_file *f, const hands_clip *c, hands_frame *fr)
{
	for (int h = 0; h < 2; h++) {
		float r[2];
		rest_compute(f, c, fr, h, r);
		rest_apply(fr, h, r);
	}
}

static void rest_compute(const hands_file *f, const hands_clip *c, const hands_frame *fr, int h, float out[2])
{
	out[0] = out[1] = 0;
	if (!c || (c->flags & 1) || !(fr->objects & (1u << HANDS_CASE)) || f->nbone < 17 || c->thw[1] <= 0) return;   // mittens: no index drawn
	{
		if (!fr->shown[h]) return;
		float own[6], q[INDEX_POINTS][3];
		for (int b = 0; b < 3; b++) { own[b * 2] = fr->joint[h][(5 + b - 2) * 2]; own[b * 2 + 1] = fr->joint[h][(5 + b - 2) * 2 + 1]; }
		index_points(f, fr, h, own, q);
		const float *tip = q[INDEX_POINTS - 1];
		// a gripping hand (the tip over the case's footprint) whose index stands up over the top
		float above = tip[1] - c->thw[1] - FINGER;
		if (above < 3 || fabsf(tip[0]) > c->thw[0] / 2 + 25 || fabsf(tip[2]) > c->thw[2] / 2 + 25) return;
		// never closer to the case than the export's own pose was (its knuckle may touch the edge)
		float near = FINGER - 2;
		for (int k = 0; k < INDEX_POINTS - 1; k++) near = fminf(near, box_distance(q[k], c->thw) - .5f);
		float best = 1e9f, bs = 0, bc = 0;
		// coarse (0.4 rad x 0.2 curl), then finer around the best
		for (int pass = 0; pass < 2; pass++) {
			float s0 = pass ? bs - .2f : -1.2f, s1 = pass ? bs + .2f : 1.2f, ss = pass ? .1f : .4f;
			float c0 = pass ? fmaxf(0, bc - .1f) : 0, c1 = pass ? fminf(1, bc + .1f) : 1, cs = pass ? .05f : .2f;
			if (pass && best >= 1e9f) break;
			for (float sp = s0; sp <= s1 + 1e-4f; sp += ss)
				for (float cu = c0; cu <= c1 + 1e-4f; cu += cs) {
					float j[6];
					memcpy(j, own, sizeof(j));
					j[1] += sp;
					for (int b = 0; b < 3; b++) j[b * 2] = own[b * 2] + (fr->joint[h][(8 + b - 2) * 2] - own[b * 2]) * cu;   // toward the middle finger's curl
					index_points(f, fr, h, j, q);
					bool clear = true;
					for (int k = 0; k < INDEX_POINTS && clear; k++) clear = box_distance(q[k], c->thw) >= fmaxf(near, 0);
					if (!clear) continue;
					// resting: the tip one finger thickness off the case, not above its top; then the smallest change
					float d = box_distance(q[INDEX_POINTS - 1], c->thw);
					float cost = fabsf(d - FINGER) * 2 + fmaxf(0, q[INDEX_POINTS - 1][1] - c->thw[1]) + fabsf(sp) * 8 + cu * 4;
					if (cost < best) { best = cost; bs = sp; bc = cu; }
				}
		}
		if (best >= 1e9f) return;
		float w = ui_motion_ease((above - 3) / 10);   // fades in: no pop where the export already rests
		out[0] = bs * w;
		out[1] = bc * w;
	}
}

bool hands_prepare(hands_file *f)
{
	unsigned n = 0;
	for (unsigned i = 0; i < f->nclip; i++) n += f->clip[i].frames;
	f->rest = calloc(n, sizeof(*f->rest));
	f->rest_first = calloc(f->nclip, sizeof(*f->rest_first));
	if (!f->rest || !f->rest_first) { hands_free(f); return false; }
	f->rest_done = calloc(f->nclip, 1);
	f->rest_key = calloc(n, 1);
	if (!f->rest_done || !f->rest_key) { hands_free(f); return false; }
	for (unsigned i = 0, at = 0; i < f->nclip; i++) {
		f->rest_first[i] = at;
		at += f->clip[i].frames;
	}
	return true;
}

// ahead of time: one keyframe a call (the caller calls it for as long as its frame allows;
// a whole clip took one 20-140 ms frame on the console)
static const hands_file *prep_file;
static unsigned prep_clip;
static int prep_key;
static void rest_key(const hands_file *f, unsigned i, int k)
{
	unsigned at = f->rest_first[i] + (unsigned)k;
	if (f->rest_key[at]) return;   // already shown once
	hands_frame fr;
	decode(f, &f->clip[i], k, &fr);
	for (int h = 0; h < 2; h++) rest_compute(f, &f->clip[i], &fr, h, f->rest[at][h]);
	f->rest_key[at] = 1;
}
bool hands_prepare_mitten;   // the glove in use: its clips first
// the next clip to prepare: the glove in use first, and of its clips the ones a take-out plays
// (pick, hold, put back), then the turns, then the rest
static int clip_rank(const hands_file *f, unsigned i)
{
	const hands_clip *c = &f->clip[i];
	int rank = ((c->flags & 1) != 0) == hands_prepare_mitten ? 0 : 3;
	if (!strncmp(c->name, "pick", 4) || !strncmp(c->name, "hold", 4) || !strncmp(c->name, "putback", 7)) return rank;
	return rank + (!strncmp(c->name, "turn", 4) ? 1 : 2);
}
static bool next_clip(const hands_file *f)
{
	for (int rank = 0; rank < 6; rank++)
		for (unsigned i = 0; i < f->nclip; i++)
			if (!f->rest_done[i] && clip_rank(f, i) == rank) {
				if (i != prep_clip) { prep_clip = i; prep_key = 0; }
				return true;
			}
	return false;
}
bool hands_prepare_step(hands_file *f)
{
	if (!f->rest_done) return true;
	if (prep_file != f) { prep_file = f; prep_clip = 0; prep_key = 0; }
	if (prep_clip >= f->nclip || f->rest_done[prep_clip] || prep_key == 0)   // a clip at a time, the best one next
		if (!next_clip(f)) return true;
	rest_key(f, prep_clip, prep_key++);
	if (prep_key >= f->clip[prep_clip].frames) f->rest_done[prep_clip] = 1;
	return false;
}
void hands_free(hands_file *f)
{
	if (prep_file == f) prep_file = NULL;
	free(f->rest);
	free(f->rest_first);
	free(f->rest_done);
	free(f->rest_key);
	f->rest_key = NULL;
	f->rest = NULL;
	f->rest_first = NULL;
	f->rest_done = NULL;
}

// ---------------------------------------------------------------- smooth normals

static const uint8_t *sort_vert;
static int cmp_pos(const void *a, const void *b)
{
	const uint8_t *p = sort_vert + *(const uint16_t *)a * 16, *q = sort_vert + *(const uint16_t *)b * 16;
	return memcmp(p, q, 6);
}

void hands_normals(const hands_file *f, int8_t (*out)[4])
{
	float (*acc)[3] = calloc(f->nvert, sizeof(*acc));
	uint16_t *order = malloc(f->nvert * sizeof(*order));
	unsigned *bin = malloc(f->nvert * sizeof(*bin));
	if (!acc || !order || !bin) { free(acc); free(order); free(bin); memset(out, 0, f->nvert * 4); return; }
	// weld: vertices with the same position share one normal
	for (unsigned i = 0; i < f->nvert; i++) order[i] = (uint16_t)i;
	sort_vert = f->vert;
	qsort(order, f->nvert, sizeof(*order), cmp_pos);
	for (unsigned i = 0, b = 0; i < f->nvert; i++) {
		if (i && memcmp(f->vert + order[i] * 16, f->vert + order[i - 1] * 16, 6)) b = i;
		bin[order[i]] = order[b];
	}
	for (unsigned t = 0; t + 2 < f->nidx; t += 3) {
		float p[3][3];
		for (int v = 0; v < 3; v++)
			for (int k = 0; k < 3; k++) p[v][k] = rds16(f->vert + f->idx[t + v] * 16 + k * 2);
		float e1[3] = { p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2] };
		float e2[3] = { p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2] };
		float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
		for (int v = 0; v < 3; v++) {   // area weighted; the light is two-sided, so align signs per position
			float *a = acc[bin[f->idx[t + v]]];
			float s = a[0] * n[0] + a[1] * n[1] + a[2] * n[2] < 0 ? -1 : 1;
			for (int k = 0; k < 3; k++) a[k] += s * n[k];
		}
	}
	for (unsigned i = 0; i < f->nvert; i++) {
		const float *a = acc[bin[i]];
		float l = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
		for (int k = 0; k < 3; k++) out[i][k] = (int8_t)lroundf(l > 0 ? a[k] / l * 127 : (k == 1) * 127);
		out[i][3] = 0;
	}
	free(acc); free(order); free(bin);
}

// ---------------------------------------------------------------- the bookshelf

// The clip whose pose is the hand's grip at a side (its last frame; the front's is the hold).
static const char *const grip_clip[3] = { "hold", "turn_r1", "turn_r2" };
// The case's yaw (about the vertical: its z axis's heading) in a frame.
float hands_case_yaw(const hands_frame *fr) { return atan2f(fr->obj[HANDS_CASE][2], fr->obj[HANDS_CASE][10]); }
// Turn the case and its parts (lid, flap, cartridge) by delta radians about the vertical through the case's origin.
void hands_case_turn(hands_frame *fr, float delta)
{
	if (!(fr->objects & (1u << HANDS_CASE)) || delta == 0) return;
	float c = cosf(delta), s = sinf(delta), o[3] = { fr->obj[HANDS_CASE][3], fr->obj[HANDS_CASE][7], fr->obj[HANDS_CASE][11] };
	for (int k = 0; k < HANDS_OBJECTS; k++) {
		if (!(fr->objects & (1u << k))) continue;
		float *m = fr->obj[k];
		for (int j = 0; j < 4; j++) {   // the rotation's columns, then the position about the case's origin
			float x = j < 3 ? m[j] : m[3] - o[0], z = j < 3 ? m[8 + j] : m[11] - o[2];
			m[j] = c * x + s * z + (j == 3 ? o[0] : 0);
			m[8 + j] = -s * x + c * z + (j == 3 ? o[2] : 0);
		}
	}
}
// The case's yaw resting at a side: its grip's, half round more at the open edge.
static float grip_yaw(const hands_file *f, int box, bool mitten, int side)
{
	int g = HANDS_GRIP(side);
	const hands_clip *c = hands_clip_for(f, grip_clip[g], box, mitten);
	if (!c) return 0;
	hands_frame fr;
	hands_sample(f, c, g ? hands_clip_seconds(f, c) - 1e-3f : 0, &fr);
	return hands_case_yaw(&fr) + (side == 3 ? (float)M_PI : 0);
}

hands_shelf_clip hands_for_shelf(const hands_file *f, int box, bool mitten, const hands_shelf_state *s)
{
	hands_shelf_clip r = { 0 };
#define CLIP(n) hands_clip_for(f, n, box, mitten)
	if (s->play_t0 >= 0) {   // game start: the case opens in both hands, then black
		r.clip = CLIP("open");
		r.t = ui_motion_ease((s->t - s->play_t0) / UI_MOTION_LAUNCH_S) * (hands_clip_seconds(f, r.clip) - 1e-3f);
		r.camera_k = 1;
		return r;
	}
	float u = s->pull_seconds > 0 ? clampf((s->t - s->pull_t0) / s->pull_seconds, 0, 1.0001f) : 1;
	if (s->pull_to >= 1) {
		if (s->rev == 2 && u < 1) {   // a put-back taken back: its clip backwards, to the hold
			r.clip = CLIP("putback");
			float d = hands_clip_seconds(f, r.clip);
			r.t = clampf(s->rev_t * (1 - u), 0, d - 1e-3f);
			r.camera_k = ui_motion_ease((1 - r.t / d) / .35f);
			return r;
		}
		float pick = s->pull_seconds > 0 ? (s->t - s->pull_t0) / s->pull_seconds : 1;
		if (pick < 1 && s->rev != 2) {   // take out, in the shelf's own time
			r.clip = CLIP("pick");
			r.t = clampf(pick, 0, 1) * (hands_clip_seconds(f, r.clip) - 1e-3f);
			r.camera_k = ui_motion_ease(pick / .35f);
			return r;
		}
		r.camera_k = 1;
		// L / R turn the box round and round (shelf_input.h): the hand's grip at each side is the front's, the spine's,
		// the back's, and for the open edge the spine's again with the case half round in it. The clip moves the
		// hand between grips; the case's turn is set here, so it always goes on the way pressed (hands_case_yaw).
		float k = (s->t - s->turn_t0) / UI_MOTION_TURN_S;
		bool moving = s->side != s->turn_from && k < 1;
		if (moving || s->side) {
			static const char *const turn[3][3] = { { NULL, "turn_r1", NULL }, { "turn_l2", NULL, "turn_r2" }, { NULL, "turn_l1", NULL } };
			int pf = HANDS_GRIP(s->turn_from), pt = HANDS_GRIP(s->side);
			const char *n = turn[pf][pt];
			r.clip = CLIP(n ? n : grip_clip[pt]);   // half round (the back to the front): the hand to its new grip
			if (r.clip) {
				float d = hands_clip_seconds(f, r.clip);
				r.t = n && moving ? ui_motion_ease(k) * (d - 1e-3f) : pt ? d - 1e-3f : 0;
				if (moving) {
					float a = grip_yaw(f, box, mitten, s->turn_from), b = grip_yaw(f, box, mitten, s->side), want = -s->turn_steps * (float)M_PI_2;
					float span = b - a;   // the way pressed: R turns the case's yaw down, L up, about steps quarters
					while (span > want + (float)M_PI) span -= 2 * (float)M_PI;
					while (span < want - (float)M_PI) span += 2 * (float)M_PI;
					r.yaw_mode = HANDS_YAW_SET;
					r.yaw = a + span * ui_motion_ease(k);
				} else if (s->side == 3) {
					r.yaw_mode = HANDS_YAW_ADD;
					r.yaw = (float)M_PI;
				}
				if (s->side || moving) return r;
			}
		}
		r.yaw_mode = HANDS_YAW_NONE;
		r.clip = CLIP("hold");   // the box resting in the hand
		r.t = 0;   // a resting case does not breathe or twitch
		return r;
	}
	if (s->pull > .001f && s->rev == 1 && u < 1) {   // a take-out stopped half way: its clip backwards
		r.clip = CLIP("pick");
		float d = hands_clip_seconds(f, r.clip);
		r.t = clampf(s->rev_t * (1 - u), 0, d - 1e-3f);
		r.camera_k = ui_motion_ease((r.t / d) / .35f);
		return r;
	}
	if (s->pull > .001f) {   // put back
		float back = s->pull_seconds > 0 ? (s->t - s->pull_t0) / s->pull_seconds : 1;
		r.clip = CLIP("putback");
		r.t = clampf(back, 0, 1) * (hands_clip_seconds(f, r.clip) - 1e-3f);
		r.camera_k = ui_motion_ease((1 - back) / .35f);
		float since = s->t - s->pull_t0;
		if (s->side == 3 && since < HANDS_BLEND) {   // from the open edge: the half turn the spine's grip lacks, gone with the blend
			r.yaw_mode = HANDS_YAW_ADD;
			r.yaw = (float)M_PI * (1 - clampf(since / HANDS_BLEND, 0, 1));
		}
	}
#undef CLIP
	return r;
}
