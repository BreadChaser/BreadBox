/* include/breadbox/ui_format.h: one spelling for play time, game counts, game names and a cut path. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui_format.h"

static const char *time_of(unsigned s) { static char t[24]; ui_format_time(t, sizeof t, s); return t; }
static const char *games_of(int n) { static char t[24]; ui_format_games(t, sizeof t, n); return t; }
static const char *clean(const char *in) { static char t[256]; ui_clean_title(in, t, sizeof t); return t; }
static const char *tail(const char *in, int chars) { static char t[256]; ui_path_tail(t, sizeof t, in, chars); return t; }
/* whole UTF-8 characters only (review r1b R1b-5: a cut inside a four-byte character) */
static int utf8_ok(const char *s)
{
	for (const unsigned char *p = (const unsigned char *)s; *p;) {
		int k = *p < 0x80 ? 1 : (*p & 0xe0) == 0xc0 ? 2 : (*p & 0xf0) == 0xe0 ? 3 : (*p & 0xf8) == 0xf0 ? 4 : 0;
		if (!k) return 0;
		for (int i = 1; i < k; i++) if ((p[i] & 0xc0) != 0x80) return 0;
		p += k;
	}
	return 1;
}
#define EMOJI "\xf0\x9f\x98\x80"
#define CJK "\xe3\x81\x82"
/* every capacity from 0 up: whole characters, never past cap, a prefix of the roomy answer */
static void every_cap(const char *dir, bool at_library)
{
	char full[256], t[256];
	ui_empty_help(full, sizeof full, dir, at_library);
	assert(utf8_ok(full) && strstr(full, "Put games in ") == full);
	for (size_t cap = 0; cap <= strlen(full) + 1; cap++) {
		memset(t, 'x', sizeof t);
		ui_empty_help(t, cap, dir, at_library);
		assert(t[cap] == 'x');   // nothing written past cap
		if (cap) assert(utf8_ok(t) && strlen(t) < cap && !strncmp(t, full, strlen(t)) && strlen(t) + 4 >= (cap - 1 < strlen(full) ? cap - 1 : strlen(full)));
		memset(t, 'x', sizeof t);
		ui_path_tail(t, cap, dir, UI_EMPTY_TAIL);
		assert(t[cap] == 'x');
		if (cap) assert(utf8_ok(t) && strlen(t) < cap);
	}
}

int main(void)
{
	assert(!strcmp(time_of(0), "< 1 min") && !strcmp(time_of(59), "< 1 min"));
	assert(!strcmp(time_of(60), "1 min") && !strcmp(time_of(3599), "59 min"));
	assert(!strcmp(time_of(3600), "1 h 00 min") && !strcmp(time_of(3600 + 125 * 60), "3 h 05 min"));
	assert(!strcmp(games_of(0), "0 games") && !strcmp(games_of(1), "1 game") && !strcmp(games_of(24), "24 games"));
	// a raw file stem saved by an older build, and the same game the library lists
	const char *raw = "Game_Boy_Advance_Video_-_Pokemon_-_Volume_1";
	assert(!strcmp(clean(raw), "Game Boy Advance Video - Pokemon - Volume 1"));
	assert(!strcmp(clean("Game_Boy_Advance_Video_-_Pokemon_-_Volume_1.gba"), clean(raw)));
	assert(!strcmp(clean("Metroid Fusion (USA).gba"), "Metroid Fusion"));
	assert(!strcmp(clean("Legend of Zelda, The.gba"), "The Legend of Zelda"));
	// already clean: nothing more is cut (a dot inside a name is not a file type)
	const char *same[] = { "Metroid Fusion", "Dr. Mario", "Final Fantasy VII Vol. 2", "Pok\xc3\xa9mon Emerald", "Ape Escape" };
	for (unsigned i = 0; i < sizeof same / sizeof *same; i++) assert(!strcmp(clean(same[i]), same[i]));
	assert(!strcmp(clean(clean(raw)), clean(raw)));
	// a long path keeps its end, whole characters only
	assert(!strcmp(tail("sdmc:/games", 30), "sdmc:/games"));
	assert(!strcmp(tail("sdmc:/games/WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", 10), "...WWWWWWWWWW"));
	assert(!strcmp(tail("sdmc:/\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9", 3), "...\xc3\xa9\xc3\xa9\xc3\xa9"));
	assert(!strcmp(tail("sdmc:/\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", 2), "...\xe6\x9c\xac\xe8\xaa\x9e"));
	// a name that does not fit is cut between characters, never inside one
	{
		char t[8];
		ui_clean_title("\xe3\x82\xb2\xe3\x83\xbc\xe3\x83\xa0\xe3\x81\x8c", t, sizeof t);   // 3-byte characters, 7 bytes of room
		assert(!strcmp(t, "\xe3\x82\xb2\xe3\x83\xbc"));
		ui_copy_text(t, sizeof t, "ab\xc3\xa9\xc3\xa9\xc3\xa9");   // 2-byte characters
		assert(!strcmp(t, "ab\xc3\xa9\xc3\xa9\xc3\xa9") || !strcmp(t, "ab\xc3\xa9\xc3\xa9"));
	}
	// the empty library names the folder games go in (F01: it used to name a "Rescan" button that is not there)
	{
		char t[96];
		ui_empty_help(t, sizeof t, "", false);
		assert(!strcmp(t, "Put games in /roms on the SD card."));
		ui_empty_help(t, sizeof t, "sdmc:/roms/", true);
		assert(!strcmp(t, "Put games in /roms. Hold Y to look again."));
		ui_empty_help(t, sizeof t, "sdmc:/roms/gba", false);
		assert(!strcmp(t, "Put games in /roms/gba on the SD card."));
		ui_empty_help(t, sizeof t, "sdmc:/Games/PlayStation", false);   // cut from a folder name on (r1b: one line)
		assert(!strcmp(t, "Put games in .../PlayStation on the SD card."));
		ui_empty_help(t, sizeof t, "sdmc:/a/very/long/folder/name/for/games", false);
		assert(!strcmp(t, "Put games in .../for/games on the SD card."));
		ui_empty_help(t, sizeof t, "sdmc:/ROMS/GAMEBOYADVANCE", true);   // no folder start in the tail: as cut
		assert(!strcmp(t, "Put games in ...AMEBOYADVANCE. Hold Y to look again."));
		assert(!strstr(t, "Rescan"));
	}
	// R1b-5: long ASCII, CJK, four-byte characters and trailing slashes keep whole characters, at any capacity
	{
		char t[256];
		ui_empty_help(t, sizeof t, "sdmc:/long/" CJK EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI, true);
		assert(utf8_ok(t) && !strcmp(t, "Put games in ..." CJK EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI ". Hold Y to look again."));
		ui_empty_help(t, sizeof t, "sdmc:/" EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI, false);
		assert(utf8_ok(t) && !strcmp(t, "Put games in ..." EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI " on the SD card."));
		ui_empty_help(t, sizeof t, "sdmc:/" CJK CJK "/" CJK CJK CJK CJK CJK CJK CJK CJK CJK CJK CJK CJK "/", false);   // a folder start in the tail
		assert(utf8_ok(t) && !strcmp(t, "Put games in .../" CJK CJK CJK CJK CJK CJK CJK CJK CJK CJK CJK CJK " on the SD card."));
		ui_empty_help(t, sizeof t, "sdmc:/roms/gba///", false);
		assert(!strcmp(t, "Put games in /roms/gba on the SD card."));
		ui_empty_help(t, sizeof t, "sdmc:/", false);
		assert(!strcmp(t, "Put games in / on the SD card."));
		ui_empty_help(t, sizeof t, "sdmc:/Games/PlayStation/", false);   // the slash is not one of the 13 kept
		assert(!strcmp(t, "Put games in .../PlayStation on the SD card."));
		ui_empty_help(t, sizeof t, "sdmc:/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", false);
		assert(!strcmp(t, "Put games in ...aaaaaaaaaaaaa on the SD card."));
		static const char *const dirs[] = { "", "sdmc:/roms/", "sdmc:/a/very/long/folder/name/for/all/my/games/PlayStation/",
			"sdmc:/long/" CJK EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI EMOJI,
			"sdmc:/" CJK CJK CJK CJK CJK CJK CJK CJK CJK CJK CJK CJK CJK CJK CJK "//", "sdmc:/" EMOJI "/" EMOJI EMOJI EMOJI };
		for (unsigned i = 0; i < sizeof dirs / sizeof *dirs; i++) { every_cap(dirs[i], false); every_cap(dirs[i], true); }
		// the tail alone: a buffer too small for the characters asked for keeps whole ones
		char s[8];
		ui_path_tail(s, sizeof s, "sdmc:/" EMOJI EMOJI EMOJI, 3);
		assert(!strcmp(s, "..." EMOJI));
		ui_path_tail(s, 3, "sdmc:/" EMOJI EMOJI, 1);   // no room for "...": whole characters or nothing
		assert(!strcmp(s, ""));
		ui_path_tail(s, 6, "sdmc:/ab", 1);
		assert(!strcmp(s, "...b"));
		ui_copy_text(s, 0, "abc");   // no room at all: nothing written
	}
	// the folder picker hides the 3DS's own folders at the root only (F14)
	assert(ui_system_folder("sdmc:/", "Nintendo 3DS") && ui_system_folder("sdmc:/", "3DS") && ui_system_folder("sdmc:", "luma"));
	assert(!ui_system_folder("sdmc:/", "roms") && !ui_system_folder("sdmc:/roms", "3ds") && !ui_system_folder("sdmc:/", "3ds games"));
	// F04: the shelf's L/R hint shows the two button glyphs and a plain verb, never "Page"
	assert(!strncmp(UI_HINT_LR_JUMP, UI_GLYPH_L, 3) && strstr(UI_HINT_LR_JUMP, UI_GLYPH_R) && strstr(UI_HINT_LR_JUMP, "Jump 5 games"));
	assert(!strstr(UI_HINT_LR_JUMP, "Page") && !strstr(UI_HINT_LR_TURN, "L/R"));
	// F05: cheat names that repeat a long start fold it, so the part that differs shows
	{
		const char *a = "Infinite Lives \"N. Sanity Beach\" Stage", *b = "Infinite Lives \"Jungle Rollers\" Stage";
		size_t n = ui_shared_words(b, a, UI_FOLD_MIN);
		assert(n == strlen("Infinite Lives ") && !strncmp(b + n, "\"Jungle", 7));
		assert(!ui_shared_words("Infinite Health", "Infinite Ammo", UI_FOLD_MIN));   // "Infinite " is too short to fold
		assert(!ui_shared_words("Max Health", "Max Health", UI_FOLD_MIN) && !ui_shared_words(a, NULL, 1));
		assert(ui_shared_words("Gives Crash 99 Wumpa Fruit \"Boulders\" Stage", "Gives Crash 99 Wumpa Fruit \"Upstream\" Stage", UI_FOLD_MIN) == 27);
	}
	// F15: one spelling for a key combo on every screen
	{
		char k[32];
		assert(!strcmp(ui_keys_text("SELECT+R", k, sizeof k), "Select + R") && !strcmp(ui_keys_text("L+R", k, sizeof k), "L + R"));
		assert(!strcmp(ui_keys_text("off", k, sizeof k), "Off") && !strcmp(ui_keys_text("START+ZL", k, sizeof k), "Start + ZL"));
	}
	puts("ui_format PASS");
	return 0;
}
