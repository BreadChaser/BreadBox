#!/usr/bin/env python3
"""Menu sounds: one per action, none for a dead press, none when off.

Executes the shipped sound queue (app/ui/uisound.c), the shared list/card input,
the input gate, screen entry, the pause card open/close and the two-way question
(app/ui/ui.c) with a recording player, at 30 and 60 Hz. Then a source audit:
no screen plays its own move sound after the shared list/card input (that would
double it), and every event in the enum has a file in the table.
"""
from pathlib import Path
import os, re, subprocess, tempfile

root = Path(__file__).resolve().parents[2]
ui = (root/'app/ui/ui.c').read_text()
snd = (root/'app/ui/uisound.c').read_text()
header = (root/'include/breadbox/uisound.h').read_text()


def function(name, s):
    m = re.search(r'(?:PSX3D_COLD )?(?:static )?(?:void|u32|bool|float|int) ' + name + r'\([^;{}]*?\)\s*\{', s)
    assert m, name
    i = s.index('{', m.start()) + 1
    n = 1
    while n:
        n += (s[i] == '{') - (s[i] == '}')
        i += 1
    return s[m.start():i].replace('PSX3D_COLD ', '')


enum = header[header.index('enum {'):header.index('};', header.index('enum {')) + 2]
table = snd[snd.index('// The sound files'):snd.index('static clip_t clips')]
queue = snd[snd.index('// Plays now:'):snd.index('// start (fading in)')]
code = r'''
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "3ds.h"
#include "ui_trace.h"
#include "ui_motion.h"
#include "ui_style.h"
static void log_printf(const char *f, ...) { (void)f; }
#define SYSCLOCK_ARM11 1000000ULL
#define UI_SWAP_REMOTE (1u<<23)
#define UI_SWAP_MENU 0u
''' + enum + r'''
static bool ok = true, sw_sfxlog, closing, alive = true;
static struct { bool sounds; } g_cfg = { true };
static u64 tick;
static int played[64], nplayed, files[64];
static u8 sfx_turn[SFX_COUNT];
static bool aptShouldClose(void) { return closing; }
bool aptMainLoop(void) { return alive; }
static bool gspHasGpuRight(void) { return true; }
static u64 svcGetSystemTick(void) { return tick; }
''' + table + r'''
static int last_event;
static void play_file(int file) { files[nplayed] = file; played[nplayed++] = last_event; }
''' + queue.replace('\tplay_file(file);', '\tlast_event = event;\n\tplay_file(file);') + r'''
typedef struct { bool init; float hl, hl_v, scroll, scroll_v, scroll_target, lift[8]; u32 motion_tick; } flist_t;
static u32 held, touch_y;
static u32 input_keys_held(void) { return held; }
void hidTouchRead(touchPosition *p) { p->px = 100; p->py = (u16)touch_y; }
void ui_card_touch(touchPosition *p) { hidTouchRead(p); }   /* a straight card: the tilt has its own test (tests/card_tilt) */
static int flist_row_at(const flist_t *f, float y0, float h, float py) { (void)f; return (int)((py - y0) / h); }
static u32 ui_resume_pending, swap_keys, swap_buffer, swap_token, swap_buffer_token, swap_buffer_tick, swap_enter_tick;
static u64 pause_open_tick, pause_closed_tick;
static void ui_pause_motion(float p) { (void)p; }
''' + '\n'.join(function(n, ui) for n in ['motion_now', 'list_keys', 'ui_list_input', 'ui_card_input', 'swap_sound', 'ui_swap_keys',
                                          'ui_swap_confirm_keys', 'ui_swap_enter', 'ui_swap_hold', 'ui_pause_open', 'ui_pause_close']) + r'''
/* the two-way question, as shipped */
typedef struct { u32 key; const char *label; u8 icon; bool on; } ui_button_t;
enum { UI_ICON_EXIT, UI_ICON_CROSS, UI_ICON_CHECK };
#define UI_BAR_MAX 6
typedef struct { const char *title, *message; ui_button_t tab[2]; int lit; } pick_t;
static int ui_action_icon(const char *s, int d) { (void)s; return d; }
static u32 script[64]; static int frame, script_n;
static void input_poll(void) {}
static u32 input_keys_down(void) { return frame < script_n ? script[frame] : 0; }
static void dev_tick_keys(void) {}
static void ui_begin(void) {}
static void ui_menu_top(void *p) { (void)p; }
static void pick_draw(void *p) { (void)p; }
static int hz = 60;
static void video_present_vsync(bool g, void (*t)(void *), void (*b)(void *), void *p) { (void)g; (void)t; (void)b; (void)p; frame++; tick += 1000000 / hz; held = 0; if (frame > 40) alive = false; }
static int ui_tabs_hit(int n, u32 keys) { (void)n; (void)keys; return -1; }
''' + function('ui_confirm_pick_update', ui) + r'''
/* the question with up to UI_BAR_MAX answers (the GBA "This save looks different"), as shipped */
typedef struct { const char *title, *message; ui_button_t tab[UI_BAR_MAX]; int n, lit; } pick_n_t;
static void pick_n_draw(void *p) { (void)p; }
''' + function('ui_confirm_pick_n', ui) + r'''
static void reset(void) { nplayed = 0; frame = 0; script_n = 0; alive = true; closing = false; g_cfg.sounds = true; held = 0; ui_resume_pending = swap_keys = swap_buffer = 0; sfx_cancel(); (void)sfx_close_said(); }
/* one menu frame: input, the shared card input, the gate, the screen's own reaction */
static u32 menu_frame(flist_t *f, int n, int *sel, u32 keys, u32 mask) {
 u32 k = ui_card_input(f, n, sel, keys);
 k = ui_swap_keys(k, mask);
 tick += 1000000 / hz;
 return k;
}
static int count(int event) { int c = 0; for (int i = 0; i < nplayed; i++) c += played[i] == event; return c; }
int main(void) {
 for (hz = 30; hz <= 60; hz += 30) {
  flist_t f = {0}; int sel = 0;
  tick = 5000000;
  /* move: one sound, played at once; the end of a list is silent */
  reset(); menu_frame(&f, 5, &sel, KEY_DOWN, UI_SWAP_MENU); assert(nplayed == 1 && played[0] == SFX_MOVE && sel == 1);
  reset(); sel = 0; menu_frame(&f, 5, &sel, KEY_UP, UI_SWAP_MENU); menu_frame(&f, 5, &sel, 0, UI_SWAP_MENU); assert(nplayed == 0);
  reset(); sel = 4; menu_frame(&f, 5, &sel, KEY_DOWN, UI_SWAP_MENU); menu_frame(&f, 5, &sel, 0, UI_SWAP_MENU); assert(nplayed == 0);
  /* select and back: the gate's default, one frame later, exactly once */
  reset(); sel = 1; u32 k = menu_frame(&f, 5, &sel, KEY_A, UI_SWAP_MENU); assert(k & KEY_A); assert(nplayed == 0);
  menu_frame(&f, 5, &sel, 0, UI_SWAP_MENU); menu_frame(&f, 5, &sel, 0, UI_SWAP_MENU); assert(nplayed == 1 && played[0] == SFX_SELECT);
  reset(); menu_frame(&f, 5, &sel, KEY_B, UI_SWAP_MENU); ui_swap_enter(); /* back on the previous screen */ assert(nplayed == 1 && played[0] == SFX_BACK);
  /* a value change replaces the gate's select: one sound, the value's */
  reset(); k = menu_frame(&f, 5, &sel, KEY_A, UI_SWAP_MENU); if (k & KEY_A) sfx_play(SFX_TOGGLE);
  menu_frame(&f, 5, &sel, 0, UI_SWAP_MENU); assert(nplayed == 1 && played[0] == SFX_TOGGLE);
  reset(); k = menu_frame(&f, 5, &sel, KEY_RIGHT, UI_SWAP_MENU); if (k & KEY_RIGHT) sfx_play(SFX_VALUE);
  menu_frame(&f, 5, &sel, 0, UI_SWAP_MENU); assert(nplayed == 1 && played[0] == SFX_VALUE);
  /* a press that does nothing (a disabled row): silent */
  reset(); k = menu_frame(&f, 5, &sel, KEY_A, UI_SWAP_MENU); if (k & KEY_A) sfx_cancel();
  menu_frame(&f, 5, &sel, 0, UI_SWAP_MENU); assert(nplayed == 0);
  /* a tab: its sound when the new tab comes up, not also select/back */
  reset(); k = menu_frame(&f, 5, &sel, KEY_R, UI_SWAP_MENU); assert(k & KEY_R); sfx_play(SFX_TAB); ui_swap_enter();
  menu_frame(&f, 5, &sel, 0, UI_SWAP_MENU); assert(nplayed == 1 && played[0] == SFX_TAB);
  /* touch: a new row moves (one sound), the chosen row again selects (one sound) */
  reset(); sel = 0; touch_y = (u32)(UI_CARD_ROWS_Y + UI_CARD_ROW * 2 + 1);
  menu_frame(&f, 5, &sel, KEY_TOUCH, UI_SWAP_MENU); assert(sel == 2 && nplayed == 1 && played[0] == SFX_MOVE);
  menu_frame(&f, 5, &sel, KEY_TOUCH, UI_SWAP_MENU); menu_frame(&f, 5, &sel, 0, UI_SWAP_MENU); assert(nplayed == 2 && played[1] == SFX_SELECT);
  /* the question: opening it is one sound (not also select); the double tap's second press is silent and does
     not answer; a later yes is one confirm; B is one deny */
  reset(); k = menu_frame(&f, 5, &sel, KEY_A, UI_SWAP_MENU); assert(k & KEY_A);
  script_n = 30; memset(script, 0, sizeof(script)); script[2] = KEY_A; script[3 + hz / 3] = KEY_LEFT; script[5 + hz / 3] = KEY_A;
  bool yes = ui_confirm_pick_update("Quit?", "", "Quit", "Cancel", NULL, NULL);
  ui_swap_enter();
  assert(yes && count(SFX_DIALOG) == 1 && count(SFX_SELECT) == 0 && count(SFX_MOVE) == 1 && count(SFX_CONFIRM) == 1 && nplayed == 3);
  reset(); script_n = 30; memset(script, 0, sizeof(script)); script[3 + hz / 3] = KEY_B;
  assert(!ui_confirm_pick_update("Quit?", "", "Quit", "Cancel", NULL, NULL)); ui_swap_enter();
  assert(count(SFX_DIALOG) == 1 && count(SFX_DENY) == 1 && count(SFX_BACK) == 0 && nplayed == 2);
  /* the many-answer question: the same sounds (open, move, confirm, deny), the same double-tap guard, no dim */
  { static const char *const words[] = { "Use it", "Start empty", "Back" };
   reset(); k = menu_frame(&f, 5, &sel, KEY_A, UI_SWAP_MENU); assert(k & KEY_A);
   script_n = 30; memset(script, 0, sizeof(script)); script[2] = KEY_A; script[3 + hz / 3] = KEY_LEFT; script[5 + hz / 3] = KEY_A;
   assert(ui_confirm_pick_n("This save looks different", "", words, 3, 2) == 1); ui_swap_enter();
   assert(count(SFX_DIALOG) == 1 && count(SFX_SELECT) == 0 && count(SFX_MOVE) == 1 && count(SFX_CONFIRM) == 1 && nplayed == 3 && !ui_resume_pending);
   reset(); script_n = 30; memset(script, 0, sizeof(script)); script[3 + hz / 3] = KEY_RIGHT; script[4 + hz / 3] = KEY_B;
   assert(ui_confirm_pick_n("This save looks different", "", words, 3, 2) == -1); ui_swap_enter();
   assert(count(SFX_DIALOG) == 1 && count(SFX_MOVE) == 0 && count(SFX_DENY) == 1 && count(SFX_BACK) == 0 && nplayed == 2); }
  /* the pause card: open is one sound; B resumes the game (a hand-over: no back sound, the card's close sound);
     PS1 run_game's own SFX_BACK after the resume never plays (no menu gate runs in a game), the next open drops it */
  reset(); assert(ui_pause_open()); menu_frame(&f, 5, &sel, 0, KEY_B | UI_SWAP_REMOTE); assert(nplayed == 1 && played[0] == SFX_PAUSE_OPEN);
  menu_frame(&f, 5, &sel, KEY_B, KEY_B | UI_SWAP_REMOTE); for (int i = 0; i < hz / 4; i++) menu_frame(&f, 5, &sel, 0, KEY_B | UI_SWAP_REMOTE);
  assert(ui_pause_close()); assert(nplayed == 2 && played[1] == SFX_PAUSE_CLOSE);
  sfx_play(SFX_BACK); tick += 3000000; /* the game runs */ assert(nplayed == 2);
  ui_resume_pending = 0; assert(ui_pause_open()); menu_frame(&f, 5, &sel, 0, KEY_B); assert(nplayed == 3 && played[2] == SFX_PAUSE_OPEN);
  /* a board load stands for the card closing after it: load once, no close sound */
  sfx_play(SFX_LOAD); sfx_card_closes(); menu_frame(&f, 5, &sel, 0, KEY_START); assert(nplayed == 4 && played[3] == SFX_LOAD);
  assert(ui_pause_close()); assert(nplayed == 4);
  /* a launch plays at once and leaves nothing behind */
  reset(); sfx_play(SFX_SELECT); sfx_play(SFX_LAUNCH); assert(nplayed == 1 && played[0] == SFX_LAUNCH); sfx_flush(); assert(nplayed == 1);
  /* Menu sounds off: nothing, for every kind of press */
  reset(); g_cfg.sounds = false; ui_resume_pending = 0;
  menu_frame(&f, 5, &sel, KEY_DOWN, UI_SWAP_MENU); menu_frame(&f, 5, &sel, KEY_A, UI_SWAP_MENU); menu_frame(&f, 5, &sel, KEY_B, UI_SWAP_MENU);
  sfx_play(SFX_VALUE); menu_frame(&f, 5, &sel, 0, UI_SWAP_MENU); assert(ui_pause_open()); menu_frame(&f, 5, &sel, 0, 0); assert(ui_pause_close());
  sfx_play(SFX_LAUNCH); assert(nplayed == 0);
  /* HOME > Close: nothing after it */
  reset(); closing = true; ui_resume_pending = 0; menu_frame(&f, 5, &sel, KEY_DOWN, UI_SWAP_MENU); menu_frame(&f, 5, &sel, KEY_A, UI_SWAP_MENU);
  menu_frame(&f, 5, &sel, 0, UI_SWAP_MENU); sfx_play(SFX_LAUNCH); assert(nplayed == 0);
  /* every event plays a file; variants take turns, never the same file twice in a row */
  reset();
  for (int e = 0; e < SFX_COUNT; e++) { sfx_now(e); assert(nplayed == 1 && played[0] == e && files[0] >= 0 && files[0] < SFX_FILES); nplayed = 0; }
  for (int e = 0; e < SFX_COUNT; e++) if (sfx_events[e].variants > 1)
   for (int i = 0; i < 9; i++) { sfx_now(e); if (nplayed > 1) assert(files[nplayed - 1] != files[nplayed - 2]); }
 }
 puts("Menu sounds PASS: 30/60 Hz; move at once, list ends silent; gate select/back once, one frame after the press; value/toggle/tab replace it; dead press silent; touch move/select; two-way and many-answer questions open/move/confirm/deny once and a double tap silent; pause open/close once, hand-over without back, PS1 resume line dropped; board load stands for the close; launch at once; Menu sounds off and HOME > Close silent; every event has a file, variants take turns");
}
'''


def audit():
    failures = []
    # A screen's own move sound right after the shared list/card input would double it.
    for path in sorted([*(root/'app').rglob('*.c'), *(root/'systems').rglob('*.c'), *(root/'systems').rglob('*.inc')]):
        s = path.read_text()
        for m in re.finditer(r'ui_(?:card|list)_input\([^;]*;', s):
            after = s[m.end():m.end() + 160]
            if re.search(r'^[^;]*;?[^;]*sfx_play\(SFX_MOVE\)', after.split('\n', 2)[0] + after.split('\n', 2)[1] if '\n' in after else after):
                failures.append(f'{path.relative_to(root)}: sfx_play(SFX_MOVE) right after the shared input doubles its move sound')
    names = re.findall(r'\bSFX_([A-Z_]+)\b', enum)
    for name in names:
        if name == 'COUNT':
            continue
        if not re.search(r'\[SFX_' + name + r'\]\s*=\s*\{', snd):
            failures.append(f'uisound.c: SFX_{name} has no file in sfx_events')
    # Every listed file ships, and every shipped effect file is used (music.pcm is the library's music).
    files = re.findall(r'"([a-z_0-9]+)"', snd[snd.index('sfx_files[] = {'):snd.index('#define SFX_FILES')])
    shipped = {f.stem for f in (root/'assets/romfs/sfx').glob('*.pcm')} - {'music'}
    for f in set(files) - shipped:
        failures.append(f'uisound.c: {f}.pcm is not in assets/romfs/sfx')
    for f in shipped - set(files):
        failures.append(f'assets/romfs/sfx/{f}.pcm is shipped but no event plays it')
    return failures


with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
    p = Path(tmp)
    (p/'check.c').write_text(code)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                    '-I' + str(root/'tests/app/host'), '-I' + str(root/'app/ui'), '-I' + str(root/'include/breadbox'),
                    str(p/'check.c'), '-lm', '-o', str(p/'check')], check=True)
    subprocess.run([str(p/'check')], check=True)
failures = audit()
if failures:
    raise SystemExit('Menu sound audit FAIL:\n' + '\n'.join(failures))
print('Menu sound audit PASS: no doubled move sounds after the shared input; every event has a file; the romfs set is exactly the table')
