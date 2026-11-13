/* The folder picker's 3DS folders at the SD root (review fix r1b, finding 3): a clean card hides them, a game
 * one level down (DCIM/Games/x.gba) or right inside shows the folder, and a camera folder full of photos stays
 * hidden within the bounded look. The production predicate (ui_system_folder) and look (folder_peek.c). */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ui_format.h"
#include "folder_peek.h"
static unsigned looked;
static bool is_game(const char *name)
{
	looked++;
	const char *dot = strrchr(name, '.');
	return dot && (!strcasecmp(dot, ".gba") || !strcasecmp(dot, ".cue") || !strcasecmp(dot, ".chd"));
}
static void touch(const char *path) { FILE *f = fopen(path, "w"); assert(f); fclose(f); }
static void dir(const char *path) { assert(!mkdir(path, 0700)); }
/* the picker's rule at the root: hidden = one of the 3DS's own and no game in a shallow look */
static bool hidden(const char *name) { return ui_system_folder("sdmc:/", name) && !folder_has_games(name, is_game); }
int main(void)
{
	const char *base = getenv("TMPDIR");
	char tmpl[512];
	snprintf(tmpl, sizeof(tmpl), "%s/bb-folder-peek-XXXXXX", base && base[0] ? base : "/tmp");
	assert(mkdtemp(tmpl) && !chdir(tmpl));
	/* a clean card: the 3DS's own folders, nothing to play in them */
	dir("Nintendo 3DS"); dir("Nintendo 3DS/0123456789abcdef0123456789abcdef"); dir("Nintendo 3DS/0123456789abcdef0123456789abcdef/fedcba9876543210fedcba9876543210");
	dir("DCIM"); dir("DCIM/100NIN03");
	for (int i = 0; i < 400; i++) { char p[64]; snprintf(p, sizeof(p), "DCIM/100NIN03/HNI_%04d.JPG", i); touch(p); }
	dir("3ds"); dir("3ds/BreadBox"); touch("3ds/BreadBox/BreadBox.3dsx"); dir("luma"); touch("luma/config.ini"); dir("gm9"); dir("private");
	dir("roms");
	const char *own[] = { "Nintendo 3DS", "DCIM", "3ds", "luma", "gm9", "private" };
	for (int i = 0; i < 6; i++) assert(hidden(own[i]));
	assert(!hidden("roms"));
	assert(looked <= (FOLDER_PEEK_TOP + FOLDER_PEEK_DIRS * FOLDER_PEEK_SUB) * 6);   /* bounded beside 400 photos */
	/* a game one level down shows the folder: DCIM/Games */
	dir("DCIM/Games"); touch("DCIM/Games/Advance Wars (USA).gba");
	assert(!hidden("DCIM") && hidden("Nintendo 3DS"));
	/* and right inside */
	touch("luma/Crash.cue");
	assert(!hidden("luma"));
	/* deeper than one level is not looked for (the picker stays quick): still hidden */
	dir("gm9/a"); dir("gm9/a/b"); touch("gm9/a/b/deep.gba");
	assert(hidden("gm9"));
	char rm[600];
	snprintf(rm, sizeof(rm), "rm -rf '%s'", tmpl);
	assert(!chdir("/") && !system(rm));
	puts("folder picker: the 3DS's own root folders hidden on a clean card, shown with a game inside or one level down (DCIM/Games), bounded look: PASS");
	return 0;
}
