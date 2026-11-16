#!/usr/bin/env python3
"""HOME > Close at each question asked before a game runs (review finding 3).

PS1: the real card_question.c and the real early exit of run_game with its leave_unplayed()
(taken out of emu.c verbatim) run against fakes, linked like the app: --wrap sends retro_unload_game,
retro_deinit and video_game_end to the real guards in app/ui/breadbox_handoff.c. The player
answers every question up to the k-th, then the system closes the app inside it: the shared
dialog returns false from then on (aptMainLoop false) and the GPU right is gone. The early
exit must keep the CPU-only work (cards_end, audio_exit) and reach none of the real teardown
functions (they submit and wait for GPU frames). With the GPU right it tears everything down.

GBA: the real damaged-save question (save_damaged_ask, gba_save_damaged from gba.c) answers
Back on Close at each of its questions, and the teardown it then reaches calls the GPU only
under gspHasGpuRight (checked in the source of gba_run)."""
from pathlib import Path
import re, subprocess, sys, tempfile

root = Path(__file__).resolve().parents[2]
emu = (root / 'systems/ps1/emu.c').read_text()
a = emu.index('\telse if (!cards_question()) {')
b = emu.index('\t\treturn true;\n\t}\n', a) + len('\t\treturn true;\n\t}\n')
early = emu[a:b].replace('\telse if', '\tif', 1)
# the early exit is the one shared leave-before-play path (leave_unplayed, also Back at Continue?), verbatim
la = emu.index('PSX3D_COLD_FN(leave_unplayed) static bool leave_unplayed(void)')
leave = emu[la:emu.index('\n}\n', la) + 3]
assert 'leave_unplayed();' in early
assert 'retro_unload_game();' in leave and 'video_game_end();' in leave and 'retro_deinit();' in leave
mk = (root / 'build/3ds/Makefile').read_text()
assert '--wrap=retro_unload_game,--wrap=video_game_end' in mk and '--wrap=retro_init,--wrap=retro_deinit' in mk

prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "memory_cards.h"
static int asked, close_at, answers[16];   /* answer of the n-th question (1 = A) */
static bool closed, gpu = true, restored_ok = true;
static int trouble, backup, cards_ended, audio_ended;
int gpu_work, unloaded;
static bool core_initialized = true;
bool ui_confirm_choice(const char *title, const char *msg, const char *a, const char *b, bool game)
{
	(void)title; (void)msg; (void)a; (void)b; (void)game;
	if (closed) return false;               /* aptMainLoop() false: no frame drawn */
	if (++asked == close_at) { closed = true; gpu = false; return false; }
	return answers[asked] == 1;
}
void ui_format_when(char *out, size_t n, time_t when) { (void)when; snprintf(out, n, "Nov 10"); }
card_trouble_t cards_trouble(char *label, size_t n) { snprintf(label, n, "saves/Game.srm"); return trouble; }
bool cards_trouble_backup(hist_entry_t *e) { memset(e, 0, sizeof(*e)); return backup; }
bool cards_trouble_restore(const hist_entry_t *e) { (void)e; if (restored_ok) trouble = 0; return restored_ok; }
bool cards_retry(void) { return false; }
void cards_accept(void) { trouble = 0; }
void cards_end(void) { cards_ended++; }
void emu_osd(const char *t) { (void)t; }
void emu_osd_alert(const char *t) { (void)t; }
bool gspHasGpuRight(void) { return gpu; }
void log_printf(const char *f, ...) { (void)f; }
void log_soon(void) {}
void retro_unload_game(void);   /* the app's calls: the linker sends them to the guards */
void retro_deinit(void);
void video_game_end(void);
static void config_game_end(void) {}
static void audio_exit(void) { audio_ended++; }
static void clip_close(void) {}
static void cheats_unload(void) {}
#define PSX3D_COLD_FN(x)
''' + leave + r'''
static bool early_exit(void)
{
'''
suffix = r'''
	return false;
}
/* one game start: the k-th question is where the system closes the app (0: never) */
static void start(int kind, int has_backup, int k, const int *script, int n)
{
	asked = cards_ended = audio_ended = gpu_work = unloaded = 0;
	closed = false; gpu = true; core_initialized = true;
	trouble = kind; backup = has_backup; close_at = k;
	memset(answers, 0, sizeof(answers));
	for (int i = 0; i < n; i++) answers[i + 1] = script[i];
}
int main(void)
{
	/* the questions in production order; B (0) moves to the next one */
	static const struct { int kind, backup, questions; } flows[] = {
		{ CARD_TROUBLE_DAMAGED, 1, 2 },   /* Restore / Other choices, then Use an empty card / Quit */
		{ CARD_TROUBLE_DAMAGED, 0, 2 },   /* Try again / Other choices, then Use an empty card / Quit */
		{ CARD_TROUBLE_MEMORY, 0, 2 },    /* Try again / Other choices, then Play without saving / Quit */
	};
	int closes = 0;
	for (unsigned f = 0; f < sizeof(flows) / sizeof(flows[0]); f++)
		for (int k = 1; k <= flows[f].questions; k++) {
			static const int b_then_b[] = { 0, 0, 0, 0 };
			start(flows[f].kind, flows[f].backup, k, b_then_b, 4);
			assert(!cards_question());
			assert(closed && asked == k);              /* nothing asked or drawn after the close */
			assert(early_exit());
			assert(cards_ended == 1 && audio_ended == 1 && gpu_work == 0 && !core_initialized);
			closes++;
		}
	/* Quit with the GPU right (B, B): the whole teardown as before */
	static const int quit[] = { 0, 0 };
	start(CARD_TROUBLE_DAMAGED, 1, 0, quit, 2);
	assert(!cards_question() && asked == 2 && early_exit());
	assert(cards_ended == 1 && gpu_work == 3 && unloaded == 1 && !core_initialized);
	/* Restore fails, then Close at the question asked again */
	static const int restore[] = { 1 };
	restored_ok = false;
	start(CARD_TROUBLE_DAMAGED, 1, 2, restore, 1);
	assert(!cards_question() && asked == 2 && early_exit() && gpu_work == 0);
	restored_ok = true;
	/* answered: the game runs, no early exit */
	start(CARD_TROUBLE_DAMAGED, 1, 0, restore, 1);
	assert(cards_question() && asked == 1);
	printf("PS1 card question: Close at each of %d questions, no GPU work in the early exit PASS\n", closes);
	return 0;
}
'''
real = r'''
#include <assert.h>
#include <stdbool.h>
extern bool gspHasGpuRight(void);
extern int gpu_work, unloaded;
void retro_unload_game(void) { assert(gspHasGpuRight()); unloaded++; gpu_work++; }
void retro_deinit(void) { assert(gspHasGpuRight()); gpu_work++; }
void video_game_end(void) { assert(gspHasGpuRight()); gpu_work++; }
void retro_init(void) {}
'''
gba = (root / 'systems/gba/gba.c').read_text()
ga = gba.index('#define FOREIGN_TITLE')   # the foreign-save question first, then save_damaged_ask
gb = gba.index('\n}\n', gba.index('PSX3D_COLD int gba_save_damaged(')) + 3
ask = gba[ga:gb]
gba_prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include "bb_save.h"
#define PSX3D_COLD
static int asked, close_at;
static bool closed;
bool gba_save_declined;
static bool dev_remote_game(void) { return false; }
#define log_printf(...) ((void)0)
static bool ui_confirm_choice(const char *t, const char *m, const char *a, const char *b, bool g)
{ (void)t; (void)m; (void)a; (void)b; (void)g; if (closed) return false; if (++asked == close_at) { closed = true; return false; } return false; }
static void ui_format_when(char *out, size_t n, time_t when) { (void)when; snprintf(out, n, "Nov 10"); }
static int ui_confirm_pick_n(const char *t, const char *m, const char *const *a, int n, int lit)
{ (void)t; (void)m; (void)a; (void)n; (void)lit; ++asked; closed = true; return -1; }   /* HOME > Close */
'''
gba_suffix = r'''
int main(void)
{
	for (int clock = 0; clock < 2; clock++)
		for (int backup = 0; backup < 2 - clock; backup++)
			for (int k = 1; k <= 1 + backup; k++) {
				asked = 0; closed = false; close_at = k;
				bb_save_damage d = { .clock = clock, .backup = backup };
				assert(gba_save_damaged(&d) == BB_DAMAGE_BACK && gba_save_declined && asked == k);
			}
	for (int backup = 0; backup < 2; backup++) {   /* a save that may be from another emulator */
		asked = 0; closed = false;
		bb_save_damage d = { .backup = backup, .foreign = true };
		assert(gba_save_damaged(&d) == BB_DAMAGE_BACK && gba_save_declined && asked == 1);
	}
	printf("GBA damaged-save question: Close at each question is Back PASS\n");
	return 0;
}
'''
# GBA teardown after a refused start: every GPU call guarded (the review's existing path)
tear = gba[gba.index('gba: phase=teardown begin'):gba.index('prepare_failed:')]
assert 'video_gpu_idle' in tear
for line in tear.splitlines():
    if re.search(r'video_gpu_idle|C3D_\w+\(', line):
        assert 'gspHasGpuRight()' in line, line

with tempfile.TemporaryDirectory() as d:
    d = Path(d)
    (d / 'early.c').write_text(prefix + early + suffix)
    (d / 'gba.c').write_text(gba_prefix + ask + gba_suffix)
    flags = ['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
             '-fsanitize=address,undefined', f'-I{root}/include/breadbox', f'-I{root}/app/ui']
    stub = d / 'stub'
    stub.mkdir()
    for h in ('ui.h', 'ui_format.h', 'emu.h', 'cold_code.h', 'app_paths.h'):
        (stub / h).write_text('#include <stdbool.h>\n#include <stddef.h>\n#include <time.h>\n#define PSX3D_COLD\n#define PATH_LEN 512\n'
                              'bool ui_confirm_choice(const char *,const char *,const char *,const char *,bool);\n'
                              'void ui_format_when(char *,size_t,time_t);\nvoid emu_osd(const char *);\nvoid emu_osd_alert(const char *);\n')
    (d / 'real.c').write_text(real)
    # a copy, so its own folder's emu.h does not hide the stub
    question = d / 'card_question.c'
    question.write_text((root / 'systems/ps1/card_question.c').read_text())
    subprocess.run(flags[:1] + [f'-I{stub}'] + flags[1:] + [str(d / 'early.c'), str(d / 'real.c'),
                   str(question), str(root / 'app/ui/breadbox_handoff.c'),
                   '-Wl,--wrap=retro_init,--wrap=retro_deinit,--wrap=retro_unload_game,--wrap=video_game_end',
                   '-o', str(d / 'early')], check=True)
    subprocess.run([str(d / 'early')], check=True)
    subprocess.run(flags + [str(d / 'gba.c'), '-o', str(d / 'gba')], check=True)
    subprocess.run([str(d / 'gba')], check=True)
