/* The first-launch welcome's production logic (app/ui/welcome.c): the shown-once flag at a start, the page model
 * (A next, B back, START skips, X the games folder on card 2), the games card's words and the "Brought from PSX3D"
 * line for every receipt combination. The config.ini round trip (load, save, both resets) is tests/app/host. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "welcome.h"
#include "breadbox_import.h"
#include "ui_style.h"

int config_welcome = -1;
static int saves;
void config_save(void) { saves++; }

/* tests/ui_fit/strings.py's conservative model of the 3DS system font at the text floor (px 21) */
static float width(const char *s, void *ud)
{
	(void)ud;
	float w = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		if ((*p & 0xc0) == 0x80) continue;
		w += strchr("il.,:;'|!I ", *p) ? 6 : strchr("frtj()/-\"1", *p) ? 7 : strchr("MW", *p) || *p > 127 ? 16 :
			strchr("ABCDEFGHJKLNOPQRSTUVXYZmw+", *p) ? 13 : 10;
	}
	return w * 1.05f;
}
#define BOX (UI_ROOM_TEXT_R - (UI_ROOM_MARGIN_X + UI_PAPER_MARGIN_GAP))

static void boot(bool had_config, int key, int want, bool written)
{
	config_welcome = key;
	saves = 0;
	bb_welcome_boot(had_config, 0);
	assert(config_welcome == want && saves == (written ? 1 : 0));
}

int main(void)
{
	/* the flag: a first start shows it (written as 0 at once, so a start cut short shows it again); an update from
	 * a BreadBox without the key never shows it (written as seen); finish or skip writes 1; re-open changes nothing */
	boot(false, -1, 0, true);
	assert(bb_welcome_due());
	boot(true, 0, 0, false);
	assert(bb_welcome_due());
	boot(true, -1, 1, true);
	assert(!bb_welcome_due());
	boot(true, 1, 1, false);
	config_welcome = 0; saves = 0;
	bb_welcome_seen();
	assert(config_welcome == 1 && saves == 1 && !bb_welcome_due());
	bb_welcome_seen();
	assert(config_welcome == 1 && saves == 1);   /* Settings > Help > Welcome again: no change, no write */
	bb_welcome_boot(true, PSX_IMPORT_GAMES);
	assert(bb_welcome_boot_kinds() == PSX_IMPORT_GAMES);

	/* pages and keys */
	assert(bb_welcome_pages(0) == 2 && bb_welcome_pages(PSX_IMPORT_SAVES) == 3);
	assert(bb_welcome_step(0, 2, BB_WELCOME_KEY_A) == BB_WELCOME_NEXT);
	assert(bb_welcome_step(0, 2, BB_WELCOME_KEY_B) == BB_WELCOME_STAY);   /* B on card 1 does nothing */
	assert(bb_welcome_step(0, 2, BB_WELCOME_KEY_X) == BB_WELCOME_STAY);
	assert(bb_welcome_step(0, 3, BB_WELCOME_KEY_START) == BB_WELCOME_SKIP);
	assert(bb_welcome_step(1, 3, BB_WELCOME_KEY_START | BB_WELCOME_KEY_A) == BB_WELCOME_SKIP);
	assert(bb_welcome_step(1, 2, BB_WELCOME_KEY_A) == BB_WELCOME_DONE);
	assert(bb_welcome_step(1, 3, BB_WELCOME_KEY_A) == BB_WELCOME_NEXT);
	assert(bb_welcome_step(1, 2, BB_WELCOME_KEY_B) == BB_WELCOME_BACK);
	assert(bb_welcome_step(1, 2, BB_WELCOME_KEY_X) == BB_WELCOME_FOLDERS);
	assert(bb_welcome_step(2, 3, BB_WELCOME_KEY_X) == BB_WELCOME_STAY);
	assert(bb_welcome_step(2, 3, BB_WELCOME_KEY_A) == BB_WELCOME_DONE);
	assert(bb_welcome_step(2, 3, 0) == BB_WELCOME_STAY);

	/* the games card: unknown or no games "Add games" with the folder's own name (on its own line when "Put them
	 * in" and the name are too wide together); games: the count and as much of the folder's end as fits */
	char t[48], a[96], b[96];
	assert(bb_welcome_games(-1, "", t, sizeof t, a, b, sizeof a, width, NULL, BOX) == 1);
	assert(!strcmp(t, "Add games") && !strcmp(a, "Put them in roms") && !b[0]);
	bb_welcome_games(0, "sdmc:/roms/", t, sizeof t, a, b, sizeof a, width, NULL, BOX);
	assert(!strcmp(t, "Add games") && !strcmp(a, "Put them in roms"));
	assert(bb_welcome_games(-1, "sdmc:/Games/PlayStation", t, sizeof t, a, b, sizeof a, width, NULL, BOX) == 2);
	assert(!strcmp(a, "Put them in") && !strcmp(b, "PlayStation"));
	assert(bb_welcome_games(-1, "sdmc:/Games/PlayStation", t, sizeof t, a, b, sizeof a, NULL, NULL, 0) == 1 && !strcmp(a, "Put them in PlayStation"));
	bb_welcome_games(-1, "sdmc:/ROMS/NINTENDO/GAMEBOYADVANCEANDMORE", t, sizeof t, a, b, sizeof a, width, NULL, BOX);
	assert(width(a, NULL) <= BOX && width(b, NULL) <= BOX && !strncmp(b, "...", 3));
	/* the root chosen in the picker: one line is wider than the card (the v1 Old walk showed "Put them on the SD c..."),
	 * so two ruled lines, nothing cut; one line where it fits */
	assert(bb_welcome_games(0, "sdmc:/", t, sizeof t, a, b, sizeof a, width, NULL, BOX) == 2);
	assert(!strcmp(a, "Put them on") && !strcmp(b, "the SD card") && width(a, NULL) <= BOX && width(b, NULL) <= BOX);
	assert(bb_welcome_games(0, "sdmc:/", t, sizeof t, a, b, sizeof a, width, NULL, width("Put them on the SD card", NULL)) == 1);
	assert(!strcmp(a, "Put them on the SD card") && !b[0]);
	bb_welcome_games(5, "sdmc:/", t, sizeof t, a, b, sizeof a, width, NULL, BOX);
	assert(!strcmp(a, "in /"));
	bb_welcome_games(94, "sdmc:/roms/", t, sizeof t, a, b, sizeof a, width, NULL, BOX);
	assert(!strcmp(t, "94 games found") && !strcmp(a, "in /roms") && !b[0]);
	bb_welcome_games(1, "sdmc:/roms", t, sizeof t, a, b, sizeof a, width, NULL, BOX);
	assert(!strcmp(t, "1 game found"));
	bb_welcome_games(12, "sdmc:/a/very/long/folder/name/for/all/my/games/PlayStation/", t, sizeof t, a, b, sizeof a, width, NULL, BOX);
	assert(!strcmp(a, "in .../PlayStation"));
	bb_welcome_games(12, "sdmc:/ROMS/NINTENDO/GAMEBOYADVANCE", t, sizeof t, a, b, sizeof a, width, NULL, BOX);
	assert(width(a, NULL) <= BOX && !strncmp(a, "in ...", 6));

	/* the PSX3D line, every combination of the six kinds: only what came, in order, joined with ", " and "and",
	 * the first word capitalized, each line within the card's line; one line when the whole list fits, else the
	 * short form when all three of games, saves and settings came, then the same over two lines */
	static const char *const word[] = { "games", "saves", "postcards", "settings", "cheats", "BIOS" };
	enum { SHORT = PSX_IMPORT_GAMES | PSX_IMPORT_SAVES | PSX_IMPORT_SETTINGS };
	char l1[96], l2[96], full[96], none[96];
	int short_form = 0, more = 0, two = 0;
	for (unsigned k = 1; k < 64; k++) {
		int lines = bb_welcome_brought(l1, l2, sizeof l1, k, width, NULL, BOX);
		assert((lines == 2) == (l2[0] != 0) && l1[0] >= 'A' && l1[0] <= 'Z' && width(l1, NULL) <= BOX && width(l2, NULL) <= BOX);
		two += lines == 2;
		char line[200];
		snprintf(line, sizeof line, "%s%s%s", l1, lines == 2 ? " " : "", l2);
		bb_welcome_brought(full, none, sizeof full, k, NULL, NULL, 0);   /* unmeasured: the whole list, one line */
		assert(!none[0]);
		if (!strcmp(line, full)) {   /* the whole list */
			int total = 0;
			for (int i = 0; i < 6; i++) total += k >> i & 1;
			if (total > 1) assert(strstr(line, " and ") && !strstr(line, ", and"));
			if (lines == 2) assert(width(full, NULL) > BOX);   /* two lines only when one does not hold it */
			continue;
		}
		if (!strcmp(line, "Games, saves and settings.")) {   /* the short form: all three came, the whole list did not fit */
			assert((k & SHORT) == SHORT && width(full, NULL) > BOX);
			short_form++;
			continue;
		}
		assert(strstr(line, " and more."));   /* last resort: the first ones, never a cut word */
		more++;
		const char *at = line;
		for (int i = 0; i < 6; i++) {
			if (!(k >> i & 1)) continue;
			const char *f = strcasestr(at, word[i]);
			if (!f) break;
			at = f + strlen(word[i]);
		}
	}
	bb_welcome_brought(full, none, sizeof full, 63, NULL, NULL, 0);
	assert(!strcmp(full, "Games, saves, postcards, settings, cheats and BIOS."));
	bb_welcome_brought(full, none, sizeof full, PSX_IMPORT_SAVES | PSX_IMPORT_SETTINGS, NULL, NULL, 0);
	assert(!strcmp(full, "Saves and settings."));
	bb_welcome_brought(full, none, sizeof full, PSX_IMPORT_BIOS, NULL, NULL, 0);
	assert(!strcmp(full, "BIOS."));
	assert(bb_welcome_brought(l1, l2, sizeof l1, 63, width, NULL, BOX) == 2 && !strcmp(l1, "Games, saves and") && !strcmp(l2, "settings."));
	assert(bb_welcome_brought(l1, l2, sizeof l1, PSX_IMPORT_SAVES | PSX_IMPORT_POSTCARDS, width, NULL, BOX) == 1);
	printf("welcome: flag (first start, cut short, update, finish/skip, re-open), pages, games card, 63 PSX3D lines "
		"(%d on two lines, %d short form, %d \"and more\"): PASS\n", two, short_form, more);
	return 0;
}
