#define _GNU_SOURCE   // strcasestr
// Game library: one entry per game with a clean title, built from the
// games folder. Discs of the same game ("... (Disc 1)", "... (Disc 2)")
// are grouped; the files a .cue or .m3u points at (tracks, discs) are hidden,
// and nothing else. Only readdir() is used (stat only when a folder entry has
// no type, no free-space queries), so it stays quick on huge SD cards, and
// the result is cached in library.txt. What a limit leaves out is counted
// and logged, never dropped silently.
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "safefile.h"
#include "theme.h"
#include "app_paths.h"
#include "app_log.h"
#include "platform_video.h"
#include "app_input.h"
#include "ui_pause.h"
#include "ui.h"
#include "library.h"
#include "covers.h"
#include "safefile.h"
#include "bb_index.h"
#include "bb_save.h"
#include "bb_registry.h"
#include "bb_disc.h"
#include "devtools.h"

#define LIB_CACHE PSX3D_DIR "/library.txt"
#define MAX_GAMES 2048
#define MAX_DISCS 8
#define MAX_DEPTH 3
#define MAX_ENTRIES 4096    // disc files and folders read per folder
#define MAX_CANDS 8192      // disc files held between the two scan passes
#define MAX_REFS 8192       // files named by .cue and .m3u files
#define REF_BYTES (16 * 1024)   // read of one .cue or .m3u
#define MAX_PARTS 128       // folders in a path being compared

static game_t *games, *visible_games;
static int visible_count,visible_capacity;
static bool visible_borrowed;
static int ngames;
static bool library_building;
static char lib_root[PATH_LEN];

// what the last scan left out, by reason
static struct {
	int files;     // folder or library over its limit, or a path too long
	int depth;     // folders below MAX_DEPTH, not opened
	int folders;   // folders that could not be opened
	int games;     // over MAX_GAMES
	int discs;     // over MAX_DISCS for one game
	int nomem;     // out of memory
	int refs;      // .cue/.m3u names not read: list too long (extra files may show)
	int notps1;    // disc images of other systems (logged; not counted as omitted)
} omit;

int library_count(void) {
 return visible_count;
}
const game_t *library_get(int i) {
 return i>=0 && i<visible_count ? &visible_games[i]:NULL;
}

// frees everything a game owns
static void game_free(game_t *g)
{
	free(g->title);
	free(g->cover_key);
	free(g->group);
	for (int d = 0; d < g->ndiscs; d++)
		free(g->discs[d]);
	memset(g, 0, sizeof(*g));
}

static void library_free(void)
{
 // Public descriptors borrow the builder's strings during an append-only walk.
 // At retirement they take ownership; no public pointer is freed mid-pass.
 if(visible_borrowed) {
  // A cancellation can interrupt an append pass before publication. Discard
  // only new ownership; the visible prefix still owns its original strings.
  for(int i=0;i<ngames;i++) {
   if(i>=visible_count)game_free(&games[i]);
   else for(int d=visible_games[i].ndiscs;d<games[i].ndiscs;d++)free(games[i].discs[d]);
  }
  free(games);games=NULL;ngames=0;visible_borrowed=false;return;
 }
	for (int i = 0; i < ngames; i++)
		game_free(&games[i]);
 free(games);games=NULL;
	ngames = 0;
}

// a copy of s in *p; the old text stays if there is no memory
static void replace_str(char **p, const char *s)
{
	char *n = strdup(s);
	if (n) {
		free(*p);
		*p = n;
	}
}


static const char *ext_of(const char *name)
{
	const char *dot = strrchr(name, '.');
	return dot ? dot : "";
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
	for (int i = ngames - 1; i >= 0; i--)
		if (games[i].group && !strcasecmp(games[i].group, key))
			return &games[i];
	return NULL;
}

// a new game named after the file; NULL (nothing kept) if out of memory
static game_t *new_game(u32 dir_id, const char *name, const char *key)
{
	game_t *g = &games[ngames];
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
		game_free(g);
		return NULL;
	}
	ngames++;
	return g;
}

static void add_disc(u32 dir_id, const char *path)
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
		omit.discs++;
		return;
	}
	if (!g && ngames == MAX_GAMES) {
		omit.games++;
		return;
	}
	char *p = strdup(path);
	if (p && !g)
		g = new_game(dir_id, name, key);
	if (!p || !g) {
		free(p);
		omit.nomem++;
		return;
	}
	g->discs[g->ndiscs] = p;
	g->disc_no[g->ndiscs] = disc;
	g->ndiscs++;
}

// ---------------------------------------------------------------- scanning
// Pass 1 walks the folders, collecting the disc files and every file a .cue
// or .m3u names. Pass 2 adds the collected files that nothing names.

typedef struct { char *path; u32 dir_id;
 bool probed, valid, added;
} cand_t;
typedef struct { char *name; bool isdir; } entry_t;
typedef struct { const char *s; size_t n; } part_t;

static cand_t *cands;
static int ncands, cands_cap;
static char **refs;      // normalized paths named by .cue and .m3u files, sorted
static int nrefs, refs_cap;

// arr with room for element n (doubling, at most max); NULL if there is none
// (arr itself stays valid then)
static void *grow(void *arr, int n, int *cap, size_t size, int max)
{
	if (n >= max)
		return NULL;
	if (n < *cap)
		return arr;
	int c = *cap ? *cap * 2 : 64;
	if (c > max)
		c = max;
	void *p = realloc(arr, (size_t)c * size);
	if (p)
		*cap = c;
	return p;
}

static const char *skip_sdmc(const char *p)
{
	return strncasecmp(p, "sdmc:", 5) ? p : p + 5;
}

// the folders of s, "." and ".." applied, appended to v[0..nv)
static int path_parts(part_t *v, int nv, const char *s)
{
	while (*s) {
		while (*s == '/' || *s == '\\')
			s++;
		size_t n = strcspn(s, "/\\");
		if (n == 2 && s[0] == '.' && s[1] == '.') {
			if (nv)
				nv--;
		} else if (n && !(n == 1 && s[0] == '.') && nv < MAX_PARTS) {
			v[nv].s = s;
			v[nv].n = n;
			nv++;
		}
		s += n;
	}
	return nv;
}

// One spelling per file, to compare paths: no "sdmc:" ("/x" is the same file),
// no "." or "..", either slash, lower case (FAT). A relative ref starts at dir.
static void norm_path(const char *dir, const char *ref, char *out, size_t len)
{
	part_t v[MAX_PARTS];
	int nv = 0;
	const char *r = skip_sdmc(ref);
	bool rooted = r[0] == '/' || r[0] == '\\';
	if (!rooted) {
		dir = skip_sdmc(dir);
		rooted = dir[0] == '/' || dir[0] == '\\';
		nv = path_parts(v, nv, dir);
	}
	nv = path_parts(v, nv, r);
	size_t n = 0;
	for (int i = 0; i < nv; i++) {
		if ((rooted || i) && n + 1 < len)
			out[n++] = '/';
		for (size_t k = 0; k < v[i].n && n + 1 < len; k++)
			out[n++] = tolower((unsigned char)v[i].s[k]);
	}
	if (rooted && !n && len > 1)
		out[n++] = '/';
	out[n] = 0;
}

// s without the spaces around it (cut in place)
static char *trim(char *s)
{
	while (isspace((unsigned char)*s))
		s++;
	char *end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		*--end = 0;
	return s;
}

// FILE "name.bin" BINARY, or FILE name.bin BINARY: the name; else NULL
static char *cue_name(char *l)
{
	l = trim(l);
	if (strncasecmp(l, "FILE", 4) || !isspace((unsigned char)l[4]))
		return NULL;
	l = trim(l + 4);
	if (*l == '"') {
		char *q = strchr(++l, '"');
		if (q)
			*q = 0;
		return l;
	}
	// unquoted: the last word is the track type
	char *w = l + strlen(l);
	while (w > l && !isspace((unsigned char)w[-1]))
		w--;
	if (w > l)
		w[-1] = 0;
	return trim(l);
}

// one path per line, "#" lines are comments, "path|label" names a disc
static char *m3u_name(char *l)
{
	l = trim(l);
	if (*l == '#')
		return NULL;
	char *bar = strchr(l, '|');
	if (bar)
		*bar = 0;
	return trim(l);
}

static void add_ref(const char *dir, const char *name, const char *self)
{
	char ref[PATH_LEN];
	norm_path(dir, name, ref, sizeof(ref));
	if (!strcmp(ref, self))   // a file never hides itself
		return;
	char **g = grow(refs, nrefs, &refs_cap, sizeof(*refs), MAX_REFS);
	char *p = g ? strdup(ref) : NULL;
	if (g)
		refs = g;
	if (!p) {
		omit.refs++;
		return;
	}
	refs[nrefs++] = p;
}

// the files named by the .cue or .m3u at path (read up to REF_BYTES)
static bool refs_prepared;
static const void *refs_data;
static size_t refs_size;
static void read_refs(const char *path, const char *dir, bool cue)
{
	static char buf[REF_BYTES + 1];
 FILE *f=refs_prepared ? (refs_data ? fmemopen((void *)refs_data,refs_size,"rb") : NULL) : fopen(path,"rb");
	if (!f)
		return;
	size_t n = fread(buf, 1, REF_BYTES, f);
	bool more = n == REF_BYTES && fgetc(f) != EOF;
	fclose(f);
	buf[n] = 0;
	if (more) {   // drop the line that was cut off
		while (n && buf[n - 1] != '\n' && buf[n - 1] != '\r')
			n--;
		buf[n] = 0;
		omit.refs++;
	}
	char self[PATH_LEN];
	norm_path("", path, self, sizeof(self));
	char *l = buf;
	if (!memcmp(l, "\xEF\xBB\xBF", 3))
		l += 3;
	while (*l) {
		size_t len = strcspn(l, "\r\n");
		char next = l[len];
		l[len] = 0;
		char *name = cue ? cue_name(l) : m3u_name(l);
		if (name && *name)
			add_ref(dir, name, self);
		if (!next)
			break;
		l += len + 1;
	}
}

static void note_file(u32 dir_id, const char *dir, const char *name)
{
	char path[PATH_LEN];
	if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) {
		omit.files++;
		return;
	}
	const char *e = ext_of(name);
	if (!strcasecmp(e, ".cue") || !strcasecmp(e, ".m3u"))
		read_refs(path, dir, !strcasecmp(e, ".cue"));
	cand_t *c = grow(cands, ncands, &cands_cap, sizeof(*cands), MAX_CANDS);
	char *p = c ? strdup(path) : NULL;
	if (c)
		cands = c;
	if (!p) {
		if (ncands >= MAX_CANDS)
			omit.files++;
		else
			omit.nomem++;
		return;
	}
	cands[ncands] = (cand_t){0};
	cands[ncands].path = p;
	cands[ncands].dir_id = dir_id;
	ncands++;
}

static u64 index_wait_started;
static bool index_wait_slow;
static bool index_progress_shown;
static void draw_index_progress(void *unused); /* rendered by the shared library component */
static bool wait_index(void)
{
    u64 start = osGetTime();
    bool canceling = false;
    while (((!bb_index_complete() && bb_index_busy()) || canceling) && osGetTime() - start < 65000 && aptMainLoop()) {
        bb_index_service();
        dev_tick_keys();
        input_poll();
        u32 k = input_keys_down();
        if (k & KEY_B) canceling = true;
        // A completed scan wins over B delayed by the incoming transition.
        if (ui_swap_keys(k | (canceling ? KEY_B : 0), UI_SWAP_MENU) & KEY_B) return bb_index_complete();
        index_wait_slow=osGetTime()-index_wait_started>=2000;
        ui_begin();
        video_present_vsync(false, NULL, draw_index_progress, NULL);
        index_progress_shown=true;
    }
    return true;
}

static int cmp_ref(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static void scan_free(void)
{
	for (int i = 0; i < ncands; i++)
		free(cands[i].path);
	for (int i = 0; i < nrefs; i++)
		free(refs[i]);
	free(cands);
	free(refs);
	cands = NULL;
	refs = NULL;
	ncands = cands_cap = nrefs = refs_cap = 0;
}

// Is the disc image at path a PlayStation disc as far as one cheap read can
// tell? ISO-9660 images (.iso/.img/.bin, and the first file of a .cue) need a
// volume descriptor whose system is not a PSP; Sega CD/Saturn/Dreamcast discs
// are recognised by their first sector; PBP files must hold a PS1 image.
// BreadBox verifies CHD in the shared worker and probes MDF/M3U below.
// Standalone PSX3D retains its folder-filtered handling.
static bool ps1_image_at(const char *path, int depth)
{
	const char *e = ext_of(path);
 if (!strcasecmp(e, ".m3u")) {
  if (depth >= 4) return false;
  FILE *list = fopen(path, "r"); if (!list) return false;
  char line[PATH_LEN], track[PATH_LEN]; bool valid = false;
  for (int rows=0; rows<64 && fgets(line,sizeof(line),list); rows++) {
   line[strcspn(line,"\r\n")] = 0;
   if (!line[0] || line[0]=='#') continue;
   if (line[0]=='/' || !strncasecmp(line,"sdmc:",5)) snprintf(track,sizeof(track),"%s",line);
   else {
    char dir[PATH_LEN]; snprintf(dir,sizeof(dir),"%s",path);
    char *slash=strrchr(dir,'/'); if (slash) *slash=0;
    if (snprintf(track,sizeof(track),"%s/%s",dir,line)>=(int)sizeof(track)) break;
   }
   valid=ps1_image_at(track,depth+1); break;
  }
  fclose(list); return valid;
 }
 if (!strcasecmp(e, ".chd")) return bb_disc_chd_kind(path) != BB_DISC_OTHER;   /* an unreadable CHD is the core's to judge */
	FILE *f = fopen(path, "rb");
	if (!f)
        return false;
	u8 buf[2352 + 64];
	bool ok = true;
	if (!strcasecmp(e, ".cue")) {
		// the first file the sheet names
		char cue[2048 + 1];
		size_t n = fread(cue, 1, sizeof(cue) - 1, f);
		fclose(f);
		cue[n] = 0;
		char *name = NULL;
		for (char *l = cue; *l && !name; ) {
			size_t len = strcspn(l, "\r\n");
			char next = l[len];
			l[len] = 0;
			name = cue_name(l);
			if (!next) break;
			l += len + 1;
		}
        if (!name || !*name || depth >= 4) return false;
		char dir[PATH_LEN], track[PATH_LEN];
		snprintf(dir, sizeof(dir), "%s", path);
		char *slash = strrchr(dir, '/');
		if (slash) *slash = 0;
		if (name[0] == '/' || !strncasecmp(name, "sdmc:", 5))
			snprintf(track, sizeof(track), "%s", name);
		else if (snprintf(track, sizeof(track), "%s/%s", dir, name) >= (int)sizeof(track))
			return true;
		return ps1_image_at(track, depth + 1);
	}
	if (!strcasecmp(e, ".pbp")) {
		// PBP header: the DATA.PSAR offset at 0x24; a PS1 eboot's PSAR starts "PSISOIMG" or "PSTITLEIMG"
		u8 h[40];
		u32 psar = 0;
		if (fread(h, 1, 40, f) == 40 && !memcmp(h, "\0PBP", 4))
			psar = h[36] | h[37] << 8 | h[38] << 16 | (u32)h[39] << 24;
		ok = psar && !fseek(f, psar, SEEK_SET) && fread(buf, 1, 10, f) == 10 &&
			(!memcmp(buf, "PSISOIMG", 8) || !memcmp(buf, "PSTITLEIMG", 10));
		fclose(f);
		return ok;
	}
	// first sector: Sega's discs say so (cooked, or after a raw sector header)
	size_t n = fread(buf, 1, 64, f);
	for (int off = 0; off <= 16 && n >= 64; off += 16)
		if (!memcmp(buf + off, "SEGADISCSYSTEM", 14) || !memcmp(buf + off, "SEGA SEGASATURN", 15) ||
		    !memcmp(buf + off, "SEGA SEGAKATANA", 15))
			ok = false;
	// sector 16: the primary volume descriptor, 2048-byte or raw 2352-byte sectors (mode 1 or 2)
	bool pvd = false;
	static const long at[3] = { 16 * 2048, 16 * 2352 + 16, 16 * 2352 + 24 };
	for (int i = 0; i < 3 && ok && !pvd; i++)
		if (!fseek(f, at[i], SEEK_SET) && fread(buf, 1, 40, f) == 40 && buf[0] == 1 && !memcmp(buf + 1, "CD001", 5)) {
			pvd = true;
            if (memcmp(buf + 8, "PLAYSTATION", 11)) ok = false;
            if (!memcmp(buf + 8, "PSP GAME", 8) || !memcmp(buf + 8, "UMD", 3))
				ok = false;
		}
	fclose(f);
	return ok && pvd;   // no file system at all: a cartridge or audio disc, not a PS1 game
}

// "Legend of Dragoon, The" -> "The Legend of Dragoon"
static void fix_article(game_t *g)
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
static void disambiguate(void)
{
	for (int i = 0; i < ngames; i++)
		for (int j = i + 1; j < ngames; j++) {
			if (strcasecmp(games[i].title, games[j].title))
				continue;
			game_t *gs[2] = { &games[i], &games[j] };
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

static int cmp_games(const void *a, const void *b)
{
	return strcasecmp(((const game_t *)a)->title, ((const game_t *)b)->title);
}

static void sort_discs(game_t *g)
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

static u32 shared_signature(void)
{
    u32 hash=2166136261u;char path[PATH_LEN];
    for(int i=0,n=bb_index_ps1_count();i<n;i++) {   /* counted once: each count walks the whole index */
        if(!bb_index_ps1_get(i,path,sizeof(path))) continue;
        for(const unsigned char *p=(const unsigned char *)path;*p;p++) hash=(hash^*p)*16777619u;
        hash=(hash^0xff)*16777619u;
    }
    return hash;
}

typedef struct {char *text;size_t size;} cache_output;
static void cache_output_write(void *arg)
{
 cache_output *out=arg;FILE *f=bb_index_cancelled()?NULL:safe_begin(LIB_CACHE);
 if(f)safe_finish(f,LIB_CACHE,fwrite(out->text,1,out->size,f)==out->size);
 free(out->text);free(out);
}

static void save_cache(const char *root)
{
 cache_output *out=calloc(1,sizeof(*out));if(!out)return;
 FILE *f=open_memstream(&out->text,&out->size);
 if(!f){free(out);return;}
	fprintf(f, "#root\t%s\n", root);
    fprintf(f,"#index1\t%08lx\n",(unsigned long)shared_signature());
	for (int i = 0; i < ngames; i++) {
		fprintf(f, "%s\t%s", games[i].title, games[i].cover_key);
		for (int d = 0; d < games[i].ndiscs; d++)
			fprintf(f, "\t%s", games[i].discs[d]);
		fputc('\n', f);
	}
 bool ok=!ferror(f);if(fclose(f))ok=false;
 if(!ok || !bb_index_defer(cache_output_write,out)){free(out->text);free(out);}
}

static bb_later_file presentation_cache={.path=LIB_CACHE};
static unsigned presentation_generation;
static void presentation_read(void *arg)
{
 bb_later_file *r=arg;
 __atomic_store_n(&r->state,BB_LATER_READING,__ATOMIC_RELEASE);
 FILE *f=bb_index_cancelled()?NULL:fopen(r->path,"rb");bool ok=false;
 if(f) {
  long bytes=fseek(f,0,SEEK_END)?-1:ftell(f);
  if(bytes>0 && bytes<=4*1024*1024 && !fseek(f,0,SEEK_SET)) {
   r->buf=malloc((size_t)bytes);r->size=(size_t)bytes;
   ok=r->buf && fread(r->buf,1,r->size,f)==r->size && !ferror(f);
  }
  fclose(f);
 }
 if(!ok){free(r->buf);r->buf=NULL;r->size=0;}
 __atomic_store_n(&r->state,ok?BB_LATER_DONE:BB_LATER_FAILED,__ATOMIC_RELEASE);
}
static bool load_cache(const char *root)
{
	static char line[PATH_LEN * (MAX_DISCS + 2)];
 FILE *f = presentation_cache.buf ? fmemopen(presentation_cache.buf,presentation_cache.size,"r") : NULL;
	if (!f)
		return false;
	char *vbuf = malloc(32 * 1024);   // 32 KB SD requests, not stdio's 1 KB
	if (vbuf)
		setvbuf(f, vbuf, _IOFBF, 32 * 1024);
#define CACHE_CLOSE(f) (fclose(f), free(vbuf))
	if (!fgets(line, sizeof(line), f) || strncmp(line, "#root\t", 6) ||
	    strncmp(line + 6, root, strlen(root)) || line[6 + strlen(root)] != '\n') {
		CACHE_CLOSE(f);
		return false;
	}
    char stamp[40];snprintf(stamp,sizeof(stamp),"#index1\t%08lx\n",(unsigned long)shared_signature());
    if(!fgets(line,sizeof(line),f) || strcmp(line,stamp)) {CACHE_CLOSE(f);return false;}
	bool ok = true;
	while (ok && fgets(line, sizeof(line), f) && ngames < MAX_GAMES) {
		line[strcspn(line, "\r\n")] = 0;
		char *tok = strtok(line, "\t");
		if (!tok)
			continue;
		game_t *g = &games[ngames];
		memset(g, 0, sizeof(*g));
		g->title = strdup(tok);
		tok = strtok(NULL, "\t");
		g->cover_key = strdup(tok ? tok : "");
		while (ok && (tok = strtok(NULL, "\t")) && g->ndiscs < MAX_DISCS) {
			g->discs[g->ndiscs] = strdup(tok);
			if (g->discs[g->ndiscs])
				g->ndiscs++;
			else
				ok = false;
		}
		ok = ok && g->title && g->cover_key;
		if (ok && g->ndiscs)
			ngames++;
		else
			game_free(g);   // no disc, or out of memory
	}
	CACHE_CLOSE(f);
#undef CACHE_CLOSE
	return ok && ngames > 0;
}

/* UI-owned staging arrays never replace the published rows until fully sorted. */
static bool reserve_visible(void)
{
 if(ngames<=visible_capacity)return true;
 int cap=visible_capacity?visible_capacity:8;
 while(cap<ngames)cap*=2;
 game_t *rows=realloc(visible_games,cap*sizeof(*rows));if(!rows)return false;
 visible_games=rows;visible_capacity=cap;return true;
}
static void publish_library(void)
{
 if(!visible_borrowed)for(int i=0;i<visible_count;i++)game_free(&visible_games[i]);
 if(ngames)memcpy(visible_games,games,ngames*sizeof(*games));
 visible_count=ngames;visible_borrowed=true;
}
// A later CUE/M3U can hide a file already appended to an earlier publication.
// Drop it only at the publication boundary, with covers held and no yield.
static void remove_referenced_discs(void)
{
 for(int i=0;i<ngames;) {
  game_t *g=&games[i];
  for(int d=0;d<g->ndiscs;) {
   char key[PATH_LEN],*k=key;norm_path("",g->discs[d],key,sizeof(key));
   if(!nrefs || !bsearch(&k,refs,nrefs,sizeof(refs[0]),cmp_ref)){d++;continue;}
   free(g->discs[d]);
   for(int e=d+1;e<g->ndiscs;e++){g->discs[e-1]=g->discs[e];g->disc_no[e-1]=g->disc_no[e];}
   g->ndiscs--;
  }
  if(g->ndiscs){i++;continue;}
  game_free(g);memmove(g,g+1,(ngames-i-1)*sizeof(*g));ngames--;
 }
}
static void retire_published_builder(void)
{
 library_free();
 // Only public rows remain after settlement, sized to the actual row count.
 if(visible_count){game_t *rows=realloc(visible_games,visible_count*sizeof(*rows));if(rows){visible_games=rows;visible_capacity=visible_count;}}
 else {free(visible_games);visible_games=NULL;visible_capacity=0;}
 for(int i=0;i<visible_count;i++){free(visible_games[i].group);visible_games[i].group=NULL;}
}

static unsigned ps1_snapshot;
static bool ps1_loaded, ps1_force, ps1_needs_settle;
static unsigned ps1_scan_generation;
static int ps1_noted;
static int ps1_phase, ps1_cursor, ps1_candidates, ps1_published_candidates;
static unsigned ps1_build_generation;
static bool ps1_covers_held;

/* Only the worker touches cartridge/disc files. The UI owns all grouping
 * arrays; a ready job supplies bytes or a boolean, never mutates those arrays. */
typedef struct {char path[PATH_LEN];int kind;void *data;size_t size;bool valid;} candidate_io;
static struct {int state,count;candidate_io entries[8];} ps1_io;
static candidate_io *candidate_current;
static void candidate_read(void *unused)
{
 (void)unused;
 for(int i=0;i<ps1_io.count && !bb_index_cancelled();i++) {
  candidate_io *r=&ps1_io.entries[i];
  if(r->kind==1) {
   FILE *f=fopen(r->path,"rb");
   if(f){r->data=malloc(REF_BYTES+1);if(r->data)r->size=fread(r->data,1,REF_BYTES+1,f);fclose(f);}
  } else r->valid=ps1_image_at(r->path,0);
 }
 __atomic_store_n(&ps1_io.state,bb_index_cancelled()?3:2,__ATOMIC_RELEASE);
}
static void candidate_done(void)
{
 for(int i=0;i<ps1_io.count;i++)free(ps1_io.entries[i].data);
 memset(ps1_io.entries,0,sizeof(ps1_io.entries));ps1_io.count=0;candidate_current=NULL;
 __atomic_store_n(&ps1_io.state,0,__ATOMIC_RELEASE);
}
static bool candidate_ready(const char *path,int kind)
{
 int state=__atomic_load_n(&ps1_io.state,__ATOMIC_ACQUIRE);
 if(state==1)return false;
 if(state==2)for(int i=0;i<ps1_io.count;i++)if(ps1_io.entries[i].kind==kind && !strcmp(path,ps1_io.entries[i].path)){candidate_current=&ps1_io.entries[i];return true;}
 candidate_done();
 for(int i=ps1_cursor,limit=kind==1?ps1_candidates:ncands;i<limit && ps1_io.count<8;i++) {
  char next[PATH_LEN];
  if(kind==1){if(!bb_index_ps1_get(i,next,sizeof(next)))continue;const char *e=ext_of(next);if(strcasecmp(e,".cue") && strcasecmp(e,".m3u"))continue;}
  else {
   if(cands[i].probed)continue;
   snprintf(next,sizeof(next),"%s",cands[i].path);
   char key[PATH_LEN],*k=key;norm_path("",next,key,sizeof(key));
   if(nrefs && bsearch(&k,refs,nrefs,sizeof(refs[0]),cmp_ref))continue;
  }
  candidate_io *r=&ps1_io.entries[ps1_io.count++];snprintf(r->path,sizeof(r->path),"%s",next);r->kind=kind;
 }
 if(!ps1_io.count)return false;
 __atomic_store_n(&ps1_io.state,1,__ATOMIC_RELEASE);
 if(!bb_index_defer(candidate_read,NULL)){__atomic_store_n(&ps1_io.state,0,__ATOMIC_RELEASE);return false;}
 if(__atomic_load_n(&ps1_io.state,__ATOMIC_ACQUIRE)==2)return candidate_ready(path,kind);
 return false;
}
static void candidate_consume(void){if(candidate_current){free(candidate_current->data);candidate_current->data=NULL;candidate_current->size=0;candidate_current=NULL;}}

// Run a little presentation work per menu frame. The shared worker owns all
// directory traversal, reference reads, content probes and cache writes.
// Cover workers borrow entries too: hold their drain boundary across the
// entire incremental rebuild, including generation changes and final sorting.
/* A terminal LIMITED/ERROR walk can publish an empty presentation too.
 * Cache settlement is separate from having rows ready for a caller. */
static bool library_index_busy(void) {if(!bb_index_generation())return bb_index_busy();return ps1_phase || ps1_snapshot!=bb_index_generation() || !ps1_loaded || ps1_published_candidates!=bb_index_ps1_count();}
static void settle_visible(void)
{
 if(!ps1_needs_settle || bb_index_busy())return;
 if(bb_index_status()==BB_INDEX_COMPLETE) {
  game_t *old=games;int count=ngames;games=visible_games;ngames=visible_count;
  save_cache(lib_root);games=old;ngames=count;
 }
 ps1_needs_settle=false;retire_published_builder();scan_free();ps1_noted=0;
}
bool library_poll_index(void)
{
    bb_index_service();
    int io_state=__atomic_load_n(&ps1_io.state,__ATOMIC_ACQUIRE);
    if(io_state==1)return false;
    if(io_state==3){candidate_done();scan_free();library_free();ps1_phase=0;ps1_force=true;}
    const char *root=bb_index_root();
    int candidates=bb_index_ps1_count();
    if (!bb_index_complete() && !candidates && bb_index_busy()) return false;
    unsigned generation = bb_index_generation();
    if (!root[0] || !generation) return false;
    if (!ps1_phase && !ps1_force && ps1_loaded && ps1_snapshot == generation &&
        !strcmp(lib_root,root) && ps1_published_candidates==candidates) {
        settle_visible();return false;
    }
    if (!ps1_phase && !ps1_force && presentation_generation!=generation) {
        int state=bb_later_state(&presentation_cache);
        if(state==BB_LATER_READING || state==BB_LATER_QUEUED)return false;
        free(presentation_cache.buf);presentation_cache.buf=NULL;presentation_cache.size=0;
        __atomic_store_n(&presentation_cache.state,BB_LATER_QUEUED,__ATOMIC_RELEASE);
        if(!bb_index_defer(presentation_read,&presentation_cache)) {
            __atomic_store_n(&presentation_cache.state,BB_LATER_IDLE,__ATOMIC_RELEASE);return false;
        }
        presentation_generation=generation;
    }
    if (!ps1_phase && !ps1_force) {
        int state=bb_later_state(&presentation_cache);
        if(state==BB_LATER_READING || state==BB_LATER_QUEUED)return false;
    }
    if (!ps1_covers_held) {if (!covers_library_try_begin()) return false; ps1_covers_held=true;}   // never waits for the covers' worker (round 16)
    if (ps1_phase && ps1_build_generation != generation) {
        candidate_done();
        scan_free(); library_free(); ps1_phase=0;
    }
    if (!ps1_phase) {
        bool append=!ps1_force && games && ps1_scan_generation==generation && !strcmp(lib_root,root);
        if(!append) {
            library_free();scan_free();ps1_noted=0;memset(&omit,0,sizeof(omit));
            ps1_scan_generation=generation;
            games=calloc(MAX_GAMES,sizeof(*games));
            if(!games){ps1_covers_held=false;covers_library_end();return false;}
            snprintf(lib_root,sizeof(lib_root),"%s",root);
            if(!ps1_force && bb_index_complete() && load_cache(root)) {
                if(!reserve_visible()){ps1_force=true;return false;}
                free(presentation_cache.buf);presentation_cache.buf=NULL;
                publish_library();retire_published_builder();
                ps1_snapshot=generation;ps1_loaded=true;ps1_published_candidates=candidates;ps1_needs_settle=false;
                log_printf("library: shared cache games=%d\n",visible_count);
                ps1_covers_held=false;covers_library_end();return true;
            }
            free(presentation_cache.buf);presentation_cache.buf=NULL;
            for(int i=0;i<ngames;i++)game_free(&games[i]);
            ngames=0;
        }
        ps1_force=false;library_building=true;
        ps1_candidates=candidates;ps1_cursor=ps1_noted;ps1_phase=1;
        ps1_build_generation=generation;
        log_printf("library: shared import candidates=%d\n",ps1_candidates);
    }
    u64 start=svcGetSystemTick();   // the system tick, not osGetTime (a millisecond clock that Azahar updates once a frame)
    do {
        if (ps1_phase==1) {
            if (ps1_cursor < ps1_candidates) {
                char path[PATH_LEN];
                if (bb_index_ps1_get(ps1_cursor,path,sizeof(path))) {
                    const char *ext=ext_of(path);bool reference=!strcasecmp(ext,".cue") || !strcasecmp(ext,".m3u");
                    if(reference && !candidate_ready(path,1))return false;
                    refs_prepared=reference;refs_data=candidate_current?candidate_current->data:NULL;refs_size=candidate_current?candidate_current->size:0;
                    char *slash=strrchr(path,'/');
                    if (slash) {
                        *slash=0;u32 id=2166136261u;
                        for (const unsigned char *p=(const unsigned char *)path;*p;p++) id=(id^*p)*16777619u;
                        note_file(id,path,slash+1);
                    }
                    refs_prepared=false;refs_data=NULL;refs_size=0;
                    if(reference)candidate_consume();
                }
                ps1_noted=++ps1_cursor;
            } else {
                if(nrefs) qsort(refs,nrefs,sizeof(refs[0]),cmp_ref);
                ps1_phase=2;ps1_cursor=0;
            }
        } else if (ps1_cursor < ncands) {
            cand_t *c=&cands[ps1_cursor];char key[PATH_LEN],*k=key;
            norm_path("",c->path,key,sizeof(key));
            if (!nrefs || !bsearch(&k,refs,nrefs,sizeof(refs[0]),cmp_ref)) {
                if(!c->probed) {
                    if(!candidate_ready(c->path,2))return false;
                    c->valid=candidate_current->valid;c->probed=true;candidate_consume();
                }
                if(c->valid && !c->added){add_disc(c->dir_id,c->path);c->added=true;}
            }
            ps1_cursor++;
        } else {
            candidate_done();
            if(!reserve_visible())return false;
            remove_referenced_discs();
            for(int i=0;i<ngames;i++) {
                sort_discs(&games[i]);fix_article(&games[i]);
            }
            disambiguate();qsort(games,ngames,sizeof(games[0]),cmp_games);
            bool settled=!bb_index_busy() && generation==bb_index_generation() &&
                ps1_candidates==bb_index_ps1_count();
            if(settled && bb_index_status()==BB_INDEX_COMPLETE) save_cache(root);
            publish_library();ps1_snapshot=generation;ps1_loaded=true;ps1_needs_settle=!settled;
            if(settled){retire_published_builder();scan_free();ps1_noted=0;}
            ps1_published_candidates=ps1_candidates;ps1_phase=0;library_building=false;
            log_printf("library: shared ready games=%d\n",visible_count);
            ps1_covers_held=false;covers_library_end();
            return true;
        }
    } while(svcGetSystemTick()-start<SYSCLOCK_ARM11/1000*5);   // 5 ms a menu frame
    return false; /* staging progress does not change published shelves */
}
bool library_poll_adapter(bool refresh) {return refresh || ps1_phase ? library_poll_index() : false;}
library_result library_load_result(const char *root, bool rescan)
{
    if(!root || !*root)return LIBRARY_EMPTY;
    index_wait_started=osGetTime();index_wait_slow=false;index_progress_shown=false;
    ui_swap_enter();
    bb_index_start(root,rescan);
    if(rescan){ps1_loaded=false;ps1_force=true;}
    if(!wait_index())return LIBRARY_CANCELLED;
    while(aptMainLoop()) {
        bb_index_service();dev_tick_keys();input_poll();
        u32 k=input_keys_down();
        library_poll_index();
        if(!library_index_busy()) {
            // An instant load has no progress screen to leave. Preserve the
            // incoming WAIT so the caller presents the first destination frame.
            if(!index_progress_shown)
                return (library_count()>0 || bb_system_total()>0)?LIBRARY_LOADED:LIBRARY_EMPTY;
            // Persistent completion survives an incoming WAIT/IN and expiry of
            // buffered input: asked again every frame until the gate lets it through.
            k|=UI_SWAP_REMOTE;
        }
        k=ui_swap_keys(k,UI_SWAP_MENU);
        if(k & UI_SWAP_REMOTE)return (library_count()>0 || bb_system_total()>0)?LIBRARY_LOADED:LIBRARY_EMPTY;
        if(k & KEY_B) {
            // B on the frame the scan finished: the completed scan is shown, not reported as cancelled
            if(bb_index_complete() && !library_index_busy())return (library_count()>0 || bb_system_total()>0)?LIBRARY_LOADED:LIBRARY_EMPTY;
            return LIBRARY_CANCELLED;
        }
        index_wait_slow=osGetTime()-index_wait_started>=2000;
        ui_begin();video_present_vsync(false,NULL,draw_index_progress,NULL);
        index_progress_shown=true;
    }
    return LIBRARY_CANCELLED;
}
bool library_load(const char *root,bool rescan)
{
 if(library_load_result(root,rescan)!=LIBRARY_LOADED)return false;
 return library_count()>0 || bb_system_total()>0; /* any system */
}

bool library_is_ps1_image(const char *path) { return ps1_image_at(path, 0); }

// The first file a game needs that isn't there: the disc itself, a track a
// .cue names, or a disc a .m3u names (and that disc's tracks). out gets its
// name; false when everything is present.
bool library_missing_file_cancel(const char *path, char *out, size_t len, bool (*cancel)(void *), void *arg)
{
	if (cancel && cancel(arg)) return true;
	struct stat st;
	if (stat(path, &st)) {
		const char *s = strrchr(path, '/');
		snprintf(out, len, "%s", s ? s + 1 : path);
		return true;
	}
	const char *e = ext_of(path);
	bool cue = !strcasecmp(e, ".cue");
	if (!cue && strcasecmp(e, ".m3u"))
		return false;
	if (cancel && cancel(arg)) return true;
	char *copy = malloc(REF_BYTES + 1);   // only after a failed load: no static buffer
	FILE *f = copy ? fopen(path, "rb") : NULL;
	if (!f) {
		free(copy);
		return false;
	}
	size_t n = fread(copy, 1, REF_BYTES, f);
	fclose(f);
	copy[n] = 0;
	char dir[PATH_LEN];
	snprintf(dir, sizeof(dir), "%s", path);
	char *slash = strrchr(dir, '/');
	if (slash)
		*slash = 0;
	bool missing = false;
	for (char *l = copy; *l && !missing; ) {
		if (cancel && cancel(arg)) { missing=true; break; }
		size_t ll = strcspn(l, "\r\n");
		char next = l[ll];
		l[ll] = 0;
		char *name = cue ? cue_name(l) : m3u_name(l);
		if (name && *name) {
			char ref[PATH_LEN];
			if (name[0] == '/' || !strncasecmp(name, "sdmc:", 5))
				snprintf(ref, sizeof(ref), "%s%s", name[0] == '/' ? "sdmc:" : "", name);
			else
				snprintf(ref, sizeof(ref), "%.250s/%.250s", dir, name);
			if (stat(ref, &st)) {
				snprintf(out, len, "%s", name);
				missing = true;
			} else if (!cue && !strcasecmp(ext_of(ref), ".cue"))
				missing = library_missing_file_cancel(ref, out, len, cancel, arg);
		}
		if (!next)
			break;
		l += ll + 1;
	}
	free(copy);
	return missing;
}

bool library_missing_file(const char *path, char *out, size_t len)
{
 return library_missing_file_cancel(path,out,len,NULL,NULL);
}

// Path to start a game with: the disc itself, or for multi-disc games a
// playlist (so the core can swap discs). NULL if the playlist could not be
// written; the one from before is then left as it was, and the game must not
// start with just disc 1.
const char *library_launch_path(const game_t *g, char *buf, size_t len)
{
	if (g->ndiscs < 1)
		return NULL;
	if (g->ndiscs == 1) {
		snprintf(buf, len, "%s", g->discs[0]);
		return buf;
	}
	mkdir(PSX3D_DIR "/m3u", 0777);
	char safe[128];
	size_t n = 0;
	for (const char *p = g->cover_key; *p && n < sizeof(safe) - 1; p++)
		safe[n++] = (isalnum((unsigned char)*p) || *p == ' ' || *p == '-') ? *p : '_';
	safe[n] = 0;
	// names with non-ASCII letters (Japanese titles become "____") get a hash,
	// so two such games never share one playlist
	bool changed = false;
	for (const char *p = g->cover_key; *p && !changed; p++)
		changed = (u8)*p >= 0x80;
	if (changed) {
		u32 h = 2166136261u;
		for (const char *p = g->cover_key; *p; p++)
			h = (h ^ (u8)*p) * 16777619u;
		snprintf(buf, len, PSX3D_DIR "/m3u/%s %08lx.m3u", safe, (unsigned long)h);
	} else
		snprintf(buf, len, PSX3D_DIR "/m3u/%s.m3u", safe);
	FILE *f = safe_begin(buf);
	if (!f) {
		log_printf("library: cannot write %s\n", buf);
		return NULL;
	}
	// the core only treats "/..." as absolute; on the 3DS "/x" is sdmc:/x
	bool ok = true;
	for (int d = 0; d < g->ndiscs; d++) {
		const char *p = g->discs[d];
		if (!strncmp(p, "sdmc:", 5))
			p += 5;
		if (fprintf(f, "%s\n", p) < 0)
			ok = false;
	}
	if (!safe_finish(f, buf, ok)) {
		log_printf("library: cannot write %s\n", buf);
		return NULL;
	}
	return buf;
}

#include "components/library.inc"
