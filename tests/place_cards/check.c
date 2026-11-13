/* The room's place cards before A (app/ui/place_cards.c): the lines each card gets. Prints every line as
 * "card<TAB>label<TAB>value" for check.py's fit model and asserts the logic here. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "place_cards.h"
#include "ui_style.h"

/* tests/ui_fit/strings.py's conservative model of the 3DS system font at the text floor (px 21) */
static float model(const char *s, void *ud)
{
	(void)ud;
	float w = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		if ((*p & 0xc0) == 0x80) continue;
		w += strchr("il.,:;'|!I ", *p) ? 6 : strchr("frtj()/-\"1", *p) ? 7 : strchr("MW", *p) || *p > 127 ? 16 :
			strchr("ABCDEFGHJKLNOPQRSTUVXYZmw+", *p) ? 13 : 10;
	}
	return w * UI_ROOM_LINE_PX / 21 * 1.05f;
}
static const place_fit FIT = { model, NULL, UI_ROOM_TEXT_R - UI_ROOM_TEXT_X, UI_ROOM_GAP };

static void show(const char *card, const place_lines *l)
{
	for (int i = 0; i < l->n; i++) printf("%s\t%s\t%s\n", card, l->line[i].label, l->line[i].value);
}
static bool line(const place_lines *l, int i, const char *label, const char *value)
{
	return i < l->n && !strcmp(l->line[i].label, label) && !strcmp(l->line[i].value, value);
}

int main(int argc, char **argv)
{
	place_lines l;
	bool empty;
	char n[24];
	/* counts as players read them */
	place_count(n, sizeof(n), 0); assert(!strcmp(n, "0"));
	place_count(n, sizeof(n), 7); assert(!strcmp(n, "7"));
	place_count(n, sizeof(n), 999); assert(!strcmp(n, "999"));
	place_count(n, sizeof(n), 1000); assert(!strcmp(n, "1,000"));
	place_count(n, sizeof(n), 65535); assert(!strcmp(n, "65,535"));
	place_count(n, sizeof(n), 2147483647L); assert(!strcmp(n, "2,147,483,647"));
	place_count(n, sizeof(n), -3); assert(!strcmp(n, "0"));

	/* Collection: two systems, in the order given, the total, the favorites */
	place_system two[] = { { "Game Boy Advance", "GBA", 91 }, { "PlayStation", "PS1", 42 } };
	assert(place_collection(&l, &FIT, two, 2, 3, true, &empty) == 4 && !empty);
	assert(line(&l, 0, "Game Boy Advance", "91") && line(&l, 1, "PlayStation", "42") && line(&l, 2, "Total", "133") && line(&l, 3, "Favorites", "3"));
	show("collection", &l);
	/* one system only: no total that repeats it; no favorites line at 0 */
	place_system one[] = { { "Game Boy Advance", "GBA", 0 }, { "PlayStation", "PS1", 1 } };
	assert(place_collection(&l, &FIT, one, 2, 0, true, &empty) == 1 && !empty && line(&l, 0, "PlayStation", "1"));
	show("collection", &l);
	/* 0 games, known: no lines, empty (the card says where games go) */
	place_system none[] = { { "Game Boy Advance", "GBA", 0 }, { "PlayStation", "PS1", 0 } };
	assert(place_collection(&l, &FIT, none, 2, 5, true, &empty) == 0 && empty);
	/* the first scan still running: nothing yet is not "0 games" and not "empty": the card draws no line */
	assert(place_collection(&l, &FIT, none, 2, 5, false, &empty) == 0 && !empty && l.n == 0);
	/* ... but the counts found so far show while it runs */
	place_system partial[] = { { "Game Boy Advance", "GBA", 0 }, { "PlayStation", "PS1", 12 } };
	assert(place_collection(&l, &FIT, partial, 2, 0, false, &empty) == 1 && !empty && line(&l, 0, "PlayStation", "12"));
	/* huge numbers, every shipped system (NES too) */
	place_system huge[] = { { "Game Boy Advance", "GBA", 65535 }, { "PlayStation", "PS1", 65535 }, { "Nintendo Entertainment System", "NES", 65535 } };
	assert(place_collection(&l, &FIT, huge, 3, 65535, true, &empty) == 5 && line(&l, 3, "Total", "196,605"));
	assert(line(&l, 0, "GBA", "65,535") && line(&l, 1, "PlayStation", "65,535") && line(&l, 2, "NES", "65,535"));   /* the name when it fits, else its abbreviation */
	show("collection", &l);
	/* more systems than the card has lines for: at most PLACE_SYSTEMS_MAX, the total still counts them all */
	place_system many[6] = { { "A", "A", 1 }, { "B", "B", 1 }, { "C", "C", 1 }, { "D", "D", 1 }, { "E", "E", 1 }, { "F", "F", 1 } };
	assert(place_collection(&l, &FIT, many, 6, 1, true, &empty) == PLACE_LINES && line(&l, 4, "Total", "6") && line(&l, 5, "Favorites", "1"));

	/* Settings: at most 3 lines, nothing unknown */
	assert(place_settings(&l, "1.0.0", "Up to date", "Night") == 3);
	assert(line(&l, 0, "Version", "1.0.0") && line(&l, 1, "Up to date", "") && line(&l, 2, "Room style", "Night"));
	assert(place_settings(&l, "1.0.0", NULL, "Day") == 2 && line(&l, 1, "Room style", "Day"));
	assert(place_settings(&l, NULL, NULL, NULL) == 0);
	/* the shipped strings for check.py: every version, update state and style (argv: "settings" version update style) */
	for (int i = 1; i + 3 < argc + 1 && i + 3 <= argc; i += 4) {
		if (!strcmp(argv[i], "settings")) { place_settings(&l, argv[i + 1], argv[i + 2][0] ? argv[i + 2] : NULL, argv[i + 3]); show("settings", &l); }
		else if (!strcmp(argv[i], "system")) { for (int g = 1; g <= 100000; g *= 10) { place_system s[] = { { argv[i + 1], argv[i + 2], g } }; place_collection(&l, &FIT, s, 1, 0, true, &empty); show("collection", &l); } }
	}

	/* Quit: play-time spelling (ui_format_time), the last game on its own line */
	assert(place_quit(&l, &FIT, 0, NULL) == 0 && place_quit(&l, &FIT, 0, "") == 0);
	assert(place_quit(&l, &FIT, 30, NULL) == 1 && line(&l, 0, "Played today", "< 1 min"));
	assert(place_quit(&l, &FIT, 45 * 60, NULL) == 1 && line(&l, 0, "Played today", "45 min"));
	assert(place_quit(&l, &FIT, 80 * 60 + 59, "Crash Bandicoot") == 3 && line(&l, 0, "Today", "1 h 20 min"));   /* the long label does not fit */
	assert(line(&l, 1, "Last played", "") && line(&l, 2, "Crash Bandicoot", ""));
	show("quit", &l);
	assert(place_quit(&l, NULL, 80 * 60, NULL) == 1 && line(&l, 0, "Played today", "1 h 20 min"));   /* the label when it fits */
	assert(place_quit(&l, &FIT, 0, "Tetris") == 2 && line(&l, 0, "Last played", ""));
	/* a long name: two lines, broken at a space, each fits; one that fits stays one line */
	assert(place_quit(&l, &FIT, 0, "Kirby - Nightmare in Dream Land") == 3 && line(&l, 1, "Kirby - Nightmare in", "") && line(&l, 2, "Dream Land", ""));
	show("quit", &l);
	assert(place_quit(&l, &FIT, 60, "Castlevania - Symphony of the Night") == 4 && line(&l, 2, "Castlevania -", "") && line(&l, 3, "Symphony of the Night", ""));
	show("quit", &l);
	assert(place_quit(&l, &FIT, 0, "Supercalifragilisticexpialidociousnamewithoutspaces") == 2);   /* no space: one line, cut when drawn */
	assert(place_quit(&l, &FIT, 24 * 3600 - 1, NULL) == 1 && line(&l, 0, "Today", "23 h 59 min"));
	show("quit", &l);
	assert(place_quit(&l, &FIT, 4000000000u, NULL) == 1 && line(&l, 0, "Today", "23 h 59 min"));   /* a broken log: still a line that fits its buffer */
	show("quit", &l);
	fprintf(stderr, "place cards logic PASS\n");
	return 0;
}
