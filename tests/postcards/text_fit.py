#!/usr/bin/env python3
"""Postcard text sits on its card.

Runs the shipped drawing code (app/ui/components/postcards.inc: pc_card_draw, postcards_top, pc_hand,
pc_postmark, pc_stamp, pc_layout) against the recording renderer of tests/postcards/harness.py, which keeps citro2d's view matrix
(translate, rotate, scale, shear). Every text's drawn box (the conservative width model of
tests/ui_fit/strings.py; capitals, baseline and descenders from the measured font metrics in
ui_style.h) is mapped into its card's own frame and must lie inside the card with a margin:
- the board: all 12 slots and Continue, PS1 6x2 and GBA 4x3 boards, Save and Load, selected
  (lifted, untilted) and not (tilted), used and empty;
- the big card: "Slot N" / "Continue" with its baseline on the first ruled line, the postmark's
  date and time inside its inner ring (a two-digit day), the stamp's label inside its field,
  the footer inside the card with the same space above and below it.
"""
import sys
from harness import run

TEST = r'''
static int failures;
#define MARGIN 1.0f
static void inside(const char *what, const text_t *t, int frame, float x0, float y0, float x1, float y1) {
 float c[4][2]; corners(t, frame, c);
 for (int i = 0; i < 4; i++)
  if (c[i][0] < x0 - .01f || c[i][0] > x1 + .01f || c[i][1] < y0 - .01f || c[i][1] > y1 + .01f) {
   printf("FAIL %s: \"%s\" corner (%.1f, %.1f) outside (%.1f..%.1f, %.1f..%.1f)\n", what, t->s, c[i][0], c[i][1], x0, x1, y0, y1);
   failures++; return;
  }
}
static pc_view view(bool cont, bool saving, int sel, int used_mask) {
 pc_view v; memset(&v, 0, sizeof(v));
 v.has_continue = cont; v.saving = saving; v.sel = sel; v.stamp = cont ? "GBA" : "PS1";
 for (int i = 0; i < PC_CARDS; i++) {
  v.card[i].used = (used_mask >> i) & 1;
  v.card[i].saved = 1762905540u + (u32)i * 86400u * 19;   /* Nov 11 2025 23:59 and on: two-digit days */
  v.card[i].lift = i == sel ? 1.0f : 0.0f;
 }
 return v;
}
static void board(const char *name, pc_view *v) {
 char what[96];
 ntexts = nframes = 0; M = (C3D_Mtx){ 1, 0, 0, 1, 0, 0 };
 for (int i = 0; i < PC_CARDS; i++) {
  if (i >= PC_SLOTS && !v->has_continue) continue;
  int before = ntexts, frame = nframes;
  pc_card_draw(v, &(pc_palette){0}, i);
  assert(nframes == frame + 1);
  for (int k = before; k < ntexts; k++) {
   snprintf(what, sizeof(what), "%s card %d%s%s", name, i, i == v->sel ? " selected" : "", v->card[i].used ? " used" : " empty");
   if (strstr(texts[k].s, "...")) { printf("FAIL %s: \"%s\" was cut to fit\n", what, texts[k].s); failures++; }
   inside(what, &texts[k], frame, MARGIN, MARGIN, frame_w[frame] - MARGIN, frame_h[frame] - MARGIN);
  }
 }
}
static void top(const char *name, pc_view *v) {
 char what[96];
 ntexts = nframes = 0; M = (C3D_Mtx){ 1, 0, 0, 1, 0, 0 };
 postcards_top(v);
 assert(nframes == 1);
 bool slot_line = false;
 for (int k = 0; k < ntexts; k++) {
  const text_t *t = &texts[k];
  snprintf(what, sizeof(what), "%s big card", name);
  inside(what, t, 0, MARGIN, MARGIN, UI_PC_TOP_W - MARGIN, UI_PC_TOP_H - MARGIN);
  float c[4][2]; corners(t, 0, c);
  if (strstr(t->s, "...") && strncmp(t->s, "Saved ", 6) && strncmp(t->s, "Where you", 9)) { printf("FAIL %s: \"%s\" was cut to fit\n", what, t->s); failures++; }
  if (!strncmp(t->s, "Slot ", 5) || !strcmp(t->s, "Continue")) {   /* on the first ruled line, inside the rules */
   slot_line = true;
   float bx, by, lx, ly; apply(t->m, t->align == C2D_AlignCenter ? t->x : t->x, t->y + t->size * MODEL_BASELINE, &bx, &by); unapply(frames[0], bx, by, &lx, &ly);
   if (fabsf(ly - UI_PC_TOP_RULE_Y) > .6f) { printf("FAIL %s: \"%s\" baseline %.1f, first rule %d\n", what, t->s, ly, UI_PC_TOP_RULE_Y); failures++; }
   snprintf(what, sizeof(what), "%s slot line on the rules", name);
   inside(what, t, 0, UI_PC_TOP_TEXT_X, 0, UI_PC_TOP_TEXT_X + UI_PC_TOP_TEXT_W, UI_PC_TOP_H);
  } else if (strchr(t->s, ':') && strlen(t->s) == 5) {   /* the postmark's time, and its date line before it */
   for (int j = k - 1; j <= k; j++) {
    float cc[4][2]; corners(&texts[j], 0, cc);
    float ri = UI_PC_TOP_MARK_R - UI_PC_MARK_GAP - UI_PC_MARK_LINE / 2 - MARGIN;
    for (int i = 0; i < 4; i++) {
     float dx = cc[i][0] - UI_PC_TOP_MARK_X, dy = cc[i][1] - UI_PC_TOP_MARK_Y;
     if (dx * dx + dy * dy > ri * ri) { printf("FAIL %s: postmark \"%s\" corner (%.1f, %.1f) outside its ring (r %.1f)\n", what, texts[j].s, cc[i][0], cc[i][1], ri); failures++; break; }
    }
   }
  } else if (!strcmp(t->s, v->stamp) || !strcmp(t->s, "CONTINUE")) {   /* the stamp's label inside its field */
   snprintf(what, sizeof(what), "%s stamp label", name);
   inside(what, t, 0, UI_PC_TOP_STAMP_X + UI_PC_STAMP_INSET + MARGIN, UI_PC_TOP_STAMP_Y + UI_PC_STAMP_INSET + MARGIN,
    UI_PC_TOP_STAMP_X + UI_PC_TOP_STAMP_W - UI_PC_STAMP_INSET - MARGIN, UI_PC_TOP_STAMP_Y + UI_PC_TOP_STAMP_H - UI_PC_STAMP_INSET - MARGIN);
  } else if (!strncmp(t->s, "Saved ", 6) || !strncmp(t->s, "Where you left off", 18)) {   /* the footer: even space above and below */
   float top_gap = fminf(c[0][1], c[1][1]) - (UI_PC_TOP_PIC_Y + UI_PC_TOP_PIC_H + UI_PC_TOP_BORDER);
   float base_y = fmaxf(c[0][1], c[1][1]) * 0 + (t->y + t->size * MODEL_BASELINE);   /* the footer is not slanted: its own frame */
   float bottom_gap = UI_PC_TOP_H - base_y;
   if (fabsf(top_gap - bottom_gap) > 1.5f || top_gap < 2) { printf("FAIL %s footer: %.1f px above, %.1f below\n", name, top_gap, bottom_gap); failures++; }
   float left = fminf(c[0][0], c[2][0]), right_room = UI_PC_TOP_W - (UI_PC_TOP_PIC_X + UI_PC_TOP_FOOT_W);
   if (fabsf(left - UI_PC_TOP_PIC_X) > .6f || fabsf(right_room - UI_PC_TOP_PIC_X) > .6f) { printf("FAIL %s footer: left %.1f, right room %.1f\n", name, left, right_room); failures++; }
  }
 }
 if (v->card[v->sel].used || v->sel != PC_CONTINUE) assert(slot_line);
}
int main(void) {
 int runs = 0;
 for (int cont = 0; cont < 2; cont++)
  for (int saving = 0; saving < 2; saving++)
   for (int used = 0; used < 3; used++) {   /* none, one, all used */
    int mask = used == 0 ? 0 : used == 1 ? 1 : 0x1fff;
    for (int sel = 0; sel < PC_CARDS; sel++) {
     if (sel == PC_CONTINUE && !cont) continue;
     char name[64]; snprintf(name, sizeof(name), "%s %s, %d used, card %d", cont ? "GBA 4x3" : "PS1 6x2", saving ? "save" : "load", used == 2 ? 12 : used, sel);
     pc_view v = view(cont, saving, sel, mask);
     board(name, &v); top(name, &v); runs++;
    }
   }
 if (failures) { printf("Postcard text FAIL: %d boxes off their card in %d views\n", failures, runs); return 1; }
 printf("Postcard text PASS: %d views (PS1 6x2 and GBA 4x3 with Continue, save and load, 0/1/12 used, every card selected): captions inside their cards, slot line on its rule, postmark inside its ring, stamp label in its field, footer centred\n", runs);
 return 0;
}
'''

rc, out, err = run(TEST)
lines = out.strip().splitlines()
fails = [l for l in lines if l.startswith('FAIL')]
kinds = {}
for l in fails:
    k = ('postmark' if 'postmark' in l else 'stamp label' if 'stamp label' in l else 'footer' if 'footer' in l else
         'slot line baseline' if 'baseline' in l else 'slot line width' if 'on the rules' in l else
         'Continue caption' if '"Continue"' in l else 'board caption' if ' card ' in l else 'big card')
    kinds.setdefault(k, []).append(l)
for k, v in sorted(kinds.items()):
    print(f'{k}: {len(v)} failures, e.g. {v[0]}')
print(lines[-1] if lines else err)
sys.exit(rc)
