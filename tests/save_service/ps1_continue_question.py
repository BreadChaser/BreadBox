#!/usr/bin/env python3
"""PS1's Continue question: the production states_resume_prompt()
(savestates.c) and continue_prompt() (emu.c) on scripted frames. B, HOME > Close and power-off at the question are
a Back: continue_prompt() answers false (run_game then leaves before anything runs: tests/save_service/ps1_back.py)
and the shell keeps the room as it was. Every other PS1 Continue writer stops at continue_frozen (a Start over
that could not move the old spot), which this checks in their source."""
from pathlib import Path
import re, subprocess, sys, tempfile

root = Path(__file__).resolve().parents[2]


def function(text, name):
    m = re.search(r'(?:PSX3D_COLD_FN\(\w+\) )?(?:PSX3D_COLD )?(?:static )?(?:bool|void|int) ' + name + r'\([^;{}]*?\)\s*\{', text)
    assert m, name
    i = text.index('{', m.start()) + 1
    depth = 1
    while depth:
        depth += (text[i] == '{') - (text[i] == '}')
        i += 1
    return text[m.start():i]


states = (root / 'systems/ps1/savestates.c').read_text()
emu = (root / 'systems/ps1/emu.c').read_text()
header = (root / 'include/breadbox/bb_pause.h').read_text()
bad = []
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;typedef uint16_t u16;
#define PSX3D_COLD_FN(x)
enum {KEY_A=1,KEY_B=2,KEY_X=4,KEY_Y=8,SFX_SELECT,SFX_MOVE,SFX_BACK,TW=8,TH=8,STATE_AUTO=10,NO_SLOT=-1};
''' + re.search(r'enum \{ BB_CONTINUE_ASK[^;]*;\n#define BB_CONTINUE_BUTTONS[^\n]*\n', header)[0] + \
    re.search(r'enum \{ RESUME_NONE[^;]*;', (root / 'systems/ps1/savestates.h').read_text())[0] + r'''
enum {STATES_PAGE_RING=2};
typedef struct {int unused;} C3D_Tex;typedef struct {u32 saved;} thm_hdr_t;typedef struct {char path[8];} hist_entry_t;
typedef struct {int page;hist_entry_t entry;} states_pick_t;
typedef struct {u32 key;const char *label;int icon;bool on;} ui_button_t;
static const ui_button_t bb_continue_bar[4];
static const char *script;static unsigned frame;static bool confirm_yes;
static bool went_back,decline_fails,shown_failed;static int declined,loaded;static char game_title[8]="game";
static bool aptMainLoop(void){return script[frame] && script[frame]!='C';}
static void dev_tick_keys(void){}
static void input_poll(void){}
static u32 input_keys_down(void){char c=script[frame++];return c=='A'?KEY_A:c=='B'?KEY_B:c=='X'?KEY_X:0;}
static int history_list(const char *g,bool c,hist_entry_t *e,int n){(void)g;(void)c;(void)e;(void)n;return 0;}
static bool point_unseen;   /* the SD card stops showing the point after a failed Start over */
static bool state_stat(const char *p,int s,u32 *saved){(void)p;(void)s;*saved=1;return !point_unseen;}
static bool thumb_read(const char *p,int s,u16 *px,thm_hdr_t *h,bool k){(void)p;(void)s;(void)px;(void)h;(void)k;return false;}
static bool make_tex(C3D_Tex *t,const u16 *px){(void)t;(void)px;return false;}
static void C3D_TexDelete(C3D_Tex *t){(void)t;}
static u32 ui_bar_input(const ui_button_t *b,int n,u32 k){(void)b;(void)n;return k;}
static u32 ui_swap_keys(u32 k,u32 m){(void)m;return k;}
static void ui_swap_enter(void){}
static void ui_pause_frame(void){}
static void ui_begin(void){}
static void resume_top(void *p){(void)p;}
static void resume_bottom(void *p);
static void video_present_vsync(bool b,void (*t)(void*),void (*d)(void*),void *p){(void)b;(void)t;d(p);}
static void sfx_play(int s){(void)s;}
static void sfx_now(int s){(void)s;}
static void sfx_flush(void){}
static bool ui_confirm_action(const char *a,const char *b,const char *c,bool d){(void)a;(void)b;(void)c;(void)d;return confirm_yes;}
static bool state_load_from(int s){(void)s;loaded++;return true;}
static void osd(const char *f,...){(void)f;}
static void log_printf(const char *f,...){(void)f;}
static bool continue_frozen;
static bool unseen_after_fail;
static bool continue_declined(void){declined++;continue_frozen=decline_fails;bool ok=!decline_fails;point_unseen=!ok && unseen_after_fail;decline_fails=false;return ok;}
static int states_run(bool s,const char *p,const char *t,int *slot,states_pick_t *pick){(void)s;(void)p;(void)t;(void)slot;(void)pick;return 0;}
static bool state_load_file(const char *p,int s){(void)p;(void)s;return false;}
static void bb_continue_went_back(void){went_back=true;}
''' + re.search(r'typedef struct \{ C3D_Tex tex;[^}]*\} resume_t;', states)[0] + r'''
static void resume_bottom(void *p){shown_failed|=((resume_t *)p)->over_failed;}
''' + function(re.search(r'int bb_continue_input\(u32 keys,bool older\)\n\{.*?\n\}', (root / 'app/ui/bb_pause.c').read_text(), re.S)[0], 'bb_continue_input') \
    + '\n' + function(states, 'states_resume_prompt') + '\n' + function(emu, 'continue_prompt') + r'''
static bool run(const char *keys,bool yes)
{
 script=keys;frame=0;confirm_yes=yes;went_back=shown_failed=continue_frozen=false;declined=loaded=0;
 return continue_prompt("prefix");
}
int main(void)
{
 static const char *const back[]={"B","..B","C","..C","XB"};   /* B, Close (power-off is aptMainLoop false too), X then No then B */
 for(unsigned i=0;i<5;i++)assert(!run(back[i],false) && went_back && !declined && !loaded);
 assert(run(".A",false) && !went_back && loaded==1);
 assert(run("X",true) && !went_back && declined==1 && !loaded && !shown_failed);
 /* Start over that could not move the old spot: the question stays up and says so; nothing starts over */
 decline_fails=true;assert(!run("X.B",true) && went_back && declined==1 && !loaded && shown_failed);
 decline_fails=true;assert(!run("X..C",true) && went_back && declined==1 && shown_failed);
 decline_fails=true;assert(run("X.A",true) && declined==1 && loaded==1 && shown_failed && !continue_frozen);   /* A: plays on, unfrozen */
 decline_fails=true;assert(run("X.X",true) && !went_back && declined==2 && !loaded && shown_failed);   /* a second try that works */
 decline_fails=unseen_after_fail=true;assert(!run("X",true) && went_back && declined==1 && !loaded);   /* no point to show: back */
 unseen_after_fail=point_unseen=false;
 puts("PS1 Continue question: B and Close answer leave (nothing runs); A loads the point; X starts over; a failed Start over keeps the question up with the failure words: PASS");
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    src = Path(td) / 'ps1.c'
    src.write_text(code)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined', str(src), '-o', str(Path(td) / 'ps1')], check=True)
    if subprocess.run([str(Path(td) / 'ps1')]).returncode:
        bad.append('the PS1 Continue question answers wrongly')
# every PS1 Continue writer stops at continue_frozen (sleep, HOME, timer/pause auto-save, the end's)
for name in ('sleep_save', 'home_save_take', 'autosave_start', 'save_put'):
    if 'continue_frozen' not in function(emu, name):
        bad.append(f'{name}() writes the Continue point without checking continue_frozen')
if not re.search(r'!continue_frozen\)\s*\n\s*log_printf\("auto-save: %s\\n", state_save_auto_later\(\)', emu):
    bad.append('the end of a PS1 game makes its Continue point without checking continue_frozen')
if bad:
    sys.exit('\n'.join(bad))
print('PS1 Continue question PASS: B / Close answer leave; A loads; X starts over (a failed one asks again); every other Continue writer checks continue_frozen')
