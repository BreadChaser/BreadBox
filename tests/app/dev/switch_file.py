#!/usr/bin/env python3
"""BreadBox reads only its own switches.txt. A PSX3D switches.txt alone must leave the remote off
(a former PSX3D tester once had the remote on, and updates silently off, without knowing).
The production parse_switch_file() is extracted from app/ui/main.c and run against real files;
APP_LEGACY_PSX3D_ROOT is defined here so any code that reached for the PSX3D file would find it."""
import subprocess, sys, tempfile
from pathlib import Path
root = Path(__file__).resolve().parents[3]
src = (root / 'app/ui/main.c').read_text()
a = src.index('static void parse_switch_file(void)')
fn = src[a:src.index('\nfloat slider_get', a)]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define PSX3D_DIR "bb"
#define APP_LEGACY_PSX3D_ROOT "psx"
static bool dev_remote, dev_bench, dev_bench_home, authorized;
static char dev_token[96], dev_update_url[256];
static int parsed;
static void parse_switches(int argc, char **argv) { (void)argc; (void)argv; parsed++; }
static void dev_authorize(void) { authorized = true; }
''' + fn + r'''
static void reset(void) { dev_remote = dev_bench = dev_bench_home = authorized = false; dev_token[0] = 0; parsed = 0; }
static void put(const char *path, const char *text) { FILE *f = fopen(path, "w"); assert(f); fputs(text, f); fclose(f); }
int main(void)
{
	/* no files at all: nothing is on */
	reset(); parse_switch_file();
	assert(!dev_remote && !dev_token[0] && !authorized);
	/* PSX3D's file alone: the remote stays off, no token, nothing parsed */
	put("psx/switches.txt", "remote token=psx-fake nosound");
	reset(); parse_switch_file();
	assert(!dev_remote && !dev_token[0] && !authorized && !parsed);
	/* BreadBox's own file turns it on, with its own token */
	put("bb/switches.txt", "remote token=bb-fake");
	reset(); parse_switch_file();
	assert(dev_remote && !strcmp(dev_token, "bb-fake") && authorized);
	/* and still wins when PSX3D's file is there too */
	reset(); parse_switch_file();
	assert(!strcmp(dev_token, "bb-fake"));
	puts("switches.txt: only BreadBox's own file counts: PASS");
	return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    d = Path(d)
    (d / 'bb').mkdir(); (d / 'psx').mkdir()
    (d / 't.c').write_text(harness)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(d / 't.c'), '-o', str(d / 't')], check=True)
    r = subprocess.run([str(d / 't')], cwd=d)
    sys.exit(r.returncode)
