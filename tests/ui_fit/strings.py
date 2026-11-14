#!/usr/bin/env python3
"""Player-visible strings fit their boxes (bb-uitest-fix-r1, made to measure the shipped strings in r1b).

The strings come from production: a small C program includes ui_format.h and calls ui_empty_help() for the real
paths (no folder chosen, the default /roms/, a long custom folder) at the room card and the library, and prints
the credits and install pages and the Continue question's text compiled from their own definitions
(breadbox_credits.c, the shared pause component, bb_pause.c). The L/R hints are the ui_format.h defines.
A negative control runs the same pipeline on a copy of the sources with one oversized production string and
must fail, so a passing run says the shipped text fits.

The width model is a conservative model of the system font, calibrated on Azahar's stand-in font (it is wider
and letter-spaced next to the real 3DS font, so what fits here fits on the console): at the 21 px text floor
"Copy games to the SD" measured 192 px and the model gives 200. Text that wraps breaks at spaces like
shelf_scene.c text_lines() and the dialog's wrap_text(). A string that does not fit is a failure, never a cut.
"""
import re, shutil, subprocess, sys, tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
NARROW, SLIM, WIDE, HUGE = set("il.,:;'|!I "), set("frtj()/-\"1"), set("ABCDEFGHJKLNOPQRSTUVXYZmw+"), set("MW")


def width(s, px):
    w = 0
    for ch in s:
        w += 6 if ch in NARROW else 7 if ch in SLIM else 16 if ch in HUGE or ord(ch) > 127 else 13 if ch in WIDE else 10   # a button glyph: 16
    return w * px / 21 * 1.05   # 5% over the measured stand-in font


def lines_needed(s, px, box):
    words, lines, cur = s.split(' '), 0, ''
    for w in words:
        t = (cur + ' ' + w) if cur else w
        if width(t, px) <= box:
            cur = t
            continue
        if cur:
            lines += 1
        cur = w
        if width(w, px) > box:
            return 99
    return lines + (1 if cur else 0)


def style(name):
    s = (root / 'include/breadbox/ui_style.h').read_text()
    m = re.search(r'#define\s+' + name + r'\s+([\d.]+)', s)
    return float(m[1])


def c_define(name, header='include/breadbox/ui_format.h', src=None):
    """A string #define from a header, its macros and C escapes resolved (e.g. UI_GLYPH_L "  Jump")."""
    src = src if src is not None else (root / header).read_text()
    defs = dict(re.findall(r'#define\s+(\w+)\s+(.+)', src))
    out = ''
    for tok in re.findall(r'"(?:\\.|[^"\\])*"|\w+', defs[name]):
        out += c_define(tok, header, src) if tok[0] != '"' else tok[1:-1].encode().decode('unicode_escape').encode('latin-1').decode('utf-8')
    return out


FLOOR = style('UI_TEXT_FLOOR_PX')
ROOM_TEXT = style('UI_ROOM_TEXT_R') - (style('UI_ROOM_MARGIN_X') + style('UI_PAPER_MARGIN_GAP'))
DIALOG_W, DIALOG_LINES = style('UI_DIALOG_TEXT_W'), int(style('UI_DIALOG_LINES'))
CARD_NAME_W, CARD_ALERT_LINES = style('UI_PC_CC_NAME_W'), int(style('UI_PC_CC_ALERT_LINES'))
LONG_FOLDER = 'sdmc:/a/very/long/folder/name/for/all/my/games/PlayStation/'

PROGRAM = r'''
#include <stdio.h>
#include "ui_format.h"
#include "version.h"
#include "welcome.h"
int config_welcome = -1;
void config_save(void) {}
/* the same conservative model as width() below, so the welcome's measured lines are checked as they are made */
static float model(const char *s, void *ud)
{
	(void)ud; float w = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		if ((*p & 0xc0) == 0x80) continue;
		w += strchr("il.,:;'|!I ", *p) ? 6 : strchr("frtj()/-\"1", *p) ? 7 : strchr("MW", *p) || *p > 127 ? 16 :
			strchr("ABCDEFGHJKLNOPQRSTUVXYZmw+", *p) ? 13 : 10;
	}
	return w * 1.05f;
}
static void say(const char *what, const char *s) { printf("%s\t%s\n", what, s); }
CREDITS
INSTALL
int main(void)
{
	char t[160];
	static const char *const dirs[] = { "", "sdmc:/roms/", "LONG", "sdmc:/ROMS/NINTENDO/GAMEBOYADVANCE", "sdmc:/Games/PlayStation" };
	for (int at = 0; at < 2; at++)
		for (int i = 0; i < 5; i++) {
			ui_empty_help(t, sizeof t, dirs[i], at);
			printf("empty-%s-%s\t%s\n", at ? "library" : "room", i >= 2 ? "custom" : "default", t);
		}
	for (unsigned i = 0; i < sizeof credits_pages / sizeof *credits_pages; i++) say("credits", credits_pages[i]);
	for (unsigned i = 0; i < sizeof install_pages / sizeof *install_pages; i++) say("install", install_pages[i]);
	say("start-over", START_OVER_TEXT);
	say("over-failed", UI_CONTINUE_OVER_FAILED);
	/* the first-launch welcome's card lines (welcome.c), at the folders above and the biggest counts */
	const char *welcome[] = { BB_WELCOME_SYSTEMS_1, BB_WELCOME_SYSTEMS_2, BB_WELCOME_RESCAN, BB_WELCOME_UNTOUCHED };
	for (unsigned i = 0; i < sizeof welcome / sizeof *welcome; i++) say("welcome", welcome[i]);
	char title[48], l1[96], l2[96];
	for (int i = 0; i < 5; i++)
		for (int count = -1; count <= 9999; count = count < 0 ? 1 : count * 10 + 9) {
			int n = bb_welcome_games(count, dirs[i], title, sizeof title, l1, l2, sizeof l1, model, NULL, WELCOME_BOX);
			say("welcome", l1);
			if (n > 1) say("welcome", l2);
		}
	return 0;
}
'''


def c_literal(src, pattern, what):
    m = re.search(pattern, src, re.S)
    if not m:
        sys.exit(f'ui strings: {what} not found in its source')
    return m[1]


def production(tree):
    """The shipped strings, compiled from the sources under `tree`: [(kind, text)]."""
    credits = (tree / 'app/ui/breadbox_credits.c').read_text()
    pause_inc = (tree / 'app/ui/components/bb_pause.inc').read_text()
    pause = (tree / 'app/ui/bb_pause.c').read_text()
    prog = PROGRAM.replace('"LONG"', '"' + LONG_FOLDER + '"').replace('WELCOME_BOX', str(ROOM_TEXT) + 'f')
    prog = prog.replace('CREDITS', c_literal(credits, r'(static const char \*const credits_pages\[\]\s*=\s*\{.*?\n\};)', 'credits pages'))
    prog = prog.replace('INSTALL', c_literal(credits, r'(static const char \*const install_pages\[\]\s*=\s*\{.*?\n\};)', 'install pages'))
    c_literal(pause_inc, r'(over_failed\?UI_CONTINUE_OVER_FAILED:NULL)', 'the Continue question\'s red foot (its ui_words.h words)')
    prog = prog.replace('START_OVER_TEXT', c_literal(pause, r'ui_confirm_action\("Start over\?",("(?:[^"\\]|\\.)*")', 'the Start over question'))
    with tempfile.TemporaryDirectory() as td:
        c = Path(td) / 'strings.c'
        c.write_text(prog)
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Werror', '-I' + str(tree / 'include/breadbox'), '-I' + str(tree / 'apps/breadbox'),
                        '-I' + str(tree / 'app/ui'), str(c), str(tree / 'app/ui/welcome.c'), '-o', str(Path(td) / 'strings')], check=True)
        out = subprocess.run([str(Path(td) / 'strings')], check=True, capture_output=True, text=True).stdout
    return [tuple(line.split('\t', 1)) for line in out.splitlines()]


def checks(tree):
    """(text, px, box, lines) for every shipped string: the boxes they are drawn in."""
    budget = {'empty-room-default': 2, 'empty-library-default': 2, 'empty-room-custom': 3, 'empty-library-custom': 3}
    out = []
    for kind, text in production(tree):
        if kind == 'welcome':   # one ruled line of the room card each (the PSX3D list: tests/welcome)
            out.append((text, FLOOR, ROOM_TEXT, 1))
        elif kind in budget:
            out.append((text, FLOOR, ROOM_TEXT, budget[kind]))
        elif kind == 'over-failed':   # the Continue postcard's red foot line (postcards.inc pc_continue_card)
            out.append((text, FLOOR, CARD_NAME_W, CARD_ALERT_LINES))
        elif kind in ('credits', 'install', 'start-over'):
            out.append((text, FLOOR, DIALOG_W, DIALOG_LINES))   # each page whole on the dialog: no split sentence
        else:
            sys.exit(f'ui strings: unknown kind {kind}')
    fmt = (tree / 'include/breadbox/ui_format.h').read_text()
    for hint in ('UI_HINT_LR_JUMP', 'UI_HINT_LR_TURN'):   # F04: the shelf card's foot line, one line
        out.append((c_define(hint, src=fmt), FLOOR, ROOM_TEXT, 1))
    if sum(1 for c in out if c[3] == DIALOG_LINES) < 8 or not any('Put games in' in c[0] for c in out):
        sys.exit('ui strings: the production strings were not all read')
    return out


def failures(tree):
    bad = []
    for text, px, box, n in checks(tree):
        got = lines_needed(text, px, box)
        if got > n:
            bad.append(f'{text!r}: needs {got} lines of {box:.0f} px at {px:g} px, has {n}')
    return bad


def negative_control():
    """One oversized production string in a copy of the sources must fail the suite."""
    with tempfile.TemporaryDirectory() as td:
        tree = Path(td)
        for d in ('include/breadbox', 'apps/breadbox', 'app/ui/components'):
            shutil.copytree(root / d, tree / d)
        for f in ('app/ui/breadbox_credits.c', 'app/ui/bb_pause.c', 'app/ui/welcome.c', 'app/ui/breadbox_import.h', 'app/ui/app_policy.h'):
            shutil.copy(root / f, tree / f)
        fmt = tree / 'include/breadbox/ui_format.h'
        text = fmt.read_text()
        big = text.replace('"Put games in %s on the SD card."', '"Put games in %s on the SD card. ' + 'MMMM ' * 40 + '"')
        assert big != text, 'negative control: the empty-library sentence moved'
        fmt.write_text(big)
        return failures(tree)


def main():
    bad = failures(root)
    if bad:
        sys.exit('\n'.join(bad))
    control = negative_control()
    if not control:
        sys.exit('ui strings: the negative control (an oversized ui_empty_help sentence) passed: the suite does not measure production text')
    n = len(checks(root))
    print(f'ui strings fit PASS: {n} production strings fit their boxes (conservative emulator-font model); an oversized one fails')


if __name__ == '__main__':
    main()
