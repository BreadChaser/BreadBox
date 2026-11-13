#ifndef UI_FORMAT_H
#define UI_FORMAT_H
/* One spelling for the numbers and names the screens print: play time, game counts, a game's
 * display name. The room card, the library card, the pause tag, the postcards and the lists all
 * call these, so a value reads the same wherever it shows up. Header-only (no link order change). */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* Key hints read one way everywhere: keys pressed together are joined with " + " ("Select + R", "L + R"),
 * a key and what it does are joined with " · " ("X · slot 2") or ": " in a help line ("A: back in the game"). */

/* "< 1 min", "12 min", "1 h 00 min" */
static inline void ui_format_time(char *out, size_t cap, unsigned seconds)
{
	if (seconds < 60) snprintf(out, cap, "< 1 min");
	else if (seconds < 3600) snprintf(out, cap, "%u min", seconds / 60);
	else snprintf(out, cap, "%u h %02u min", seconds / 3600, seconds / 60 % 60);
}

/* "Nov 09 2026, 14:02" (the 3DS clock is local time already); "an unknown time" for 0 */
static inline void ui_format_when(char *out, size_t cap, time_t when)
{
	struct tm *tm = when ? gmtime(&when) : NULL;
	if (!tm || !strftime(out, cap, "%b %d %Y, %H:%M", tm)) snprintf(out, cap, "an unknown time");
}

/* "0 games", "1 game", "24 games" */
static inline void ui_format_games(char *out, size_t cap, int n)
{
	snprintf(out, cap, n == 1 ? "%d game" : "%d games", n);
}

/* Copies as much of text as fits, never cutting a UTF-8 character in two. */
static inline void ui_copy_text(char *out, size_t cap, const char *text)
{
	if (!cap) return;
	size_t n = strlen(text);
	if (n >= cap) {
		n = cap - 1;
		while (n && ((unsigned char)text[n] & 0xc0) == 0x80) n--;
	}
	memcpy(out, text, n);
	out[n] = 0;
}

/* The game's name as the library lists it: a ROM file name (or a title saved from one) without
 * its type, its "(Region)" tail, "_" and ", The". Safe on a name that is clean already, so a
 * value saved by an older build (with its underscores) shows the same as a fresh one. */
static inline void ui_clean_title(const char *name, char *out, size_t cap)
{
	if (!cap) return;
	ui_copy_text(out, cap, name);
	char *dot = strrchr(out, '.');
	if (dot && strlen(dot) <= 5 && !strchr(dot, ' ')) *dot = 0;   // a file type, not the dot in "Vol. 2"
	char *meta = strchr(out, '('); if (meta) { while (meta > out && meta[-1] == ' ') meta--; *meta = 0; }
	char *comma = strstr(out, ", The");
	if (comma) {
		char prefix[256], suffix[256], tmp[512];
		snprintf(prefix, sizeof(prefix), "%.*s", (int)(comma - out), out);
		snprintf(suffix, sizeof(suffix), "%s", comma + 5);
		snprintf(tmp, sizeof(tmp), "The %.240s%s%.240s", prefix, *suffix ? " " : "", suffix);
		ui_copy_text(out, cap, tmp);
	}
	for (char *p = out; *p; p++) if (*p == '_' || *p == '\t') *p = ' ';
	if (!strncasecmp(out, "Pokemon", 7)) {
		char tmp[256];
		snprintf(tmp, sizeof(tmp), "Pok\xc3\xa9mon%s", out + 7);
		ui_copy_text(out, cap, tmp);
	}
	while (*out == ' ') memmove(out, out + 1, strlen(out));
	for (size_t n = strlen(out); n && out[n - 1] == ' '; n--) out[n - 1] = 0;
}

/* The end of a path: the last `chars` whole UTF-8 characters, "..." in front when the start was cut;
 * whole characters only, also when out is too small (cap bytes, 4 a character at most). */
static inline void ui_path_tail_n(char *out, size_t cap, const char *path, size_t len, int chars);
#define ui_path_tail(out, cap, path, chars) ui_path_tail_n(out, cap, path, strlen(path), chars)

/* The empty library says where games go, wherever it says it (room card, Library card, first-run screen):
 * the games folder without "sdmc:" ("/roms" when none is chosen: the folder the start looks in first), and
 * at the library, where Y is the rescan, how to look again. Short enough for the room card's two lines; the
 * path is one word (no space to wrap at), so a long one keeps its last UI_EMPTY_TAIL characters from a folder
 * name on (".../PlayStation"): even in capitals it fits one line (tests/ui_fit/strings.py). */
#define UI_EMPTY_FOLDER "/roms"
#define UI_EMPTY_TAIL 13
#define UI_GAMES_DIR_CAP (3 + 4 * UI_EMPTY_TAIL + 1)   // "...", the characters (4 bytes at most), NUL
/* The games folder as the player reads it: without "sdmc:", no trailing '/', its last UI_EMPTY_TAIL characters from a
 * folder name on (".../PlayStation"); "/roms" when none is chosen. out holds UI_GAMES_DIR_CAP bytes. */
#define ui_games_dir_short(out, games_dir) ui_games_dir_tail(out, games_dir, UI_EMPTY_TAIL)
static inline void ui_games_dir_tail(char *out, const char *games_dir, int chars)   // the same, its last `chars`
{
	const char *d = games_dir && games_dir[0] ? games_dir : UI_EMPTY_FOLDER;
	if (!strncmp(d, "sdmc:", 5)) d += 5;
	if (!d[0]) d = "/";
	size_t len = strlen(d);
	while (len > 1 && d[len - 1] == '/') len--;   // "/roms/" reads "/roms"
	ui_path_tail_n(out, UI_GAMES_DIR_CAP, d, len, chars < UI_EMPTY_TAIL ? chars : UI_EMPTY_TAIL);
	char *cut = !strncmp(out, "...", 3) ? strchr(out + 3, '/') : NULL;   // "...s/PlayStation" reads ".../PlayStation"
	if (cut && cut[1]) memmove(out + 3, cut, strlen(cut) + 1);
}
static inline void ui_empty_help(char *out, size_t cap, const char *games_dir, bool at_library)
{
	char dir[UI_GAMES_DIR_CAP], text[sizeof(dir) + 64];
	ui_games_dir_short(dir, games_dir);
	snprintf(text, sizeof(text), at_library ? "Put games in %s. Hold Y to look again." : "Put games in %s on the SD card.", dir);
	ui_copy_text(out, cap, text);
}

/* A key combo as the saved name spells it ("SELECT+R", "off") the way every screen shows it: "Select + R",
 * "Off" (deep UI test F15: Settings said "SELECT+R", the pause card "Select + R"). */
static inline const char *ui_keys_text(const char *combo, char *out, size_t cap)
{
	size_t n = 0;
	if (!cap) return "";
	out[0] = 0;
	if (!combo || !combo[0] || !strcmp(combo, "off")) { snprintf(out, cap, "Off"); return out; }
	for (const char *c = combo; *c && n + 4 < cap; c++) {
		if (*c == '+') { out[n++] = ' '; out[n++] = '+'; out[n++] = ' '; continue; }
		const char *part = c;   // a button's name: a word is capitalized ("Select"), a short name stays ("ZL")
		while (part > combo && part[-1] != '+') part--;
		bool word = strcspn(part, "+") > 2;
		out[n++] = c == part || !word || *c < 'A' || *c > 'Z' ? *c : (char)(*c + ('a' - 'A'));
	}
	out[n] = 0;
	return out;
}

/* Long lists whose names repeat a long start ("Infinite Lives "N. Sanity Beach" Stage", "Infinite Lives
 * "Jungle Rollers" Stage"...): the bytes of whole words (ending in a space) a and b begin with alike, 0 when
 * under `min` bytes or one word. A row that shares that much with the row above shows "... " and the rest, so the part
 * that differs is what fits (deep UI test F05). */
static inline size_t ui_shared_words(const char *a, const char *b, size_t min)
{
	size_t n = 0, word = 0, words = 0;
	if (!a || !b) return 0;
	while (a[n] && a[n] == b[n]) { if (a[n] == ' ') { word = n + 1; words++; } n++; }
	if (!a[n] && !b[n]) return 0;   // the same name twice: nothing to fold
	return word >= min && words >= 2 ? word : 0;   // two whole words at least: "Infinite " alone says too little
}
#define UI_FOLD_MIN 8                 // fold only a start this long, of two words or more ("Infinite Lives ")
#define UI_FOLD_MARK "... "

#include "ui_words.h"   /* the postcard words: defines only, for files that need nothing else */

/* The shelf card's foot line: the L and R buttons' own glyphs (system font) and what they do, in words a
 * player reads at a glance (deep UI test F04: "L/R: Page 5 games" read as "page: 5 games"). */
#define UI_GLYPH_L "\xee\x80\x84"
#define UI_GLYPH_R "\xee\x80\x85"
#define UI_HINT_LR_JUMP UI_GLYPH_L " " UI_GLYPH_R "  Jump 5 games"
#define UI_HINT_LR_TURN UI_GLYPH_L " " UI_GLYPH_R "  Turn the box"

/* The 3DS's own folders at the SD card's root: the folder picker leaves them out (a new player should not have
 * to wonder at the 32-digit folders inside "Nintendo 3DS") unless a shallow look finds a game in one
 * (browser.c, folder_peek.h): a game under DCIM/Games keeps DCIM. */
static inline bool ui_system_folder(const char *parent, const char *name)
{
	static const char *const own[] = { "Nintendo 3DS", "3ds", "DCIM", "luma", "gm9", "private" };
	if (strcmp(parent, "sdmc:/") && strcmp(parent, "sdmc:")) return false;
	for (size_t i = 0; i < sizeof(own) / sizeof(*own); i++)
		if (!strcasecmp(name, own[i])) return true;
	return false;
}

static inline void ui_path_tail_n(char *out, size_t cap, const char *path, size_t len, int chars)
{
	if (!cap) return;
	const char *tail = path + len;
	for (int n = 0; tail > path && n < chars; n++) {
		--tail;
		while (tail > path && ((unsigned char)*tail & 0xc0) == 0x80) --tail;
	}
	size_t dots = tail > path && cap > 3 ? 3 : 0, room = cap - dots, n = (size_t)(path + len - tail);
	memcpy(out, "...", dots);
	if (n >= room) {   // too small: as many whole characters as fit
		n = room - 1;
		while (n && ((unsigned char)tail[n] & 0xc0) == 0x80) n--;
	}
	memcpy(out + dots, tail, n);
	out[dots + n] = 0;
}
#endif
