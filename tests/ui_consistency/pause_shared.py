#!/usr/bin/env python3
"""One pause card and one Continue question for every system (deep UI test F06/F08/F13/F16, Old F3).

1. Vocabulary: no player-visible string calls a saved moment a "state"/"save state"; postcards everywhere.
2. The PS1 pause draws through the shared presenters and takes the shared rows' words from bb_pause.c;
   "Quit to room" (it lands on the shelf) is "Quit game"; the Save value is "Saved", no "Saved · slot".
3. The Continue question: the shared bar has no B, and the real bb_continue_input() (compiled here with
   stubs) never starts over on B and asks once more before X starts over.
"""
from pathlib import Path
import re, subprocess, sys, tempfile
r = Path(__file__).resolve().parents[2]
bad = []

# 1. what players read: string literals passed to the notice/toast/title helpers
calls = r'\b(?:osd|emu_osd_alert|notice|message|menu_note|bb_pause_notice|pc_notice|ui_list_context|ui_dialog_draw|ui_confirm_action|snprintf)\s*\('
word = re.compile(r'\b(?:save[ -]?states?|states?)\b', re.I)
for p in sorted(list((r / 'app').rglob('*.c')) + list((r / 'app').rglob('*.inc')) + list((r / 'systems').rglob('*.c')) + list((r / 'systems').rglob('*.inc'))):
    if p.name in ('devcontrol.c', 'devswitch.c', 'devnet.c') or 'tests' in p.parts:
        continue
    for n, line in enumerate(p.read_text(errors='replace').splitlines(), 1):
        if 'log_printf' in line or 'error_reply' in line or not re.search(calls, line):
            continue
        for lit in re.findall(r'"((?:\\.|[^"\\])*)"', line):
            if word.search(lit) and not re.search(r'[/=%]|\.st|state\d|rewind', lit) and len(lit) > 6:
                bad.append(f'{p.relative_to(r)}:{n}: player text says "state": "{lit}"')
fmt = (r / 'include/breadbox/ui_words.h').read_text()
for name in ('UI_PC_SAVED', 'UI_PC_LOADED', 'UI_PC_SAVE_FAILED', 'UI_PC_ALL_USED'):
    m = re.search(r'#define ' + name + r' "([^"]*)"', fmt)
    if not m or not re.search(r'[Pp]ostcard', m[1]):
        bad.append(name + ' must name the postcard')

# 2. the PS1 pause is the shared card
emu_inc = (r / 'app/ui/components/emu.inc').read_text()
emu = (r / 'systems/ps1/emu.c').read_text()
pause = (r / 'app/ui/bb_pause.c').read_text()
for need in ('bb_pause_present_top(', 'bb_pause_present_card('):
    if need not in emu_inc:
        bad.append('PS1 pause does not draw through ' + need)
names = re.findall(r'"([^"]*)"', re.search(r'static const char \*const menu_names\[\] = \{(.*?)\};', emu, re.S)[1])
shared = re.findall(r'"([^"]*)"', re.search(r'static const char \*const kind_name\[\]=\{(.*?)\};', pause, re.S)[1])
order = re.search(r'enum \{ (M_RESUME.*?) \};', emu, re.S)[1].replace(' ', '').split(',')
# the PS1 finds its shared rows by name: every row both have must be spelled as the shared card spells it
common = [n for n in names if n in shared]
for want in ('Resume', 'Undo', 'Save postcard', 'Load postcard', 'Settings', 'Cheats', 'Reset game', 'Quit game'):
    if want not in common:
        bad.append(f'PS1 row "{want}" is not spelled as the shared card spells it')
if 'bb_pause_row_name(kind)' not in emu or 'strcmp(bb_pause_row_name(k), menu_names[item])' not in emu:
    bad.append('the PS1 card does not take the shared rows\' words from bb_pause_row_name')
if 'Quit to room' in pause + emu:
    bad.append('"Quit to room" lands on the shelf: it is "Quit game"')
if '"Quit game"' not in pause:
    bad.append('the shared rows have no "Quit game"')
if re.search(r'Saved · slot|X · slot|saved >|none yet', pause + emu):
    bad.append('a pause value still says "Saved · slot" / "X · slot" / "N saved >" / "none yet"')
if '"Paused", "PlayStation"' in emu_inc or 'bb_pause_played' in emu_inc:
    bad.append('the PS1 tag/card keeps its own lines')

# 3. the Continue question
bar = re.search(r'const ui_button_t bb_continue_bar\[4\]=\{(.*?)\};', pause, re.S)
if not bar or not bar[1].startswith('{KEY_B,"Back"') or 'KEY_Y' not in bar[1].split('},')[-1]:
    bad.append('the Continue bar: B Back first, Y Older last (BB_CONTINUE_BUTTONS cuts it)')
if 'resume_bar' in (r / 'systems/ps1/savestates.c').read_text():
    bad.append('the PS1 Continue question keeps its own bar')
fn = re.search(r'int bb_continue_input\(u32 keys,bool older\)\n\{.*?\n\}', pause, re.S)
if not fn:
    bad.append('bb_continue_input() not found')
else:
    head = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;
enum { KEY_A = 1, KEY_B = 2, KEY_X = 4, KEY_Y = 8, SFX_SELECT = 0, SFX_MOVE, SFX_BACK };
enum { BB_CONTINUE_ASK, BB_CONTINUE_YES, BB_CONTINUE_OVER, BB_CONTINUE_OLDER, BB_CONTINUE_BACK };
static int asked, answer_yes;
static __attribute__((unused)) void sfx_play(int s) { (void)s; }
static void sfx_now(int s) { (void)s; }
static void sfx_flush(void) {}
static void ui_swap_enter(void) {}
static bool ui_confirm_action(const char *t, const char *m, const char *a, bool g) { (void)t; (void)m; (void)a; (void)g; asked++; return answer_yes; }
'''
    main = r'''
int main(void) {
	assert(bb_continue_input(KEY_B, true) == BB_CONTINUE_BACK && !asked);     /* B: back, never a fresh start */
	assert(bb_continue_input(KEY_A, false) == BB_CONTINUE_YES);
	answer_yes = 0; assert(bb_continue_input(KEY_X, false) == BB_CONTINUE_ASK && asked == 1);   /* asked, said no */
	answer_yes = 1; assert(bb_continue_input(KEY_X, false) == BB_CONTINUE_OVER && asked == 2);
	assert(bb_continue_input(KEY_Y, false) == BB_CONTINUE_ASK && bb_continue_input(KEY_Y, true) == BB_CONTINUE_OLDER);
	return 0;
}
'''
    with tempfile.TemporaryDirectory() as td:
        c = Path(td) / 'c.c'
        c.write_text(head + fn[0] + main)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Werror', '-fsanitize=address,undefined', str(c), '-o', str(Path(td) / 'c')], check=True)
        if subprocess.run([str(Path(td) / 'c')]).returncode:
            bad.append('bb_continue_input() answers wrongly')

# 4. Old F1/F2: opening the card writes no Continue point; quitting from the card puts the game's own picture
# back on the top screen before the room takes its TV picture from there
run = re.search(r'bool bb_pause_run\(void\)\n\{.*?\n\}', pause, re.S)[0]
opening = run[:run.index('ui_pause_open()')]
if 'bb_save_checkpoint' in opening or 'bb_save_state(' in opening:
    bad.append('the pause card writes the Continue point before it opens')
tail = run[run.index('done:'):]
if not re.search(r'if\(quit\) \{\s*(?://[^\n]*\s*)*if\(leaving\)quit_picture_take\(\);', tail):
    bad.append('quitting from the pause card does not keep the game\'s own picture for the room\'s TV')
main = (r / 'app/ui/main.c').read_text()
if not re.search(r'!bb_pause_last_picture\(path\)\)\s*bb_last_picture_from_screen\(path\)', main):
    bad.append('the room takes the TV picture from the (dimmed) screen even after a quit from the pause card')
if 'case K_QUIT: if(bb_save_checkpoint' in run:
    bad.append('Quit writes the Continue point twice')
# review fix r1b finding 1: the pause's own Continue point, taken before the card and written after its first frame
if 'bb_save_pause_take()' not in opening or 'bb_save_pause_put()' in opening:
    bad.append('the pause card must take its Continue point at once and write it only after the first frame')
loop = run[run.index('ui_pause_open()'):run.index('done:')]
if not re.search(r'video_present_vsync\(false,draw_paused_top,draw_pause,NULL\);\s*if\(continue_waiting\)\{[^}]*bb_save_pause_put\(\);\}', loop):
    bad.append('the pause Continue point is not handed on right after the first frame')

# 5. F05/P3: the cheats card says nothing the bar contradicts; A toggles a cheat; the paused game stays on top
feat = (r / 'app/ui/components/features.inc').read_text() + (r / 'app/ui/bb_feature_ui.c').read_text()
if re.search(r'Select toggles|X Inherit|Left/Right Change', feat):
    bad.append('the cheats / mappings card keeps a standing helper line')
if 'UI_ICON_TOGGLE' not in feat or 'bb_pause_scene_top(' not in feat or 'ui_shared_words(' not in feat:
    bad.append('cheats: A must show the toggle, the top the paused game, repeated starts folded')

# 6. leaving the postcard board after a load or a save: the request is repeated until the board closes
board = re.search(r'static int slots_run\(bool saving\) \{.*?\n\}', pause, re.S)[0]
if 'if(result>=0 && !pc_animating(v))k|=KEY_START;' not in board or '!leaving' in board:
    bad.append('the postcard board asks to close only once after a load (it can stay open over the game)')

if bad:
    sys.exit('\n'.join(bad))
print('pause shared PASS: postcard words, one pause card for PS1 and GBA, Continue: B goes back, never starts over; the pause Continue point after the first frame')
