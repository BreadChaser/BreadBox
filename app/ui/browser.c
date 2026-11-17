#include "app_policy.h"
#define _GNU_SOURCE   // strcasestr
// Game browser. Reads one folder at a time with readdir() and never stats
// files or asks the card for its free space, so it stays fast on huge
// (1TB+) SD cards and folders with thousands of files.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "theme.h"
#include "ui_format.h"
#include "folder_peek.h"
#include "app_paths.h"
#include "app_log.h"
#include "app_config.h"
#include "platform_video.h"
#include "app_input.h"
#include "ui_list.h"
#include "ui_pause.h"
#include "ui.h"
#include "ui_trace_card.h"
#include "library.h"
#include "uisound.h"
#include "libui.h"
#include "games_dir.h"
#include "browser.h"
#include "app_main.h"
#define LIST_HEADER ui_list_context
#define LIST_BACK(k) (k)
#include "scene.h"
#include "devtools.h"
#include "bb_index.h"
#include "breadbox_formats.h"

#define MAX_ENTRIES 4096
#define ROWS UI_LIST_ROWS
#define ROW_H UI_LIST_ROW
#define LIST_Y UI_LIST_TOP

typedef struct {
	char *name;
	bool dir;
} entry_t;

static entry_t entries[MAX_ENTRIES];
static int nentries, sel;   // sel: list row; row 0 is "Use this folder"
static flist_t lst;
static bool truncated;
static char cwd[PATH_LEN];
static char msg[64];
static int msg_frames;


static bool is_game(const char *name)
{
	return bb_formats_match(name);   // every registered system's file types (bb_systems[], breadbox_formats.c)
}

// one of the 3DS's own folders at the root shows when a shallow look finds a game in it (DCIM/Games/...)
static bool system_folder_has_games(const char *name)
{
	char path[PATH_LEN];
	snprintf(path, sizeof(path), "%s%s%s", cwd, cwd[0] && cwd[strlen(cwd) - 1] == '/' ? "" : "/", name);
	return folder_has_games(path, is_game);
}

static int cmp(const void *a, const void *b)
{
	const entry_t *x = a, *y = b;
	if (x->dir != y->dir)
		return x->dir ? -1 : 1;
	return strcasecmp(x->name, y->name);
}

static void free_entries(void)
{
	for (int i = 0; i < nentries; i++)
		free(entries[i].name);
	nentries = 0;
}

// .bin files next to a .cue of the same name are just clutter; hide them
static bool has_cue_sibling(const char *name)
{
	char want[PATH_LEN];
	const char *dot = strrchr(name, '.');
	if (!dot || strcasecmp(dot, ".bin") != 0)
		return false;
	snprintf(want, sizeof(want), "%.*s.cue", (int)(dot - name), name);
	for (int i = 0; i < nentries; i++)
		if (!entries[i].dir && strcasecmp(entries[i].name, want) == 0)
			return true;
	return false;
}

static void load_dir(const char *path)
{
	DIR *d;
	struct dirent *de;

	free_entries();
	truncated = false;
	sel = 0;
	lst.init = false;
	snprintf(cwd, sizeof(cwd), "%s", path);

	d = opendir(cwd);
	if (!d) {
		log_printf("opendir(%s) failed\n", cwd);
		return;
	}
	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		bool dir = de->d_type == DT_DIR;
		if (!dir && !is_game(de->d_name))
			continue;
		if (dir && ui_system_folder(cwd, de->d_name) && !system_folder_has_games(de->d_name))   // the 3DS's own: hidden
			continue;
		if (nentries == MAX_ENTRIES) {
			truncated = true;
			break;
		}
		entries[nentries].name = strdup(de->d_name);
		entries[nentries].dir = dir;
		nentries++;
	}
	closedir(d);

	// drop .bin tracks that belong to a .cue
	int j = 0;
	for (int i = 0; i < nentries; i++) {
		if (!entries[i].dir && has_cue_sibling(entries[i].name)) {
			free(entries[i].name);
			continue;
		}
		entries[j++] = entries[i];
	}
	nentries = j;
	qsort(entries, nentries, sizeof(entries[0]), cmp);
}

static void join(char *out, size_t len, const char *dir, const char *name)
{
	size_t n = strlen(dir);
/* Bounded truncation is intentional; preserve the validated release code layout. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
	snprintf(out, len, "%s%s%s", dir, (n && dir[n - 1] == '/') ? "" : "/", name);
#pragma GCC diagnostic pop
}

static void select_name(const char *name)
{
	for (int i = 0; i < nentries; i++)
		if (strcmp(entries[i].name, name) == 0) {
			sel = i + 1;
			flist_snap(&lst, sel, nentries + 1, ROWS);
			return;
		}
}

// up one folder, keeping the cursor on the folder we came from
static void go_up(void)
{
	char *slash, child[PATH_LEN];
	size_t len = strlen(cwd);

	if (len && cwd[len - 1] == '/')
		cwd[--len] = 0;
	slash = strrchr(cwd, '/');
	if (!slash)
		return;
	snprintf(child, sizeof(child), "%s", slash + 1);
	if (slash[-1] == ':') {
		load_dir("sdmc:/");
	} else {
		*slash = 0;
		char parent[PATH_LEN];
		snprintf(parent, sizeof(parent), "%s", cwd);
		load_dir(parent);
	}
	select_name(child);
}

static bool dir_exists(const char *path)
{
	DIR *d = opendir(path);
	if (d)
		closedir(d);
	return d != NULL;
}

// First run: find the PS1 games without asking. The usual folders first,
// then any folder on the card or in sdmc:/roms named like PlayStation, then
// sdmc:/roms itself; a folder counts only when the scan finds PS1 discs in it
// (other systems' folders and images are left out, library.c). Returns true
// with games_dir set and the library loaded.
static bool ps1_name(const char *n)
{
	return strcasestr(n, "psx") || strcasestr(n, "ps1") || strcasestr(n, "playstation") || strcasestr(n, "psone");
}

bool library_guess_dir(void)
{
	static const char *const guesses[] = {
		"sdmc:/roms/psx", "sdmc:/roms/ps1", "sdmc:/roms/playstation", "sdmc:/psx", "sdmc:/ps1",
	};
	char (*found)[PATH_LEN] = malloc(24 * PATH_LEN);   // once, at first start
	int n = 0;
	if (!found)
		return false;
	for (size_t i = 0; i < sizeof(guesses) / sizeof(guesses[0]); i++)
		if (dir_exists(guesses[i]))
			snprintf(found[n++], PATH_LEN, "%s", guesses[i]);
	static const char *const parents[] = { "sdmc:/roms", "sdmc:" };
	for (int p = 0; p < 2; p++) {
		DIR *d = opendir(p ? "sdmc:/" : parents[p]);
		struct dirent *de;
		while (d && n < 22 && (de = readdir(d)))
			if (de->d_name[0] != '.' && ps1_name(de->d_name)) {
				char path[PATH_LEN];
				snprintf(path, sizeof(path), "%s/%s", parents[p], de->d_name);
				bool dup = false;
				for (int k = 0; k < n; k++)
					dup |= !strcasecmp(found[k], path);
				if (!dup && dir_exists(path))
					snprintf(found[n++], PATH_LEN, "%s", path);
			}
		if (d)
			closedir(d);
	}
	if (dir_exists("sdmc:/roms"))
		snprintf(found[n++], PATH_LEN, "sdmc:/roms");
	// A usable snapshot may precede validation; Home polls for later publications.
	for (int i = 0; i < n; i++)
		if (library_load(found[i], true)) {
			log_printf("library: first run, usable list in %s (validation may still be running)\n", found[i]);
			snprintf(g_cfg.games_dir, sizeof(g_cfg.games_dir), "%.*s", (int)sizeof(g_cfg.games_dir) - 1, found[i]);
			config_save();
			free(found);
			return true;
		}
	free(found);
	log_printf("library: first run, no PS1 games found (%d folders tried)\n", n);
	library_load(NULL, false);   // nothing listed
	return false;
}

// a folder holding exactly one game and nothing else: that game
static bool single_game(char *out, size_t len)
{
	if (nentries != 1 || entries[0].dir)
		return false;
	join(out, len, cwd, entries[0].name);
	return true;
}


static void draw_top(void *ud); /* rendered by the shared browser component */

static const char *row_label(int i, void *ud); /* shared presentation */

static const char *row_value(int i, void *ud)
{
	(void)ud;
	if (!i)
		return strcmp(cwd, g_cfg.games_dir) ? NULL : "In use";
	return entries[i - 1].dir ? ">" : "Play";
}

// B Up (Back at the top of the card), Y the games folder, START Done, A Open
static int browser_bar(ui_button_t *b)
{
	int n = 0;
	bool root = !strcmp(cwd, "sdmc:/");
	if (!root || g_cfg.games_dir[0])
		b[n++] = (ui_button_t){KEY_B, root ? "Back" : "Up", root ? UI_ICON_BACK : UI_ICON_UP, false};
	if (g_cfg.games_dir[0] && strcmp(cwd, g_cfg.games_dir))
		b[n++] = (ui_button_t){KEY_Y, "Games", UI_ICON_FOLDER, false};
	b[n++] = (ui_button_t){KEY_START, "Done", UI_ICON_EXIT, false};
	b[n++] = !sel ? (ui_button_t){KEY_A, "Use", UI_ICON_CHECK, false}
		: sel <= nentries && entries[sel - 1].dir ? (ui_button_t){KEY_A, "Open", UI_ICON_OPEN, false}
		: (ui_button_t){KEY_A, "Play", UI_ICON_PLAY, false};
	return n;
}

static void draw_bottom(void *ud); /* rendered by the shared browser component */

// remember where to come back to: the folder and the entry that was picked
static char back_sel[PATH_LEN];

static bool launch(char *out_path, size_t out_len, const char *path,
	const char *from_dir, const char *from_entry)
{
	sfx_play(SFX_LAUNCH);   // a game starts: its sound now, as from the shelf
	snprintf(out_path, out_len, "%s", path);
	snprintf(g_cfg.last_dir, sizeof(g_cfg.last_dir), "%s", from_dir);
	snprintf(back_sel, sizeof(back_sel), "%s", from_entry);
	config_save();
	free_entries();
	return true;
}

bool browser_run(char *out_path, size_t out_len)
{
	bool remote_library = false;
	bool auto_launch = false, auto_sent = false;
	char auto_path[PATH_LEN], auto_from[PATH_LEN], auto_entry[PATH_LEN];

	// Back from a game started here: that folder. Otherwise the games folder.
	if (back_sel[0] && dir_exists(g_cfg.last_dir)) {
		load_dir(g_cfg.last_dir);
		select_name(back_sel);
	} else if (g_cfg.games_dir[0] && dir_exists(g_cfg.games_dir))
		load_dir(g_cfg.games_dir);
	else
		load_dir("sdmc:/");

	while (aptMainLoop()) {
		if (dev_tick(false))
			return false;
		if (dev_ui_take() == DEV_UI_LIBRARY) {
			remote_library = true;
		}
		input_poll();
		ui_button_t bar[4];
		u32 k = LIST_BACK(ui_bar_input(bar, browser_bar(bar), input_keys_down())), h = input_keys_held();
		int n = nentries + 1, before = sel;

 k=ui_list_input(&lst,n,&sel,k,true);
 before=sel;
 if(k&KEY_R){sel+=UI_LIST_ROWS;if(sel>=n)sel=n-1;}
 if(k&KEY_L){sel-=UI_LIST_ROWS;if(sel<0)sel=0;}
		if (sel != before)
			sfx_play(SFX_JUMP);   // a page at once (ui_list_input made the one-row moves)
		if (remote_library) { k |= UI_SWAP_REMOTE; remote_library = false; auto_launch = false; }
		else if (auto_launch && !auto_sent) { k |= UI_SWAP_REMOTE; auto_sent = true; }
		bool launches_on_a = sel && sel <= nentries && !entries[sel - 1].dir;
		k = ui_swap_keys(k, UI_SWAP_REMOTE | (launches_on_a ? KEY_A : 0));
		if (k & KEY_START) break;
		if (k & UI_SWAP_REMOTE) {
			if (auto_launch) return launch(out_path, out_len, auto_path, auto_from, auto_entry);
			break;
		}

		if (k & KEY_Y) {
			if (g_cfg.games_dir[0] && dir_exists(g_cfg.games_dir)) {
				load_dir(g_cfg.games_dir);
			} else {
				snprintf(msg, sizeof(msg), "No games folder yet: choose one first");
				msg_frames = 150;
			}
		} else if (k & KEY_B) {
			if (strcmp(cwd, "sdmc:/"))
				go_up();
			else if (g_cfg.games_dir[0])
				break;   // nothing above the card: back to the library
			else
				sfx_cancel();   // the card's root and no library to go back to: nothing happened
		} else if ((k & KEY_A) && !sel) {
			// the library loads this folder when the browser closes
			bb_games_dir_chosen();   // a folder was chosen: the saved one that could not be read is replaced
			char root[PATH_LEN];
			if (bb_index_root_normalize(cwd, root, sizeof(root)))
				snprintf(g_cfg.games_dir, sizeof(g_cfg.games_dir), "%s", root);
			else
				snprintf(g_cfg.games_dir, sizeof(g_cfg.games_dir), "%s", cwd);
			log_printf("library: selected common root %s\n", g_cfg.games_dir);
			config_save();
			break;
		} else if (k & KEY_A) {
			const entry_t *e = &entries[sel - 1];
			char path[PATH_LEN], from[PATH_LEN], name[PATH_LEN];
			join(path, sizeof(path), cwd, e->name);
			snprintf(from, sizeof(from), "%s", cwd);
			snprintf(name, sizeof(name), "%s", e->name);
			if (e->dir) {
				load_dir(path);
				// one game per folder (the usual layout): just start it
				char game[PATH_LEN];
				if (single_game(game, sizeof(game))) {
					// This directory step stays in place. Queue its automatic game launch
					// through the same polling/present loop before leaving the browser.
					snprintf(auto_path, sizeof(auto_path), "%s", game);
					snprintf(auto_from, sizeof(auto_from), "%s", from);
					snprintf(auto_entry, sizeof(auto_entry), "%s", name);
					auto_launch = true; auto_sent = false;
				}
			} else {
				return launch(out_path, out_len, path, from, name);
			}
		}

		flist_update(&lst, sel, n, ROWS);
		ui_begin();
		video_present_vsync(false, draw_top, draw_bottom, NULL);
	}
	back_sel[0] = 0;
	free_entries();
	return false;
}

#include "components/browser.inc"
