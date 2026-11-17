#include "ui_trace_postcards.h"
// Postcard slot browser: shared view state (postcards.h). Drawing lives in
// components/postcards.inc. GPL-2.0-or-later.
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "postcards.h"
#include "postcard_palette.h"
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include "theme.h"
#include "ui_style.h"
#include "platform_video.h"
#include "app_input.h"
#include "ui_motion.h"
#include "uisound.h"
#include "shelf.h"

#define CARD_TEX 128         // a card's picture: at most 128 x 128 texels
#define BIG_TEX 256          // the top screen's picture

// RGB565 rows into a square tiled texture (8x8 tiles, Morton order inside),
// every step-th pixel of every step-th row; returns the texels used.
static void upload(C3D_Tex *t, unsigned tex, const u16 *px, unsigned w, unsigned h, unsigned step, float *tw, float *th)
{
	u16 *dst = t->data;
	unsigned ow = w / step, oh = h / step;
	if (ow > tex) ow = tex;
	if (oh > tex) oh = tex;
	memset(dst, 0, t->size);
	for (unsigned y = 0; y < oh; y++)
		for (unsigned x = 0; x < ow; x++) {
			u32 i = (((y >> 3) * (tex >> 3) + (x >> 3)) << 6) |
				(x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
			dst[i] = px[y * step * w + x * step];
		}
	C3D_TexFlush(t);
	C3D_TexSetFilter(t, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(t, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	*tw = (float)ow;
	*th = (float)oh;
}

// textures may still be read by the frame in flight
static void gpu_idle(void)
{
	if (gspHasGpuRight())
		video_gpu_idle();
}

void pc_card_clear(pc_card *c)
{
	if (c->tex == &c->own)
		gpu_idle();
	if (c->tex == &c->own)
		C3D_TexDelete(&c->own);
	if (c->full)
		linearFree(c->full);
	float lift = c->lift, v = c->lift_v;
	memset(c, 0, sizeof(*c));
	c->lift = lift;
	c->lift_v = v;
}

bool pc_picture(pc_card *c, const u16 *px, unsigned w, unsigned h)
{
	if (!px || !w || !h || w > BIG_TEX || h > BIG_TEX)
		return false;
	if (c->tex == &c->own) {
		gpu_idle();
		C3D_TexDelete(&c->own);
	}
	c->tex = NULL;
	unsigned step = (w + CARD_TEX - 1) / CARD_TEX;
	if (step < 1) step = 1;
	if (!C3D_TexInit(&c->own, CARD_TEX, CARD_TEX, GPU_RGB565))
		return false;
	upload(&c->own, CARD_TEX, px, w, h, step, &c->tw, &c->th);
	c->tex = &c->own;
	// the full picture for the top screen, if there is room; else the card's.
	// Linear memory: the board never fragments the game's heap.
	if (c->full)
		linearFree(c->full);
	c->full = linearAlloc((size_t)w * h * 2);
	if (c->full) {
		memcpy(c->full, px, (size_t)w * h * 2);
		c->fw = w;
		c->fh = h;
	}
	return true;
}

void pc_open(pc_view *v, bool saving, bool has_continue, const char *game, const char *stamp)
{
	memset(v, 0, sizeof(*v));
	v->saving = saving;
	v->has_continue = has_continue;
	v->game = game;
	v->stamp = stamp;
	v->big_of = -1;
	v->big_ok = C3D_TexInit(&v->big, BIG_TEX, BIG_TEX, GPU_RGB565);
}

void pc_close(pc_view *v)
{
	gpu_idle();
	for (int i = 0; i < PC_CARDS; i++)
		pc_card_clear(&v->card[i]);
	pc_card_clear(&v->now);
	if (v->big_ok)
		C3D_TexDelete(&v->big);
	v->big_ok = false;
}

void pc_notice(pc_view *v, const char *text)
{
	v->notice = text;
	v->notice_t = 2.5f;
	sfx_play(SFX_ERROR);   // every board notice says why a press could not be done
}

void pc_animate(pc_view *v, int anim, int card)
{
	if (anim == PC_ANIM_STAMP || anim == PC_ANIM_TOSS)
		sfx_play(anim == PC_ANIM_STAMP ? SFX_SAVE : SFX_TOSS);
	v->anim = anim;
	v->anim_card = card;
	v->anim_t = 0;
	v->anim_fresh = true;
}

bool pc_animating(const pc_view *v) { return v->anim != PC_ANIM_NONE; }

static bool selectable(const pc_view *v, int i)
{
	if (i == PC_CONTINUE)
		return v->has_continue && !v->saving;   // Continue writes itself when you leave
	return i >= 0 && i < PC_SLOTS;
}

static void pc_layout(const pc_view *v, int i, float *x, float *y, float *w, float *h, float *tilt); /* components/postcards.inc */

// the nearest card in a direction (dx, dy), favouring cards in line with it
static int neighbour(const pc_view *v, int from, float dx, float dy)
{
	float x0, y0, w, h, t, best = 1e9f;
	int pick = from;
	pc_layout(v, from, &x0, &y0, &w, &h, &t);
	for (int i = 0; i < PC_CARDS; i++) {
		if (i == from || !selectable(v, i))
			continue;
		float x, y;
		pc_layout(v, i, &x, &y, &w, &h, &t);
		float along = (x - x0) * dx + (y - y0) * dy, across = fabsf((x - x0) * dy - (y - y0) * dx);
		if (along <= 4)
			continue;
		float score = along + across * 2.5f;
		if (score < best) {
			best = score;
			pick = i;
		}
	}
	return pick;
}

bool pc_input(pc_view *v, u32 *keys)
{
	static u32 previous;
	static int ticks;
	u32 k = *keys, dirs = KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_CPAD_UP | KEY_CPAD_DOWN | KEY_CPAD_LEFT | KEY_CPAD_RIGHT;
	u32 held = input_keys_held() & dirs;
	if (held != previous || (k & held)) {
		previous = held;
		ticks = 0;
	} else if (held && ++ticks > UI_REPEAT_DELAY && (ticks - UI_REPEAT_DELAY) % UI_REPEAT_INTERVAL == 0)
		k |= held;
	int before = v->sel;
	if (k & (KEY_LEFT | KEY_CPAD_LEFT)) v->sel = neighbour(v, v->sel, -1, 0);
	else if (k & (KEY_RIGHT | KEY_CPAD_RIGHT)) v->sel = neighbour(v, v->sel, 1, 0);
	else if (k & (KEY_UP | KEY_CPAD_UP)) v->sel = neighbour(v, v->sel, 0, -1);
	else if (k & (KEY_DOWN | KEY_CPAD_DOWN)) v->sel = neighbour(v, v->sel, 0, 1);
	bool pick = (k & KEY_A) != 0;
	if (k & KEY_TOUCH) {
		touchPosition tp;
		hidTouchRead(&tp);
		for (int i = 0; i < PC_CARDS; i++) {
			float x, y, w, h, t;
			if (!selectable(v, i))
				continue;
			pc_layout(v, i, &x, &y, &w, &h, &t);
			if (fabsf(tp.px - x) <= w / 2 && fabsf(tp.py - y) <= h / 2) {
				if (i == v->sel)
					pick = true;   // tap the chosen card again to use it
				v->sel = i;
				break;
			}
		}
	}
	if (v->sel != before) {
		v->confirm_replace = v->confirm_delete = false;
		sfx_now(SFX_MOVE);
	}
	*keys = k;
	return pick;
}

// the top screen's picture: the chosen card's (or the paused game's) full size
static void big_update(pc_view *v)
{
	const pc_card *c = &v->card[v->sel];
	int want = v->sel;
	if (!c->used && v->saving && v->sel != PC_CONTINUE && v->now.tex) {
		c = &v->now;
		want = PC_CARDS;
	}
	if (!v->big_ok || want == v->big_of)
		return;
	v->big_of = -1;
	if (!c->full)
		return;
	gpu_idle();
	upload(&v->big, BIG_TEX, c->full, c->fw, c->fh, 1, &v->big_w, &v->big_h);
	v->big_of = want;
}

void pc_tick(pc_view *v, float dt)
{
	for (int i = 0; i < PC_CARDS; i++) {
		pc_card *c = &v->card[i];
		float target = i == v->sel ? 1.0f : 0.0f;
		ui_motion_settle(&c->lift, &c->lift_v, target, UI_MOTION_SETTLE_RATE, dt);
	}
	if (v->anim != PC_ANIM_NONE) {
		if (v->anim_fresh) v->anim_fresh = false;
		else v->anim_t += dt / UI_MOTION_TURN_S;
		if (v->anim_t >= 1.0f) {
			v->anim_t = 1.0f;
			v->anim = PC_ANIM_NONE;
		}
	}
	if (v->notice_t > 0)
		v->notice_t -= dt;
	v->t += dt;
	big_update(v);
}
#include "components/postcards.inc"
