// Host checks for the hands (app/ui/hands.c) against the shipped hands.bbh:
// tables, every clip sampled, forward kinematics, smooth normals, the shelf's clips.
#include "hands.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void *slurp(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	void *p = aligned_alloc(16, (size_t)(n + 15) & ~(size_t)15);
	if (p && fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); p = NULL; }
	fclose(f);
	*size = (size_t)n;
	return p;
}

static float det3(const float m[12])
{
	return m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
}

static void apply(const float m[12], const float p[3], float o[3])
{
	for (int i = 0; i < 3; i++) o[i] = m[i * 4] * p[0] + m[i * 4 + 1] * p[1] + m[i * 4 + 2] * p[2] + m[i * 4 + 3];
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "assets/romfs/room/hands.bbh";
	size_t size;
	void *data = slurp(path, &size);
	if (!data) { printf("FAIL: cannot read %s\n", path); return 1; }
	hands_file f;
	char err[64];
	CHECK(hands_parse(&f, data, size, err, sizeof(err)), "parse: %s", err);
	if (fails) return 1;
    // Default mode must preserve the exact authored camera/prop poses, including
    // fractional frames, while containing no hands, joints, mesh or textures.
    size_t prop_size;
    void *prop_data = slurp("assets/romfs/room/case-motion.bbh", &prop_size);
    hands_file props;
    CHECK(prop_data && hands_parse(&props, prop_data, prop_size, err, sizeof(err)), "compact tracks: %s", err);
    if (!prop_data || fails) return 1;
    CHECK(prop_size < size / 4 && props.ntex == 0 && props.nclip == f.nclip, "compact asset size/tables");
    CHECK(!hands_has_sleeve(&props), "the compact tracks have no sleeve, yet hands_has_sleeve says so");
    for (unsigned i = 0; i < f.nclip; i++) {
        const hands_clip *c = &f.clip[i], *p = &props.clip[i];
        CHECK(!(p->mask & 3) && p->frames == c->frames && !strcmp(c->name, p->name), "compact clip metadata");
        for (float t = 0; t < hands_clip_seconds(&f, c); t += .017f) {
            hands_frame a, b;
            hands_sample(&f, c, t, &a); hands_sample(&props, p, t, &b);
            CHECK(!b.shown[0] && !b.shown[1] && a.objects == b.objects && a.fade == b.fade, "compact visibility/fade");
            CHECK(!memcmp(a.cam, b.cam, sizeof(a.cam)) && !memcmp(a.obj, b.obj, sizeof(a.obj)), "compact camera/prop mismatch %s at %.3f", c->name, t);
        }
    }
    hands_free(&props);
    free(prop_data);
	// Adapter dimensions survive exact keys, fractional keys and clip blends.
    // Native meshes are 14x125x142 (case) and 27x137x120 (carton).
    // Both shipping adapters have height 125: the held cover keeps that height.
    for (int kind = HANDS_BOX_CASE; kind <= HANDS_BOX_CASE_MULTI; kind++) {
        const hands_clip *c = hands_clip_for(&f, "hold", kind, false);
        const float mesh_case[3] = {14,125,142}, mesh_carton[3] = {27,137,120};
        const float *mesh = kind == HANDS_BOX_CARTON ? mesh_carton : mesh_case;
        for (int i = 0; i < 240; i++) {
            hands_frame fr;
            hands_sample(&f, c, i / 120.0f, &fr);
            for (int col = 0; col < 3; col++) {
                float *m = fr.obj[HANDS_CASE];
                float scale = sqrtf(m[col]*m[col]+m[col+4]*m[col+4]+m[col+8]*m[col+8]);
                CHECK(fabsf(scale * mesh[col] - c->thw[col]) < .08f,
                    "held dimensions %s t %.3f axis %d: %.4f expected %.4f",
                    c->name, i/120.0f, col, scale*mesh[col], c->thw[col]);
            }
        }
    }
	CHECK(!hands_parse(&(hands_file){0}, data, size / 3, err, sizeof(err)), "a cut file parsed");
	unsigned first, count;
	CHECK(hands_part(&f, "hand", &first, &count) && hands_part(&f, "hand_sleeve", &first, &count) && hands_part(&f, "hand_mitt", &first, &count), "parts");
	CHECK(f.ntex == 1 && f.tex[0].w && f.tex[0].h, "one sleeve texture, the room's (%u)", f.ntex);
	// review N5: the room's hands refuse a file without its sleeve (room_hands.c checks hands_has_sleeve before tex[0])
	CHECK(hands_has_sleeve(&f), "the hands file's sleeve");
	{ hands_file none = f; none.ntex = 0; CHECK(!hands_has_sleeve(&none), "a hands file without textures has a sleeve"); }
	if (f.version >= 3) CHECK(f.tex[0].w >= 256 && f.tex[0].w == 2 * f.tex[0].h && f.glove[1][0] + f.glove[1][1] > 0 && f.glove[0][0] == 0, "v3 texture and glove table");
	// every clip: every frame decodes, the shown hands' palms are rotations (det +-1)
	int frames = 0;
	for (unsigned i = 0; i < f.nclip; i++) {
		const hands_clip *c = &f.clip[i];
		for (float t = 0; t < hands_clip_seconds(&f, c); t += .05f) {
			hands_frame fr;
			hands_sample(&f, c, t, &fr);
			for (int h = 0; h < 2; h++) {
				if (!fr.shown[h]) continue;
				float d = det3(fr.palm[h]);
				if (fabsf(fabsf(d) - 1) > .02f) { CHECK(0, "%s t %.2f: palm det %.3f", c->name, t, d); break; }
				float M[HANDS_MAX_BONES][12];
				hands_pose(&f, &fr, h, c->flags & 1, M);
				for (unsigned k = 0; k < f.nbone; k++) {
					float dk = det3(M[k]);
					if (fabsf(fabsf(dk) - 1) > .03f) { CHECK(0, "%s: bone %u det %.3f", c->name, k, dk); break; }
				}
				// the fingertips stay near the palm (no exploded chain)
				float tip[3], palm[3] = { fr.palm[h][3], fr.palm[h][7], fr.palm[h][11] };
				apply(M[16], f.bone[16].head, tip);
				float dist = sqrtf((tip[0] - palm[0]) * (tip[0] - palm[0]) + (tip[1] - palm[1]) * (tip[1] - palm[1]) + (tip[2] - palm[2]) * (tip[2] - palm[2]));
				if (dist > 250) { CHECK(0, "%s: pinky %.0f mm from the palm", c->name, dist); break; }
			}
			frames++;
		}
		CHECK(hands_clip_named(&f, c->name, false) == c || (c->flags & 1), "lookup %s", c->name);
	}
	CHECK(hands_clip_named(&f, "pick_ps1", true) && (hands_clip_named(&f, "pick_ps1", true)->flags & 1), "mitten twin");
	// round 11: one hand, the right: no left hand in the take-out, the put-back, the hold or a turn (only the game start has it)
	for (unsigned i = 0; i < f.nclip; i++) {
		const hands_clip *c = &f.clip[i];
		if (strncmp(c->name, "pick_", 5) && strncmp(c->name, "putback", 7) && strncmp(c->name, "hold", 4) && strncmp(c->name, "turn", 4)) continue;
		int left = 0, right = 0;
		for (int k = 0; k < c->frames; k++) {
			hands_frame fr;
			hands_sample(&f, c, k / (float)f.fps, &fr);
			left += fr.shown[1];
			right += fr.shown[0];
		}
		CHECK(!left && right, "%s: left hand in %d frames, right in %d", c->name, left, right);
	}
	// normals: unit length
	int8_t (*n)[4] = malloc(f.nvert * 4);
	hands_normals(&f, n);
	int bad = 0;
	for (unsigned i = 0; i < f.nvert; i++) {
		float l = sqrtf((float)(n[i][0] * n[i][0] + n[i][1] * n[i][1] + n[i][2] * n[i][2])) / 127;
		if (fabsf(l - 1) > .03f) bad++;
	}
	CHECK(!bad, "%d normals not unit", bad);
	free(n);
	// the shelf: take out -> hold (loop) -> turn -> put back -> nothing; Play -> open
	hands_shelf_state s = { .t = .3f, .pull = .3f, .pull_to = 1, .pull_t0 = 0, .pull_seconds = 1.05f, .play_t0 = -1 };
	hands_shelf_clip c = hands_for_shelf(&f, HANDS_BOX_CASE, false, &s);
	CHECK(c.clip && !strcmp(c.clip->name, "pick_ps1") && c.camera_k > 0 && c.camera_k < 1, "take out: %s", c.clip ? c.clip->name : "-");
	s.t = 5;
	c = hands_for_shelf(&f, HANDS_BOX_CASE, false, &s);
	CHECK(c.clip && !strcmp(c.clip->name, "hold_ps1") && c.camera_k == 1, "hold: %s", c.clip ? c.clip->name : "-");
	s.side = 1; s.turn_from = 0; s.turn_t0 = 4.9f;
	c = hands_for_shelf(&f, HANDS_BOX_CARTON, true, &s);
	CHECK(c.clip && !strcmp(c.clip->name, "turn_r1_gba_m"), "turn: %s", c.clip ? c.clip->name : "-");
	s.side = 0; s.turn_from = 1; s.turn_t0 = 4.9f; s.t = 6.3f;   // after the 1.13 s turn
	c = hands_for_shelf(&f, HANDS_BOX_CASE, false, &s);
	CHECK(c.clip && !strcmp(c.clip->name, "hold_ps1"), "turned back: %s", c.clip ? c.clip->name : "-");
	s = (hands_shelf_state){ .t = 7.2f, .pull = .6f, .pull_to = 0, .pull_t0 = 7, .pull_seconds = .7f, .play_t0 = -1 };
	c = hands_for_shelf(&f, HANDS_BOX_CASE, false, &s);
	CHECK(c.clip && !strcmp(c.clip->name, "putback_ps1"), "put back: %s", c.clip ? c.clip->name : "-");
	s.pull = 0;
	c = hands_for_shelf(&f, HANDS_BOX_CASE, false, &s);
	CHECK(!c.clip, "in the shelf: no hand");
	s = (hands_shelf_state){ .t = 9, .pull = 1, .pull_to = 1, .pull_t0 = 0, .pull_seconds = 1, .play_t0 = 8.5f };
	c = hands_for_shelf(&f, HANDS_BOX_CARTON, false, &s);
	CHECK(c.clip && !strcmp(c.clip->name, "open_gba"), "game start: %s", c.clip ? c.clip->name : "-");
	// clips holding the case: a gripping index rests on it, never stands up over the top
	// for more than a 2-frame re-grip (hands_rest_index; the UI mock's 3e grips had it 21 mm up)
	for (unsigned ci = 0; ci < f.nclip; ci++) {
		const hands_clip *oc = &f.clip[ci];
		if (oc->space != HANDS_SPACE_SLOT || (oc->flags & 1)) continue;   // mittens: the index is not drawn
		int run = 0, worst_run = 0;
		for (int i = 0; i < oc->frames; i++) {
			hands_frame fr;
			hands_sample(&f, oc, i / (float)f.fps, &fr);
			bool up = false;
			for (int h = 0; h < 2 && (fr.objects & 1); h++) {
				if (!fr.shown[h]) continue;
				float M[HANDS_MAX_BONES][12], tw[3], tip[3], b[3];
				hands_pose(&f, &fr, h, oc->flags & 1, M);
				for (int k = 0; k < 3; k++) b[k] = 2 * f.bone[7].head[k] - f.bone[6].head[k];
				apply(M[7], b, tw);
				float *cm = fr.obj[0], r[3] = { tw[0] - cm[3], tw[1] - cm[7], tw[2] - cm[11] };
				for (int k = 0; k < 3; k++) tip[k] = cm[k] * r[0] + cm[4 + k] * r[1] + cm[8 + k] * r[2];
				if (fabsf(tip[0]) <= oc->thw[0] / 2 + 25 && fabsf(tip[2]) <= oc->thw[2] / 2 + 25 && tip[1] - oc->thw[1] > 12) up = true;
			}
			run = up ? run + 1 : 0;
			if (run > worst_run) worst_run = run;
		}
		CHECK(worst_run <= 2, "%s: index up over the case for %d frames", oc->name, worst_run);
	}
	// round 8 loops (hold): one period is frames - 1 keys (the last key is the first), and the
	// curve runs on across the seam: no stall, no jump
	{
		const hands_clip *hc = hands_clip_named(&f, "hold_ps1", false);
		CHECK(hc && hc->loop, "hold_ps1 is a loop");
		if (hc && hc->loop) {
			float P = hands_loop_seconds(&f, hc), worst = 0;
			CHECK(fabsf(P - (hc->frames - 1) / (float)f.fps) < 1e-4f, "loop period %.3f s", P);
			hands_frame a, b;
			for (float t = P - .05f; t < P + .05f; t += 1 / 240.0f) {
				hands_sample(&f, hc, t, &a);
				hands_sample(&f, hc, t + 1 / 240.0f, &b);
				for (int h = 0; h < 2; h++)
					if (a.shown[h] && b.shown[h])
						for (int k = 0; k < 3; k++) worst = fmaxf(worst, fabsf(a.palm[h][k] - b.palm[h][k]));
			}
			CHECK(worst < 1, "hold seam: palm step %.2f mm per 1/240 s", worst);
		}
	}
	// prepared at load: the same poses as computed per frame
	{
		const hands_clip *oc = hands_clip_named(&f, "open_ps1", false);
		hands_frame a, b;
		hands_sample(&f, oc, 0.1f, &a);
		CHECK(hands_prepare(&f), "prepare");
		hands_sample(&f, oc, 0.1f + 1.0f / 30, &b);   // another keyframe pair (not cached)
		hands_sample(&f, oc, 0.1f, &b);
		float d = 0;
		for (int k = 0; k < 30; k++) d = fmaxf(d, fabsf(a.joint[0][k] - b.joint[0][k]));
		CHECK(d < 1e-4f, "prepared rest differs by %f", d);
		// ahead of time in small steps (a keyframe a call), the glove in use first
		int steps = 0;
		hands_prepare_mitten = true;
		while (!hands_prepare_step(&f) && steps < 100000) steps++;
		unsigned keys = 0, done = 0, clips_done = 0;
		for (unsigned i = 0; i < f.nclip; i++) { clips_done += f.rest_done[i]; keys += f.clip[i].frames; }
		for (unsigned k = 0; k < keys; k++) done += f.rest_key[k];
		CHECK(clips_done == f.nclip && done == keys && steps >= (int)keys - 1, "prepare steps: %u/%u clips, %u/%u keys in %d steps", clips_done, f.nclip, done, keys, steps);
		hands_free(&f);
	}
	// 60 Hz through clip changes: with the blend, no frame jumps far (palm and case, mm per frame)
	{
		static const char *const pairs[][2] = { { "pick", "hold" }, { "hold", "turn_r1" }, { "turn_r1", "putback" }, { "hold", "open" } };
		float worst_blend = 0, worst_snap = 0;
		for (int pi = 0; pi < 4; pi++) {
			const hands_clip *a = hands_clip_for(&f, pairs[pi][0], HANDS_BOX_CASE, false), *b = hands_clip_for(&f, pairs[pi][1], HANDS_BOX_CASE, false);
			if (!a || !b) continue;
			for (int blend = 0; blend < 2; blend++) {
				hands_track k = { 0 };
				float last[3] = { 0 }, worst = 0;
				bool seen = false;
				float at = hands_clip_seconds(&f, a) * .6f;   // switch mid-clip: an interruption
				for (int n = 0; n < 60; n++) {
					float now = n / 60.0f;
					bool second = now >= at;
					hands_track_set(&k, second ? b : a, second ? now - at : now, blend ? now : 0);
					if (!blend) k.since = -10;   // no blend: snap
					hands_frame fr;
					hands_track_sample(&f, &k, blend ? now : 0, &fr);
					if (!fr.shown[0]) { seen = false; continue; }
					float p[3] = { fr.palm[0][3], fr.palm[0][7], fr.palm[0][11] };
					if (seen && now > at - .04f && now < at + .3f) {   // around the change only (the clips' own motion aside)
						float d = sqrtf((p[0] - last[0]) * (p[0] - last[0]) + (p[1] - last[1]) * (p[1] - last[1]) + (p[2] - last[2]) * (p[2] - last[2]));
						if (d > worst) worst = d;
					}
					memcpy(last, p, sizeof(p));
					seen = true;
				}
				if (getenv("HANDS_VERBOSE")) printf("  %s -> %s %s: %.1f mm\n", pairs[pi][0], pairs[pi][1], blend ? "blend" : "snap", worst);
				if (blend) worst_blend = fmaxf(worst_blend, worst); else worst_snap = fmaxf(worst_snap, worst);
			}
		}
		printf("hands: worst palm step at 60 Hz through a clip change: %.1f mm blended, %.1f mm snapped\n", worst_blend, worst_snap);
		CHECK(worst_blend <= worst_snap + .01f && worst_blend < 40, "clip changes jump %.1f mm", worst_blend);
	}
	// round 8 box classes: by thickness; a PAL / multi-disc case without its own clips plays the jewel case's
	CHECK(hands_box_class(true, 23) == HANDS_BOX_CARTON && hands_box_class(false, 10.6f) == HANDS_BOX_CASE, "box class");
	CHECK(hands_box_class(false, 16.9f) == HANDS_BOX_CASE_PAL && hands_box_class(false, 25) == HANDS_BOX_CASE_MULTI, "box class");
	{
		static const char *const act[] = { "pick", "hold", "putback", "open", "turn_r1" };
		for (int cls = HANDS_BOX_CASE_PAL; cls <= HANDS_BOX_CASE_MULTI; cls++)
			for (int a = 0; a < 5; a++) {
				const hands_clip *c = hands_clip_for(&f, act[a], cls, false), *one = hands_clip_for(&f, act[a], HANDS_BOX_CASE, false);
				CHECK(c && (c->box == cls || c == one) && hands_box_is_case(c->box), "%s for case class %d", act[a], cls);
			}
	}
	// the shelf at the clips' own lengths, with keys mid-clip (shelf.c): B half way out
	// reverses, A hurries the rest to 1.5x, A during a put-back brings it back out. Every
	// 60 Hz frame of the case stays within the export's own 30 mm (plus the 1.5x speed-up).
	{
		const hands_clip *pk = hands_clip_for(&f, "pick", HANDS_BOX_CASE, false), *pb = hands_clip_for(&f, "putback", HANDS_BOX_CASE, false);
		float lp = hands_clip_seconds(&f, pk), lb = hands_clip_seconds(&f, pb), worst_all = 0;
		// round 11: the case flies into the hand, take-out and put-back are about 1.1 s (they were 2.7 s)
		CHECK(lp > .9f && lp < 1.4f && lb > .9f && lb < 1.4f, "clip lengths %.2f %.2f", lp, lb);
		static const char *const name[] = { "take out", "B half way", "A half way", "A in the put-back" };
		for (int sc = 0; sc < 4; sc++) {
			hands_shelf_state st = { .pull_to = 1, .pull_seconds = lp, .play_t0 = -1 };
			hands_track k = { 0 };
			float last[3] = { 0 }, worst = 0, at = lp * .5f;
			bool seen = false, done = false;
			if (sc == 3) st = (hands_shelf_state){ .pull_to = 0, .pull_seconds = lb, .play_t0 = -1 };
			for (int n = 0; n < 60 * 6; n++) {
				float now = n / 60.0f;
				st.t = now;
				if (!done && now >= at && sc) {   // the key
					float u = (now - st.pull_t0) / st.pull_seconds;
					if (sc == 1) st = (hands_shelf_state){ .t = now, .pull_to = 0, .pull_t0 = now, .pull_seconds = u * lp, .play_t0 = -1, .rev = 1, .rev_t = u * lp };
					if (sc == 2) { st.pull_seconds /= 1.5f; st.pull_t0 = now - u * st.pull_seconds; }
					if (sc == 3) st = (hands_shelf_state){ .t = now, .pull_to = 1, .pull_t0 = now, .pull_seconds = u * lb, .play_t0 = -1, .rev = 2, .rev_t = u * lb };
					done = true;
				}
				float u = (now - st.pull_t0) / st.pull_seconds;
				st.pull = st.pull_to ? fminf(u, 1) : 1 - fminf(u, 1);
				hands_shelf_clip c = hands_for_shelf(&f, HANDS_BOX_CASE, false, &st);
				hands_track_set(&k, c.clip, c.t, now);
				hands_frame fr;
				hands_track_sample(&f, &k, now, &fr);
				if (!k.c || !(fr.objects & 1)) { seen = false; continue; }
				float p[3] = { fr.obj[0][3], fr.obj[0][7], fr.obj[0][11] };
				if (seen) {
					float d = sqrtf((p[0] - last[0]) * (p[0] - last[0]) + (p[1] - last[1]) * (p[1] - last[1]) + (p[2] - last[2]) * (p[2] - last[2]));
					if (d > worst) worst = d;
				}
				memcpy(last, p, sizeof(p));
				seen = true;
			}
			printf("hands: %s: worst case step %.1f mm per 60 Hz frame\n", name[sc], worst);
			worst_all = fmaxf(worst_all, worst);
		}
		CHECK(worst_all < 30 * 1.5f + 1, "keys mid-clip jump %.1f mm", worst_all);
	}
	if (!fails) printf("hands PASS: %u clips, %d sampled frames, %u vertices, %u bones\n", f.nclip, frames, f.nvert, f.nbone);
	free(data);
	return fails ? 1 : 0;
}
