"""The postcard drawing code (app/ui/components/postcards.inc) under a recording renderer, for the host tests.

Keeps citro2d's view matrix (translate, rotate, scale, shear) so each drawn text and rectangle can be mapped into
its card's own frame; text widths come from the conservative width model of tests/ui_fit/strings.py (Azahar's
stand-in font + 5%: what fits here fits on the console). `build(test_c)` compiles the shipped postcards.inc
with the test's own C (main included) and returns the binary's path in a temporary directory."""
from pathlib import Path
import os, subprocess, tempfile

root = Path(__file__).resolve().parents[2]
pc_h = (root/'include/breadbox/postcards.h').read_text()
types = pc_h[pc_h.index('typedef struct {\n\tbool used;'):pc_h.index('} pc_view;') + len('} pc_view;')]
cont_types = pc_h[pc_h.index('typedef struct {\n\tC3D_Tex *tex;'):pc_h.index('} pc_continue;') + len('} pc_continue;')]

HARNESS = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "3ds.h"
#include "ui_style.h"
/* The model font's letters stand this far down its 30 px cell (Nintendo-like, as measured from the hardware stills;
   ui_font.c measures the loaded font): the drawing must follow it, and the checks use it, not the UI's token. */
#define MODEL_BASELINE 25.0f
#define MODEL_TITLE_BASELINE 24.3f
float ui_face_baseline(int face) { return face == UI_FACE_TITLE ? MODEL_TITLE_BASELINE : MODEL_BASELINE; }
#include "ui_trace_postcards.h"
#include "ui_motion.h"
#include "theme.h"
typedef struct { u16 width, height; void *data; u32 size; } C3D_Tex;
typedef struct { u16 width, height; float left, top, right, bottom; } Tex3DS_SubTexture;
typedef struct { C3D_Tex *tex; const Tex3DS_SubTexture *subtex; } C2D_Image;
typedef void *C2D_Font;
enum { C2D_AlignLeft = 0, C2D_AlignRight = 1, C2D_AlignCenter = 2 };
typedef struct { u32 key; const char *label; u8 icon; bool on; } ui_button_t;
#define UI_BAR_MAX 6
#define PC_SLOTS 12
#define PC_CONTINUE PC_SLOTS
#define PC_CARDS (PC_SLOTS + 1)
#define PC_BAR_MAX UI_BAR_MAX
@@TYPES@@
@@CONT_TYPES@@
/* the view: x' = a x + c y + e, y' = b x + d y + f */
typedef struct { float a, b, c, d, e, f; } C3D_Mtx;
static C3D_Mtx M = { 1, 0, 0, 1, 0, 0 };
static C3D_Mtx mul(C3D_Mtx p, C3D_Mtx q) { return (C3D_Mtx){ p.a*q.a + p.c*q.b, p.b*q.a + p.d*q.b, p.a*q.c + p.c*q.d, p.b*q.c + p.d*q.d, p.a*q.e + p.c*q.f + p.e, p.b*q.e + p.d*q.f + p.f }; }
static void C2D_ViewSave(C3D_Mtx *m) { *m = M; }
static void C2D_ViewRestore(const C3D_Mtx *m) { M = *m; }
static void C2D_ViewTranslate(float x, float y) { M = mul(M, (C3D_Mtx){ 1, 0, 0, 1, x, y }); }
static void C2D_ViewRotate(float r) { M = mul(M, (C3D_Mtx){ cosf(r), sinf(r), -sinf(r), cosf(r), 0, 0 }); }
static void C2D_ViewScale(float x, float y) { M = mul(M, (C3D_Mtx){ x, 0, 0, y, 0, 0 }); }
static void C2D_ViewShear(float x, float y) { M = mul(M, (C3D_Mtx){ 1, y, x, 1, 0, 0 }); }
static void apply(C3D_Mtx m, float x, float y, float *ox, float *oy) { *ox = m.a*x + m.c*y + m.e; *oy = m.b*x + m.d*y + m.f; }
static void unapply(C3D_Mtx m, float x, float y, float *ox, float *oy) {
 float det = m.a*m.d - m.b*m.c, X = x - m.e, Y = y - m.f;
 *ox = (m.d*X - m.c*Y) / det; *oy = (-m.b*X + m.a*Y) / det;
}
/* recording */
typedef struct { char s[64]; float x, y, size; u32 align; C3D_Mtx m; int frame; } text_t;
static text_t texts[256]; static int ntexts;
static C3D_Mtx frames[32]; static float frame_w[32], frame_h[32]; static int nframes;
typedef struct { float x, y, z, w, h; u32 c; C3D_Mtx m; } rect_t;
static rect_t rects[512]; static int nrects;
static void C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 c) {
 if (z == (float)UI_PC_Z_SHADOW) { frames[nframes] = M; frame_w[nframes] = w; frame_h[nframes] = h; nframes++; }   /* a card's shadow: its frame */
 if (nrects < 512) rects[nrects++] = (rect_t){ x, y, z, w, h, c, M };
}
static void C2D_DrawRectangle(float x, float y, float z, float w, float h, u32 a, u32 b, u32 c, u32 d) { (void)x; (void)y; (void)z; (void)w; (void)h; (void)a; (void)b; (void)c; (void)d; }
static void C2D_DrawCircleSolid(float x, float y, float z, float r, u32 c) { (void)x; (void)y; (void)z; (void)r; (void)c; }
static void C2D_DrawLine(float x, float y, u32 a, float x2, float y2, u32 b, float w, float z) { (void)x; (void)y; (void)a; (void)x2; (void)y2; (void)b; (void)w; (void)z; }
typedef struct { float x, y, w, h; C3D_Mtx m; } image_t;
static image_t images[8]; static int nimages;
static void C2D_DrawImageAt(C2D_Image i, float x, float y, float z, void *t, float sx, float sy) {
 (void)z; (void)t; if (nimages < 8) images[nimages++] = (image_t){ x, y, i.subtex->width * sx, i.subtex->height * sy, M };
}
static void ui_text_ex(C2D_Font f, float x, float y, float size, u32 color, u32 align, const char *s) {
 (void)f; (void)color; text_t *t = &texts[ntexts++];
 snprintf(t->s, sizeof(t->s), "%s", s); t->x = x; t->y = y; t->size = size; t->align = align; t->m = M; t->frame = nframes - 1;
}
/* the conservative width model of tests/ui_fit/strings.py (Azahar's stand-in font + 5%) */
static float width(const char *s, float size) {
 float w = 0;
 for (; *s; s++) {
  char ch = *s;
  w += strchr("il.,:;'|!I ", ch) ? 6 : strchr("frtj()/-\"1", ch) ? 7 : strchr("MW", ch) ? 16 : strchr("ABCDEFGHJKLNOPQRSTUVXYZmw+", ch) ? 13 : 10;
 }
 return w * (size * 30) / 21 * 1.05f;
}
static char fitted[16][96]; static int nfit;
static const char *ui_fit(const char *s, float size, float w) {
 char *o = fitted[nfit++ % 16]; snprintf(o, 96, "%s", s);
 size_t n = strlen(o);
 while (n && width(o, size) > w) { n--; snprintf(o + n, 96 - n, "..."); if (n) o[n] = 0, strcat(o, "..."); }
 return o;
}
static const char *ui_fit_ex(C2D_Font f, const char *s, float size, float min, float w, float *out) {
 (void)f; float k = size;
 while (k > min && width(s, k) > w) k -= .005f;
 if (k < min) k = min;
 *out = k; return ui_fit(s, k, w);
}
#include "ui_fit.h"
static float model_measure(const char *s, float size, void *ud) { (void)ud; return width(s, size); }
#define UI_FIT_LINES_MAX 4
static int ui_fit_lines(C2D_Font f, const char *s, float size, float min, float w, int max, ui_fit_line *out, float *got) {
 (void)f; return ui_fit_lines_bounded(s, size, min, w, max, model_measure, NULL, out, got);
}
static int desks;
static void shelf_scene_desk(bool load) { (void)load; desks++; }
static float ui_depth(float d) { (void)d; return 0; }
static void ui_bar_draw(const ui_button_t *b, int n) { (void)b; (void)n; }
static theme_room_t room = { .cork = 1 };
static theme_t th = { .room = &room };
const theme_t *theme(void) { return &th; }
u32 theme_alpha(u32 c, int a) { (void)a; return c; }
#include "postcard_palette.h"
#include "postcard_palette.c"
#include "components/postcards.inc"

/* text geometry from the measured font (ui_style.h): capitals stand UI_PAPER_CAP at the floor size */
#define CAP_PER_SIZE (UI_PAPER_CAP / UI_TEXT_FLOOR)
#define DESC_PER_SIZE 6.0f
static void box(const text_t *t, float *x0, float *y0, float *x1, float *y1) {
 float w = width(t->s, t->size), base = t->y + t->size * MODEL_BASELINE;
 *x0 = t->align == C2D_AlignCenter ? t->x - w / 2 : t->align == C2D_AlignRight ? t->x - w : t->x; *x1 = *x0 + w;
 *y0 = base - t->size * CAP_PER_SIZE;
 *y1 = strpbrk(t->s, "gjpqy,") ? base + t->size * DESC_PER_SIZE : base;
}
/* the text's four corners in its card's frame */
static void corners(const text_t *t, int frame, float out[4][2]) {
 float x0, y0, x1, y1; box(t, &x0, &y0, &x1, &y1);
 float pts[4][2] = { { x0, y0 }, { x1, y0 }, { x0, y1 }, { x1, y1 } };
 for (int i = 0; i < 4; i++) { float wx, wy; apply(t->m, pts[i][0], pts[i][1], &wx, &wy); unapply(frames[frame], wx, wy, &out[i][0], &out[i][1]); }
}
'''


def run(test_c):
    """Compile the harness + test_c with ASan/UBSan and run it: (returncode, stdout, stderr)."""
    code = HARNESS.replace('@@TYPES@@', types).replace('@@CONT_TYPES@@', cont_types) + test_c
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        p = Path(tmp)
        (p/'check.c').write_text(code)
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
                        '-fsanitize=address,undefined', '-I' + str(root/'tests/app/host'), '-I' + str(root/'app/ui'),
                        '-I' + str(root/'include/breadbox'), str(p/'check.c'), '-lm', '-o', str(p/'check')], check=True)
        r = subprocess.run([str(p/'check')], capture_output=True, text=True)
        return r.returncode, r.stdout, r.stderr
