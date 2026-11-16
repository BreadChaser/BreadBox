#!/usr/bin/env python3
"""Card titles stand in the header row on the red rule (owner 2026-11-11), and the place is called Collection.

1. Runs the shipped ui_card_header (app/ui/ui.c) with a recording text drawer for the pause/settings card and the
   room's cards (the arguments shelf_scene_draw.c's card_title passes): every title's cell lies inside the card above
   the first body line, its baseline on the red rule, its width inside the header row; the eyebrow too; a long
   title is fitted and cut to one line and takes the eyebrow's room; "Collection" is never shrunk or cut.
2. Source: both card kinds draw their titles only through ui_card_header (no second copy of the numbers), the
   room's body starts on the first ruled line, and no player-visible string still names the place "Library",
   "Your shelves" or "Shelves".
"""
from pathlib import Path
import os, re, subprocess, tempfile

root = Path(__file__).resolve().parents[2]
ui = (root/'app/ui/ui.c').read_text()
scene = (root/'app/ui/shelf_scene_draw.c').read_text()


def function(name, s):
    m = re.search(r'(?:PSX3D_COLD )?(?:static )?(?:void|u32|bool|float|int) ' + name + r'\([^;{}]*?\)\s*\{', s)
    assert m, name
    i = s.index('{', m.start()) + 1
    n = 1
    while n:
        n += (s[i] == '{') - (s[i] == '}')
        i += 1
    return s[m.start():i].replace('PSX3D_COLD ', '')


code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "3ds.h"
#include "ui_style.h"
/* The model font's letters stand this far down its 30 px cell (Nintendo-like, as measured from the hardware stills;
   ui_font.c measures the loaded font): the drawing must follow it, and the checks use it, not the UI's token. */
#define MODEL_BASELINE 25.0f
#define MODEL_TITLE_BASELINE 24.3f
float ui_face_baseline(int face) { return face == UI_FACE_TITLE ? MODEL_TITLE_BASELINE : MODEL_BASELINE; }
typedef void *C2D_Font;
enum { C2D_AlignRight = 4 };
typedef void (*ui_text_fn)(C2D_Font font, float x, float y, float size, u32 color, u32 flags, const char *s);
static int TITLE_FONT;
static C2D_Font libui_font(void) { return &TITLE_FONT; }
/* widths: tests/ui_fit/strings.py's conservative system-font model; the title font is taken 30% wider */
static float width(C2D_Font f, const char *s, float size) {
 float w = 0;
 for (; *s; s++) { char ch = *s; w += strchr("il.,:;'|!I ", ch) ? 6 : strchr("frtj()/-\"1", ch) ? 7 : strchr("MW", ch) ? 16 : strchr("ABCDEFGHJKLNOPQRSTUVXYZmw+", ch) ? 13 : 10; }
 return w * (size * 30) / 21 * 1.05f * (f ? 1.3f : 1);
}
static float ui_text_width(C2D_Font f, float size, const char *s) { return width(f, s, size); }
static float text_width(float size, const char *s) { return width(NULL, s, size); }
static char outs[8][256]; static int nout;
static const char *ui_fit_ex(C2D_Font f, const char *s, float size, float min, float w, float *got) {
 char *o = outs[nout++ % 8]; snprintf(o, 256, "%s", s);
 float k = size; while (k > min && width(f, o, k) > w) k -= .005f; if (k < min) k = min; *got = k;
 size_t n = strlen(o);
 while (n > 0 && width(f, o, k) > w) { o[--n] = 0; while (n && o[n - 1] == ' ') o[--n] = 0; if (n >= 3) { o[n - 3] = 0; n -= 3; } strcat(o, "..."); n = strlen(o); if (width(f, o, k) <= w) break; o[n - 3] = 0; n -= 3; }
 return o;
}
static void caps(char *out, size_t n, const char *s) { size_t i = 0; for (; s && s[i] && i + 1 < n; i++) out[i] = s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]; out[i] = 0; }
typedef struct { char s[256]; float x, y, size; u32 flags; bool title; } drawn_t;
static drawn_t d[4]; static int nd;
static void rec(C2D_Font f, float x, float y, float size, u32 c, u32 flags, const char *s) {
 (void)c; drawn_t *t = &d[nd++]; snprintf(t->s, sizeof(t->s), "%s", s); t->x = x; t->y = y; t->size = size; t->flags = flags; t->title = f != NULL;
}
''' + function('ui_card_header', ui) + r'''
static int failures;
static void check(const char *card, float top, float x, float right, float rule, float body, const char *title, const char *eyebrow, bool whole) {
 nd = 0;
 ui_card_header(rec, x, right, rule, title, eyebrow, 1, 2);
 for (int i = 0; i < nd; i++) {
  const drawn_t *t = &d[i];
  float base = t->y + t->size * (t->title ? MODEL_TITLE_BASELINE : MODEL_BASELINE), w = width(t->title ? libui_font() : NULL, t->s, t->size);
  float x0 = t->flags & C2D_AlignRight ? t->x - w : t->x, x1 = x0 + w, cell_bottom = t->y + t->size * 30;
  if (t->y < top || cell_bottom > body || base < rule - .01f || base > rule + .01f || x0 < x - .01f || x1 > right + .01f) {
   printf("FAIL %s: \"%s\" cell %.1f..%.1f (baseline %.1f, rule %.0f, body capitals from %.0f), x %.1f..%.1f in %.0f..%.0f\n", card, t->s, t->y, cell_bottom, base, rule, body, x0, x1, x, right);
   failures++;
  }
  if (t->title && whole && strcmp(t->s, title)) { printf("FAIL %s: \"%s\" became \"%s\"\n", card, title, t->s); failures++; }
  if (t->title && (t->size < UI_CARD_TITLE_MIN - .001f || t->size > UI_CARD_TITLE_SIZE + .001f)) { printf("FAIL %s: \"%s\" at %.2f\n", card, t->s, t->size); failures++; }
 }
 if (title && !nd) { printf("FAIL %s: \"%s\" not drawn\n", card, title); failures++; }
}
int main(void) {
 static const char *const titles[] = { "Collection", "Settings", "Share BreadBox", "Play together", "Quit BreadBox", "Nothing to continue",
  "Nothing here yet", "PlayStation", "Game Boy Advance", "Favorites", "Crash Bandicoot", "Kirby & The Amazing Mirror",
  "Castlevania: Symphony of the Night", "The Legend of Zelda: The Minish Cap (Europe) (En,Fr,De,Es,It)", "Final Fantasy Tactics Advance" };
 const float room_body = UI_ROOM_ROWS_Y - UI_PAPER_CAP, card_body = UI_CARD_ROWS_Y + UI_CARD_ROW - UI_PAPER_CAP;
 for (unsigned i = 0; i < sizeof(titles) / sizeof(*titles); i++) {
  bool whole = !strcmp(titles[i], "Collection");
  /* the room's place / Collection card and a shelf's game card (card coordinates, as card_title passes them) */
  check("room card", 0, UI_ROOM_TEXT_X, UI_ROOM_TEXT_R, UI_ROOM_RULE_Y, room_body, titles[i], NULL, whole);
  check("room card beside the picture", 0, UI_ROOM_TEXT_X, UI_ROOM_TEXT_X + UI_ROOM_TITLE_W_PICTURE, UI_ROOM_RULE_Y, room_body, titles[i], NULL, false);
  /* the pause / settings card, with and without its eyebrow */
  float right = UI_CARD_X + UI_CARD_W - UI_CARD_RIGHT;
  check("pause card", UI_CARD_Y, UI_CARD_TEXT_X, right, UI_CARD_RULE_Y, card_body, titles[i], NULL, whole);
  check("pause card with eyebrow", UI_CARD_Y, UI_CARD_TEXT_X, right, UI_CARD_RULE_Y, card_body, titles[i], "GBA", false);
 }
 if (failures) { printf("Card headers FAIL: %d\n", failures); return 1; }
 puts("Card headers PASS: every title (and eyebrow) on the red rule in the header row, inside the card, above the body's first line; long titles cut to one line; \"Collection\" whole at full size");
 return 0;
}
'''


def audit():
    failures = []
    # One header routine, one set of numbers: the pause card and the room's cards both call it, nothing else
    # draws a card title.
    if 'ui_card_header(ui_text_ex, UI_CARD_TEXT_X, right, UI_CARD_RULE_Y' not in function('ui_card_draw', ui):
        failures.append('ui_card_draw does not draw its header through ui_card_header')
    if 'ui_card_header(header_text, UI_ROOM_TEXT_X, UI_ROOM_TEXT_X + width, UI_ROOM_RULE_Y' not in function('card_title', scene):
        failures.append('shelf_scene_draw.c card_title does not use ui_card_header')
    for name in ['shelf_bottom', 'shelf_scene_card']:
        body = function(name, scene)
        if 'card_title(' not in body:
            failures.append(f'shelf_scene_draw.c {name}: no card_title')
        if re.search(r'UI_ROOM_TITLE_(?:Y|PX|STEP)|UI_ROOM_KICKER', body):
            failures.append(f'shelf_scene_draw.c {name}: a title drawn outside the header row')
    # The place's one name.
    for path in sorted([*(root/'app').rglob('*.c'), *(root/'app').rglob('*.inc'), *(root/'app').rglob('*.h'), *(root/'systems').rglob('*.c'),
                        *(root/'systems').rglob('*.inc'), *(root/'include').rglob('*.h')]):
        for n, line in enumerate(path.read_text().splitlines(), 1):
            if 'log_printf' in line or 'error_reply' in line or line.lstrip().startswith('//') or line.lstrip().startswith('*'):
                continue
            for lit in re.findall(r'"((?:\\.|[^"\\])*)"', line):
                if re.search(r'\bYour shelves\b|^Library$|^Shelves$|\bthe shelves\b|\bthe library\b|\bin the library\b|^Checking library|^Library (?:updated|checked)', lit):
                    failures.append(f'{path.relative_to(root)}:{n}: "{lit}" names the place; it is your Collection')
    return failures


with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
    p = Path(tmp)
    (p/'check.c').write_text(code)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                    '-I' + str(root/'tests/app/host'), '-I' + str(root/'include/breadbox'), str(p/'check.c'), '-lm', '-o', str(p/'check')], check=True)
    subprocess.run([str(p/'check')], check=True)
failures = audit()
if failures:
    raise SystemExit('Card header audit FAIL:\n' + '\n'.join(failures))
print('Card header audit PASS: one header routine for the pause and room cards; the room body starts on the first ruled line; the place is Collection everywhere')
