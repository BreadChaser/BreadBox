#!/usr/bin/env python3
"""Saves made inside the pause card queue their message for after it; a newer save's
message replaces an older one still waiting, so resuming never shows a stale slot. The real
osd queue code is compiled from systems/ps1/emu.c; the messages are the shipped postcard words
(include/breadbox/ui_words.h), so a change of wording cannot silently turn the check off."""
import re, subprocess, sys, tempfile
from pathlib import Path
root = Path(__file__).resolve().parents[2]
src = (root / 'systems/ps1/emu.c').read_text()
words = dict(re.findall(r'#define\s+(UI_PC_\w+)\s+("(?:\\.|[^"\\])*")', (root / 'include/breadbox/ui_words.h').read_text()))
head = src[src.index('#define OSD_QUEUE'):src.index('static bool osd_dirty;') + len('static bool osd_dirty;')]
start = src.index('// A save')
end = src.index('// a frame passed: the shown message counts down')
tick = src[end:src.index('static void osd(const char *fmt, ...)\n{')]
prog = '''#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define PSX3D_COLD
''' + ''.join(f'#define {k} {v}\n' for k, v in words.items()) + r'''
static void log_printf(const char *f, ...) { (void)f; }
static char said[8][96];
static const char *msg(int k, const char *fmt, int n) { snprintf(said[k], sizeof(said[k]), fmt, n); return said[k]; }
''' + head + '\n' + src[start:end] + tick + r'''
int main(void)
{
	/* pause: X saves postcard 1, X saves postcard 2, then save over postcard 2 */
	osd_push(msg(0, UI_PC_SAVED, 1), false);
	osd_push(msg(1, UI_PC_SAVED, 2), false);
	osd_push(msg(2, UI_PC_SAVED_OVER, 2), false);
	assert(osd_n == 1 && !strcmp(osd_q[0].text, said[2]));
	assert(osd_frames == OSD_FRAMES);
	/* other messages stay, in order (a load is not a save); only the save line is replaced */
	osd_push(msg(3, UI_PC_LOADED, 3), false);
	osd_push(msg(4, UI_PC_SAVED, 4), false);
	assert(osd_n == 2 && !strcmp(osd_q[0].text, said[3]) && !strcmp(osd_q[1].text, said[4]));
	osd_push("That save is damaged", true);
	osd_push(msg(5, UI_PC_SAVED, 12), false);
	assert(osd_n == 3 && !strcmp(osd_q[1].text, "That save is damaged") && !strcmp(osd_q[2].text, said[5]));
	for (int i = 0; i < OSD_FRAMES; i++) osd_tick();
	assert(osd_n == 2 && osd_q[0].alert && osd_frames == OSD_ALERT_FRAMES);
	puts("PS1 save messages: a newer save replaces a waiting older one: PASS");
	return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    c = Path(d) / 'osd.c'; c.write_text(prog)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-variable',
                    '-fsanitize=address,undefined', '-g', str(c), '-o', str(Path(d) / 'osd')], check=True)
    sys.exit(subprocess.run([str(Path(d) / 'osd')]).returncode)
