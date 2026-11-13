// The first-launch welcome's flag and words (welcome.h). The tour itself is home_welcome.inc.
#include <stdio.h>
#include <string.h>
#include "cold_code.h"
#include "app_config.h"
#include "ui_format.h"
#include "breadbox_import.h"
#include "welcome.h"

static unsigned boot_kinds;

PSX3D_COLD int bb_welcome_boot_key(bool had_config, int key)
{
	if (!had_config) return 0;   // a first start: the tour waits for the room
	return key == 0 ? 0 : 1;     // no key: made by a BreadBox before the welcome (an update), seen
}

PSX3D_COLD void bb_welcome_boot(bool had_config, unsigned brought)
{
	boot_kinds = brought;
	int key = bb_welcome_boot_key(had_config, config_welcome);
	if (key == config_welcome) return;
	config_welcome = key;
	config_save();
}

PSX3D_COLD bool bb_welcome_due(void) { return config_welcome == 0; }
PSX3D_COLD unsigned bb_welcome_boot_kinds(void) { return boot_kinds; }

PSX3D_COLD void bb_welcome_seen(void)
{
	if (config_welcome == 1) return;   // a re-open: nothing changes
	config_welcome = 1;
	config_save();
}

PSX3D_COLD int bb_welcome_pages(unsigned brought) { return brought ? 3 : 2; }

PSX3D_COLD int bb_welcome_step(int page, int pages, unsigned keys)
{
	if (keys & BB_WELCOME_KEY_START) return BB_WELCOME_SKIP;
	if (keys & BB_WELCOME_KEY_A) return page + 1 < pages ? BB_WELCOME_NEXT : BB_WELCOME_DONE;
	if (keys & BB_WELCOME_KEY_B) return page > 0 ? BB_WELCOME_BACK : BB_WELCOME_STAY;
	if ((keys & BB_WELCOME_KEY_X) && page == 1) return BB_WELCOME_FOLDERS;
	return BB_WELCOME_STAY;
}

static bool fits(const char *s, bb_welcome_measure measure, void *ud, float box) { return !measure || measure(s, ud) <= box; }

PSX3D_COLD int bb_welcome_games(int count, const char *games_dir, char *title, size_t title_cap, char *l1, char *l2, size_t cap,
	bb_welcome_measure measure, void *ud, float box)
{
	char dir[UI_GAMES_DIR_CAP];
	l2[0] = 0;
	if (count > 0) {   // "in /roms": the folder's end, as much of it as fits the line
		snprintf(title, title_cap, "%d game%s found", count, count == 1 ? "" : "s");
		for (int chars = UI_EMPTY_TAIL; chars >= 4; chars--) {
			ui_games_dir_tail(dir, games_dir, chars);
			snprintf(l1, cap, "in %s", dir);
			if (fits(l1, measure, ud, box)) break;
		}
		return 1;
	}
	snprintf(title, title_cap, "%s", BB_WELCOME_ADD);
	// the folder's own name ("roms"): its last characters when it is long
	const char *d = games_dir && games_dir[0] ? games_dir : UI_EMPTY_FOLDER;
	size_t len = strlen(d);
	while (len > 1 && d[len - 1] == '/') len--;
	size_t at = len;
	while (at > 0 && d[at - 1] != '/' && d[at - 1] != ':') at--;
	char name[UI_GAMES_DIR_CAP];
	if (at == len) {   // the card's root itself: no folder name to say (two ruled lines when one is too narrow)
		snprintf(l1, cap, "Put them on the SD card");
		if (fits(l1, measure, ud, box)) return 1;
		snprintf(l1, cap, "Put them on");
		snprintf(l2, cap, "the SD card");
		return 2;
	}
	for (int chars = UI_EMPTY_TAIL; chars >= 4; chars--) {
		ui_path_tail_n(name, sizeof(name), d + at, len - at, chars);
		snprintf(l1, cap, "Put them in %s", name);
		if (fits(l1, measure, ud, box)) return 1;
		if (fits(name, measure, ud, box)) {   // the name on the next ruled line
			snprintf(l1, cap, "Put them in");
			snprintf(l2, cap, "%s", name);
			return 2;
		}
	}
	return l2[0] ? 2 : 1;
}

// the kinds' words in PSX_IMPORT_* bit order (user-facing prose: one word each)
static const char kind_words[] = "games saves postcards settings cheats BIOS";
#define KINDS 6
static PSX3D_COLD const char *kind_name(int i, char *out, size_t cap)
{
	const char *w = kind_words;
	for (; i > 0 && *w; i--) w = strchr(w, ' ') ? strchr(w, ' ') + 1 : w + strlen(w);
	size_t n = strcspn(w, " ");
	snprintf(out, cap, "%.*s", (int)n, w);
	return out;
}

// the first `limit` kinds of `kinds` joined with ", " and " and " (more: " and more"), the first one capitalized
static PSX3D_COLD void join(char *out, size_t cap, unsigned kinds, int limit)
{
	int n = 0, total = 0;
	for (int i = 0; i < KINDS; i++) total += (kinds >> i) & 1;
	bool more = limit < total;
	size_t len = 0;
	out[0] = 0;
	for (int i = 0; i < KINDS && n < limit; i++) {
		if (!(kinds >> i & 1)) continue;
		const char *sep = !n ? "" : (!more && n + 1 == (total < limit ? total : limit)) ? " and " : ", ";
		char word[16];
		int w = snprintf(out + len, cap - len, "%s%s", sep, kind_name(i, word, sizeof(word)));
		if (w < 0 || (size_t)w >= cap - len) return;
		len += (size_t)w;
		n++;
	}
	snprintf(out + len, cap - len, more ? " and more." : ".");
	if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 'a' + 'A');
}

// text over at most `lines` lines of box (broken after a word, like the card's own text): l1 and l2 (or "")
static PSX3D_COLD bool wrap(const char *text, char *l1, char *l2, size_t cap, int lines, bb_welcome_measure measure, void *ud, float box)
{
	snprintf(l1, cap, "%s", text);
	l2[0] = 0;
	if (!measure || measure(l1, ud) <= box) return true;
	if (lines < 2) return false;
	for (const char *sp = strrchr(text, ' '); sp; ) {   // the longest first line that fits, the rest on the second
		snprintf(l1, cap, "%.*s", (int)(sp - text), text);
		if (measure(l1, ud) <= box) {
			snprintf(l2, cap, "%s", sp + 1);
			return measure(l2, ud) <= box;
		}
		const char *prev = sp;
		sp = NULL;
		for (const char *q = text; q < prev; q++) if (*q == ' ') sp = q;
	}
	return false;
}

PSX3D_COLD int bb_welcome_brought(char *l1, char *l2, size_t cap, unsigned kinds, bb_welcome_measure measure, void *ud, float box)
{
	enum { SHORT = PSX_IMPORT_GAMES | PSX_IMPORT_SAVES | PSX_IMPORT_SETTINGS };
	char text[128];
	kinds &= (1u << KINDS) - 1;
	bool short_ok = (kinds & SHORT) == SHORT && kinds != SHORT;
	// one line: the whole list, else the short form; two lines: the same; last, the first ones "and more"
	for (int lines = 1; lines <= 2; lines++) {
		join(text, sizeof(text), kinds, KINDS);
		if (wrap(text, l1, l2, cap, lines, measure, ud, box)) return l2[0] ? 2 : 1;
		if (short_ok) {
			join(text, sizeof(text), SHORT, KINDS);
			if (wrap(text, l1, l2, cap, lines, measure, ud, box)) return l2[0] ? 2 : 1;
		}
	}
	for (int limit = KINDS - 1; limit >= 1; limit--) {   // never a cut word: fewer of them
		join(text, sizeof(text), kinds, limit);
		if (wrap(text, l1, l2, cap, 2, measure, ud, box)) break;
	}
	return l2[0] ? 2 : 1;
}
