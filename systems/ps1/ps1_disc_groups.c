#define _GNU_SOURCE   // strcasestr
// PS1 library titles and disc groups: a clean title per game, the discs of one
// game ("... (Disc 1)", "... (Disc 2)") grouped, twin titles told apart. They
// run once per library build, so the outlined ones are PSX3D_COLD.

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "app_log.h"
#include "cold_code.h"
#include "ps1_library.h"

// a copy of s in *p; the old text stays if there is no memory
static void replace_str(char **p, const char *s)
{
	char *n = strdup(s);
	if (n) {
		free(*p);
		*p = n;
	}
}

static bool is_disc_ext(const char *e)
{
	return !strcasecmp(e, ".cue") || !strcasecmp(e, ".chd") || !strcasecmp(e, ".pbp") ||
		!strcasecmp(e, ".iso") || !strcasecmp(e, ".img") || !strcasecmp(e, ".mdf") ||
		!strcasecmp(e, ".bin");   // the core's valid_extensions; no .ccd (it can't load CloneCD)
}

// "Metal Gear Solid (USA) (Disc 2) (Rev 1)" -> disc 2, group key
// "Metal Gear Solid (USA) (Rev 1)"
static int disc_number(const char *name, char *key, size_t len)
{
	const char *p = strcasestr(name, "(Disc ");
	int disc = 0;
	if (p) {
		disc = atoi(p + 6);
		const char *close = strchr(p, ')');
		size_t pre = p - name;
		while (pre > 0 && name[pre - 1] == ' ')
			pre--;
		snprintf(key, len, "%.*s%s", (int)pre, name, close ? close + 1 : "");
	} else {
		snprintf(key, len, "%s", name);
	}
	return disc;
}

// strip "(...)" and "[...]" tags and the extension
static char *clean_title(const char *name)
{
	char out[PATH_LEN];
	size_t n = 0;
	int depth = 0;
	const char *end = name + strlen(name);
	const char *dot = strrchr(name, '.');
	if (dot && is_disc_ext(dot))
		end = dot;
	if (dot && !strcasecmp(dot, ".m3u"))
		end = dot;
	for (const char *p = name; p < end && n < sizeof(out) - 1; p++) {
		if (*p == '(' || *p == '[') { depth++; continue; }
		if (*p == ')' || *p == ']') { if (depth) depth--; continue; }
		if (depth)
			continue;
		if (*p == '_')
			out[n++] = ' ';
		else
			out[n++] = *p;
	}
	out[n] = 0;
	// collapse spaces, trim
	char *s = out, *d = out;
	bool space = true;
	for (; *s; s++) {
		if (*s == ' ') {
			if (!space)
				*d++ = ' ';
			space = true;
		} else {
			*d++ = *s;
			space = false;
		}
	}
	while (d > out && d[-1] == ' ')
		d--;
	*d = 0;
	return strdup(out[0] ? out : name);
}

// discs of one game share the name minus "(Disc N)", wherever they sit
// (many libraries put each disc in its own folder)
static game_t *find_group(u32 dir_id, const char *key)
{
	(void)dir_id;
	for (int i = ps1_library_ngames - 1; i >= 0; i--)
		if (ps1_library_games[i].group && !strcasecmp(ps1_library_games[i].group, key))
			return &ps1_library_games[i];
	return NULL;
}

// a new game named after the file; NULL (nothing kept) if out of memory
static game_t *new_game(u32 dir_id, const char *name, const char *key)
{
	game_t *g = &ps1_library_games[ps1_library_ngames];
	memset(g, 0, sizeof(*g));
	char ck[PATH_LEN];
	snprintf(ck, sizeof(ck), "%s", name);
	char *e = strrchr(ck, '.');
	if (e)
		*e = 0;
	g->title = clean_title(name);
	g->cover_key = strdup(ck);
	g->group = strdup(key);
	g->dir_id = dir_id;
	if (!g->title || !g->cover_key || !g->group) {
		ps1_library_game_free(g);
		return NULL;
	}
	ps1_library_ngames++;
	return g;
}

PSX3D_COLD void ps1_library_add_disc(u32 dir_id, const char *path)
{
	const char *slash = strrchr(path, '/');
	const char *name = slash ? slash + 1 : path;
	char key[PATH_LEN];
	int disc = disc_number(name, key, sizeof(key));

	// strip the extension from the group key so "X (Disc 1).cue" and
	// "X (Disc 2).cue" match
	char *dot = strrchr(key, '.');
	if (dot && is_disc_ext(dot))
		*dot = 0;

	game_t *g = disc ? find_group(dir_id, key) : NULL;
	if (g && g->ndiscs >= MAX_DISCS) {
		ps1_library_omit.discs++;
		return;
	}
	if (!g && ps1_library_ngames == MAX_GAMES) {
		ps1_library_omit.games++;
		return;
	}
	char *p = strdup(path);
	if (p && !g)
		g = new_game(dir_id, name, key);
	if (!p || !g) {
		free(p);
		ps1_library_omit.nomem++;
		return;
	}
	g->discs[g->ndiscs] = p;
	g->disc_no[g->ndiscs] = disc;
	g->ndiscs++;
}

// "Legend of Dragoon, The" -> "The Legend of Dragoon"
PSX3D_COLD void ps1_library_fix_article(game_t *g)
{
	static const char *const arts[] = { ", The", ", A", ", An" };
	for (size_t i = 0; i < sizeof(arts) / sizeof(arts[0]); i++) {
		size_t tl = strlen(g->title), al = strlen(arts[i]);
		char *at = strstr(g->title, arts[i]);
		// ", The" at the end or before " - subtitle"
		if (at && (at[al] == 0 || !strncmp(at + al, " - ", 3))) {
			char buf[PATH_LEN];
			snprintf(buf, sizeof(buf), "%s %.*s%s", arts[i] + 2, (int)(at - g->title), g->title, at + al);
			(void)tl;
			replace_str(&g->title, buf);
			return;
		}
	}
}

// the "(...)" tags of a No-Intro name, minus the boring ones
// Whole words only: "(It)" is a language, "(Italy)" is not, "(Demo)" is
// interesting (an earlier prefix match hid "Italy" behind "It" and "Demo"
// behind "De"). A tag is boring when every comma-separated item is.
static bool boring_item(const char *t, size_t n)
{
	static const char *const boring[] = { "USA", "Europe", "Japan", "World", "En", "Fr", "De", "Es",
		"It", "Pt", "Sv", "Nl", "Da", "No", "Fi", "Ja" };
	for (size_t i = 0; i < sizeof(boring) / sizeof(boring[0]); i++)
		if (n == strlen(boring[i]) && !strncasecmp(t, boring[i], n))
			return true;
	// "Rev 1", "Disc 2", "v1.1"
	return (n >= 3 && !strncasecmp(t, "Rev", 3) && (n == 3 || t[3] == ' ')) ||
		(n >= 4 && !strncasecmp(t, "Disc", 4) && (n == 4 || t[4] == ' ')) ||
		(n >= 2 && (t[0] == 'v' || t[0] == 'V') && isdigit((unsigned char)t[1]));
}

static bool interesting_tag(const char *t, size_t n)
{
	bool any = false;
	for (size_t i = 0; i < n; ) {
		while (i < n && (t[i] == ',' || t[i] == ' ')) i++;
		size_t j = i;
		while (j < n && t[j] != ',') j++;
		size_t k = j;
		while (k > i && t[k - 1] == ' ') k--;
		if (k > i && !boring_item(t + i, k - i))
			any = true;
		i = j;
	}
	return any;
}

// two different games with the same clean title: keep what tells them apart
// (an interesting tag like "Demo" or "Italy"; else, for two regional copies,
// their region, so "Resident Evil 2 (USA)" and "(Europe)" never look the same)
PSX3D_COLD void ps1_library_disambiguate(void)
{
	for (int i = 0; i < ps1_library_ngames; i++)
		for (int j = i + 1; j < ps1_library_ngames; j++) {
			if (strcasecmp(ps1_library_games[i].title, ps1_library_games[j].title))
				continue;
			game_t *gs[2] = { &ps1_library_games[i], &ps1_library_games[j] };
			char tags[2][128] = { "", "" }, region[2][64] = { "", "" };
			for (int k = 0; k < 2; k++) {
				const char *p = gs[k]->cover_key;
				while ((p = strchr(p, '('))) {
					const char *q = strchr(p, ')');
					if (!q)
						break;
					if (interesting_tag(p + 1, q - p - 1) && !tags[k][0])
						snprintf(tags[k], sizeof(tags[k]), "%.*s", (int)(q - p - 1), p + 1);
					if (!region[k][0])   // the first tag of a No-Intro name is its region
						snprintf(region[k], sizeof(region[k]), "%.*s", (int)(q - p - 1), p + 1);
					p = q;
				}
			}
			bool by_region = !tags[0][0] && !tags[1][0] && strcmp(region[0], region[1]);
			for (int k = 0; k < 2; k++) {
				const char *t = by_region ? region[k] : tags[k];
				if (t[0]) {
					char buf[PATH_LEN];
					snprintf(buf, sizeof(buf), "%s (%s)", gs[k]->title, t);
					replace_str(&gs[k]->title, buf);
				}
			}
		}
}

int ps1_library_cmp_games(const void *a, const void *b)
{
	return strcasecmp(((const game_t *)a)->title, ((const game_t *)b)->title);
}

PSX3D_COLD void ps1_library_sort_discs(game_t *g)
{
	for (int i = 1; i < g->ndiscs; i++)
		for (int j = i; j > 0 && g->disc_no[j] < g->disc_no[j - 1]; j--) {
			char *t = g->discs[j]; g->discs[j] = g->discs[j - 1]; g->discs[j - 1] = t;
			int n = g->disc_no[j]; g->disc_no[j] = g->disc_no[j - 1]; g->disc_no[j - 1] = n;
		}
	// cover art is named after disc 1
	if (g->ndiscs > 1) {
		const char *base = strrchr(g->discs[0], '/');
		char ck[PATH_LEN];
		snprintf(ck, sizeof(ck), "%s", base ? base + 1 : g->discs[0]);
		char *e = strrchr(ck, '.');
		if (e)
			*e = 0;
		replace_str(&g->cover_key, ck);
	}
}
