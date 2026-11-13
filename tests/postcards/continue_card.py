#!/usr/bin/env python3
"""The Continue postcard (postcards.inc pc_continue_card) lays out inside the screen at real metrics.

Owner 2026-11-12: "needs to be angled a bit": the card lies turned by UI_PC_CC_TILT; all four corners stay on the screen
and above the bar.

Owner 2026-11-11: "the continue game menu bottom cuts the name off, lacks detail and looks bad"; mock A (the
postcard) picked. Runs the shipped drawing code against the recording renderer of tests/postcards/harness.py
(the conservative width model of tests/ui_fit/strings.py) for the four names of devtools continue-card NOTES.md,
a one-word name and an empty one, PS1 (4:3) and GBA (3:2, letterboxed in the room's 256x192 picture) and no
picture, the room ("Continue") and the question ("Continue?"), "Left off" and the red Start over notice, and
the blank card. Every view must keep:
- the card on the screen above the 40 px bar (its shadow may pass under the bar); the desk drawn under it;
- every text inside the paper with a margin, no name and no notice cut ("..."), at the text floor or larger;
- the name on its ruled lines (each baseline on a rule), balanced: the four names wrap as the mock's
  ("Kirby - Nightmare" / "in Dream Land"), never one lone last word;
- nothing overlapping: the picture's frame, the stamp, "Continue", the name, the foot line;
- the picture in the game's own shape (the letterbox cropped), the stamp's label inside its field.
"""
import sys
from harness import run

TEST = r'''
#include "ui_words.h"
#include "last_picture.h"
static int failures;
#define MARGIN 1.0f
#define FAIL(...) do { printf("FAIL " __VA_ARGS__); printf("\n"); failures++; } while (0)
typedef struct { float x0, y0, x1, y1; } box_t;
static box_t text_box(const text_t *t) {   /* in the card's frame (frame 0) */
 float c[4][2]; corners(t, 0, c);
 box_t b = { 1e9f, 1e9f, -1e9f, -1e9f };
 for (int i = 0; i < 4; i++) { b.x0 = fminf(b.x0, c[i][0]); b.y0 = fminf(b.y0, c[i][1]); b.x1 = fmaxf(b.x1, c[i][0]); b.y1 = fmaxf(b.y1, c[i][1]); }
 return b;
}
static bool overlap(box_t a, box_t b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }
static box_t rect_box(const rect_t *r) {   /* the card is only translated: its frame is the card's */
 float x, y; apply(r->m, r->x, r->y, &x, &y); unapply(frames[0], x, y, &x, &y);
 return (box_t){ x, y, x + r->w, y + r->h };
}
static C3D_Tex tex = { 256, 256, 0, 0 };
static const char *names[] = { "Crash Bandicoot", "Kirby - Nightmare in Dream Land",
 "The Legend of Zelda - A Link to the Past & Four Swords", "Tom Clancy's Splinter Cell - Pandora Tomorrow & Chaos Theory", "Tetris" };
/* the mock's balanced lines (NOTES.md "Fix round on A"), at the conservative font model */
static const char *const kirby[] = { "Kirby - Nightmare", "in Dream Land" };
static int views;
static void view(const char *what, const pc_continue *c, int want_lines, const char *const *want) {
 ntexts = nframes = nrects = nimages = desks = 0; M = (C3D_Mtx){ 1, 0, 0, 1, 0, 0 };
 pc_continue_card(c);
 views++;
 if (desks != 1) FAIL("%s: the desk drawn %d times", what, desks);
 if (nframes != 1) { FAIL("%s: %d card frames", what, nframes); return; }
 float w = frame_w[0], h = frame_h[0];   /* the card's own frame: its shadow is drawn in it */
 /* the card is turned (UI_PC_CC_TILT): every corner of the paper on the screen and above the bar, the shadow's
  * right corners on the screen too (it may go under the bar) */
 static const float at[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
 for (int i = 0; i < 4; i++) {
  float px, py, sx, sy; apply(frames[0], at[i][0] * w, at[i][1] * h, &px, &py);
  apply(frames[0], at[i][0] * w + UI_PC_SHADOW_X, at[i][1] * h + UI_PC_SHADOW_Y, &sx, &sy);
  if (px < 0 || py < 0 || px > UI_BOTTOM_SCREEN_WIDTH || py > BAR_TOP || sx > UI_BOTTOM_SCREEN_WIDTH)
   FAIL("%s: card corner %d at %.1f,%.1f (shadow %.1f) off the screen or under the bar (top %d)", what, i, px, py, sx, BAR_TOP);
 }
 { float x0, y0, x1, y1; apply(frames[0], 0, 0, &x0, &y0); apply(frames[0], w, 0, &x1, &y1);
   if (fabsf(atan2f(y1 - y0, x1 - x0) + UI_PC_CC_TILT) > 1e-4f) FAIL("%s: card not turned by UI_PC_CC_TILT (%.4f)", what, atan2f(y1 - y0, x1 - x0)); }
 box_t name_b[4], foot_b[2], title_b = { 0 }, label_b = { 0 }; int nn = 0, nf = 0; bool title = false, label = false;
 float first_base = 0;
 for (int k = 0; k < ntexts; k++) {
  const text_t *t = &texts[k];
  box_t b = text_box(t);
  if (b.x0 < MARGIN || b.y0 < MARGIN || b.x1 > w - MARGIN || b.y1 > h - MARGIN)
   FAIL("%s: \"%s\" (%.1f,%.1f..%.1f,%.1f) off the paper", what, t->s, b.x0, b.y0, b.x1, b.y1);
  if (t->size < UI_TEXT_FLOOR - 1e-4f) FAIL("%s: \"%s\" at %.2f, under the floor", what, t->s, t->size);
  float base = t->y + t->size * MODEL_BASELINE;
  if (!strncmp(t->s, "Continue", 8)) { title = true; title_b = b; if (strcmp(t->s, c->question ? "Continue?" : "Continue")) FAIL("%s: title \"%s\"", what, t->s); }
  else if (c->stamp && !strcmp(t->s, c->stamp)) { label = true; label_b = b; }
  else if (!strncmp(t->s, "Left off", 8) || (c->alert && strstr(c->alert, t->s))) {
   if (nf < 2) foot_b[nf++] = b;
  } else if (!c->empty) {
   if (nn < 4) name_b[nn++] = b;
   if (nn == 1) first_base = base;
   bool on_rule = false;   /* each name line stands on a ruled line */
   for (int r = 0; r < nrects; r++) if (rects[r].h == 1 && fabsf(rects[r].y - base) < .01f && rects[r].w >= UI_PC_CC_NAME_W) on_rule = true;
   if (!on_rule) FAIL("%s: name line \"%s\" baseline %.1f on no rule", what, t->s, base);
  }
  /* never cut; with the Start over notice (an error, two lines) a name that needs three gets two, cut */
  if (strstr(t->s, "...") && !(c->alert && nn == 2 && !strcmp(t->s, texts[k].s))) FAIL("%s: \"%s\" was cut", what, t->s);
 }
 if (c->empty) {
  if (ntexts != 1 || strcmp(texts[0].s, "Nothing to continue") || fabsf(texts[0].y + texts[0].size * MODEL_BASELINE - UI_PC_CC_EMPTY_LINE) > .01f)
   FAIL("%s: the blank card says only \"Nothing to continue\" on its middle line", what);
  if (nimages) FAIL("%s: a picture on the blank card", what);
  return;
 }
 if (!title) FAIL("%s: no title", what);
 if (want_lines && nn != want_lines) FAIL("%s: name on %d lines, want %d", what, nn, want_lines);
 for (int i = 0; want && i < nn; i++) {
  int k = 0; while (k < ntexts && !(texts[k].align == 0 && text_box(&texts[k]).y0 == name_b[i].y0)) k++;
  if (k < ntexts && strcmp(texts[k].s, want[i])) FAIL("%s: line %d \"%s\", want \"%s\"", what, i + 1, texts[k].s, want[i]);
 }
 if (nn >= 2) {   /* no lone last word: the last line holds more than one word, or the name is one word per line */
  int k = 0; while (k < ntexts && !(texts[k].align == 0 && text_box(&texts[k]).y0 == name_b[nn - 1].y0)) k++;
  if (k < ntexts && !strchr(texts[k].s, ' ')) FAIL("%s: a lone last word \"%s\"", what, texts[k].s);
 }
 if (c->alert ? nf < 1 : (c->saved && nf != 1)) FAIL("%s: %d foot lines", what, nf);
 /* the picture's frame, the stamp, the title, the name and the foot do not overlap */
 box_t frame = { 0 }, stamp = { 0 }; bool have_frame = false, have_stamp = false;
 for (int r = 0; r < nrects; r++) {
  if (rects[r].z == (float)UI_PC_Z_BORDER && rects[r].w > UI_PC_CC_FRAME * 4) { frame = rect_box(&rects[r]); have_frame = true; }
  if (rects[r].z == (float)UI_PC_Z_STAMP && !have_stamp) { stamp = rect_box(&rects[r]); have_stamp = true; }
 }
 if (!have_frame || !have_stamp) { FAIL("%s: no picture frame or stamp", what); return; }
 if (frame.x0 < MARGIN || frame.y0 < MARGIN || frame.x1 > w - MARGIN) FAIL("%s: picture frame off the paper", what);
 box_t all[8]; const char *what_k[8]; int na = 0;
 all[na] = frame; what_k[na++] = "picture"; all[na] = stamp; what_k[na++] = "stamp"; all[na] = title_b; what_k[na++] = "title";
 for (int i = 0; i < nn; i++) { all[na] = name_b[i]; what_k[na++] = "name"; }
 for (int i = 0; i < nf; i++) { all[na] = foot_b[i]; what_k[na++] = "foot"; }
 for (int i = 0; i < na; i++) for (int j = i + 1; j < na; j++)
  if (overlap(all[i], all[j])) FAIL("%s: %s (%.1f,%.1f..%.1f,%.1f) overlaps %s (%.1f,%.1f..%.1f,%.1f)", what, what_k[i], all[i].x0, all[i].y0, all[i].x1, all[i].y1,
   what_k[j], all[j].x0, all[j].y0, all[j].x1, all[j].y1);
 if (first_base && frame.y1 > first_base - UI_PAPER_CAP - 2) FAIL("%s: picture frame ends at %.1f, name capitals at %.1f", what, frame.y1, first_base - UI_PAPER_CAP);
 if (c->stamp && (!label || label_b.x0 < stamp.x0 + UI_PC_STAMP_INSET || label_b.x1 > stamp.x1 - UI_PC_STAMP_INSET || label_b.y1 > stamp.y1 - UI_PC_STAMP_INSET))
  FAIL("%s: stamp label outside its field", what);
 /* the picture in the game's own shape, inside its frame */
 if (c->tex) {
  if (nimages != 1) { FAIL("%s: %d pictures", what, nimages); return; }
  float want_aspect = c->tw / (c->th - 2 * c->band), got = images[0].w / images[0].h;
  if (fabsf(got - want_aspect) > .02f) FAIL("%s: picture %.0fx%.0f, aspect %.3f, want %.3f", what, images[0].w, images[0].h, got, want_aspect);
  if (images[0].x < frame.x0 + UI_PC_CC_FRAME - .5f || images[0].x + images[0].w > frame.x1 - UI_PC_CC_FRAME + .5f || images[0].y + images[0].h > frame.y1 - UI_PC_CC_FRAME + .5f)
   FAIL("%s: picture outside its frame", what);
 }
}
int main(void) {
 const u32 saved = 1762905540u;   /* Nov 11 2025, 23:59: a two-digit day */
 struct { const char *stamp; float tw, th, band; } sys[] = { { "PS1", 256, 192, 0 }, { "GBA", 256, 192, 11 }, { "GBA", 240, 160, 0 }, { "GBA", 0, 0, 0 } };
 for (unsigned s = 0; s < 4; s++)
  for (unsigned n = 0; n < 5; n++)
   for (int q = 0; q < 2; q++)
    for (int alert = 0; alert < 2; alert++) {
     if (alert && !q) continue;   /* the notice is the question's */
     char what[160];
     snprintf(what, sizeof(what), "%s %s%s, %s%s", sys[s].stamp, sys[s].tw ? "" : "no picture, ", names[n], q ? "question" : "room", alert ? ", Start over failed" : "");
     pc_continue c = { sys[s].tw ? &tex : NULL, sys[s].tw, sys[s].th, sys[s].band, names[n], sys[s].stamp, saved, alert ? UI_CONTINUE_OVER_FAILED : NULL, q, false };
     view(what, &c, n == 1 && !alert ? 2 : n == 0 || n == 4 ? 1 : 0, n == 1 && !alert ? kirby : NULL);
    }
 pc_continue blank = { 0 }; blank.empty = true;
 view("blank card", &blank, 0, NULL);
 pc_continue unknown = { &tex, 256, 192, 0, "Crash Bandicoot", "PS1", 0, NULL, false, false };   /* no time: no foot line */
 view("PS1 no time", &unknown, 1, NULL);
 pc_continue nameless = { &tex, 256, 192, 0, "", "PS1", saved, NULL, true, false };
 view("PS1 empty name", &nameless, 0, NULL);
 {   /* the letterbox a GBA last picture is saved in (bb_last_picture_save: 256x170 centred, 11 black rows each side) */
  static u16 px[BB_PICTURE_W * BB_PICTURE_H];
  for (int i = 0; i < BB_PICTURE_W * BB_PICTURE_H; i++) px[i] = (i / BB_PICTURE_W >= 11 && i / BB_PICTURE_W < BB_PICTURE_H - 11) ? 0x1234 : 0;
  if (bb_picture_band(px) != 11) FAIL("letterbox: %d rows, want 11", bb_picture_band(px));
  px[(BB_PICTURE_H - 6) * BB_PICTURE_W + 100] = 1;   /* the bottom run shorter: the shorter one */
  if (bb_picture_band(px) != 5) FAIL("letterbox uneven: %d rows, want 5", bb_picture_band(px));
  memset(px, 0, sizeof(px));   /* a black picture: at most a quarter, 2:1 */
  if (bb_picture_band(px) != BB_PICTURE_H / 4) FAIL("black picture: %d rows", bb_picture_band(px));
  for (int i = 0; i < BB_PICTURE_W * BB_PICTURE_H; i++) px[i] = 0x8410;
  if (bb_picture_band(px) != 0 || bb_picture_band(NULL) != 0) FAIL("a full picture is not cropped");
 }
 if (failures) { printf("Continue postcard FAIL: %d problems in %d views\n", failures, views); return 1; }
 printf("Continue postcard PASS: %d views (four names, one word, empty name; PS1, GBA letterboxed and not, no picture; room and question; Left off and the Start over notice; the blank card): on the screen above the bar, text inside the paper at the floor and never cut, name on its rules and balanced, nothing overlapping, the picture in its game's shape, the letterbox found\n", views);
 return 0;
}
'''

rc, out, err = run(TEST)
lines = out.strip().splitlines()
for l in [l for l in lines if l.startswith('FAIL')][:25]:
    print(l)
print(lines[-1] if lines else err)
sys.exit(rc)
