#define _GNU_SOURCE   // strcasestr
// PS1 library disc files: the files a .cue or .m3u names, PS1 image checks and
// the missing-file report.

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "app_log.h"
#include "bb_disc.h"
#include "ps1_library.h"

// ---------------------------------------------------------------- scanning
// Pass 1 walks the folders, collecting the disc files and every file a .cue
// or .m3u names. Pass 2 adds the collected files that nothing names.

const char *ps1_library_ext_of(const char *name)
{
	const char *dot = strrchr(name, '.');
	return dot ? dot : "";
}
typedef struct { const char *s; size_t n; } part_t;

cand_t *ps1_library_cands;
int ps1_library_ncands;
static int cands_cap;
char **ps1_library_refs;      // normalized paths named by .cue and .m3u files, sorted
int ps1_library_nrefs;
static int refs_cap;

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
void ps1_library_norm_path(const char *dir, const char *ref, char *out, size_t len)
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
char *ps1_library_trim(char *s)
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
	l = ps1_library_trim(l);
	if (strncasecmp(l, "FILE", 4) || !isspace((unsigned char)l[4]))
		return NULL;
	l = ps1_library_trim(l + 4);
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
	return ps1_library_trim(l);
}

// one path per line, "#" lines are comments, "path|label" names a disc
static char *m3u_name(char *l)
{
	l = ps1_library_trim(l);
	if (*l == '#')
		return NULL;
	char *bar = strchr(l, '|');
	if (bar)
		*bar = 0;
	return ps1_library_trim(l);
}

static void add_ref(const char *dir, const char *name, const char *self)
{
	char ref[PATH_LEN];
	ps1_library_norm_path(dir, name, ref, sizeof(ref));
	if (!strcmp(ref, self))   // a file never hides itself
		return;
	char **g = grow(ps1_library_refs, ps1_library_nrefs, &refs_cap, sizeof(*ps1_library_refs), MAX_REFS);
	char *p = g ? strdup(ref) : NULL;
	if (g)
		ps1_library_refs = g;
	if (!p) {
		ps1_library_omit.refs++;
		return;
	}
	ps1_library_refs[ps1_library_nrefs++] = p;
}

// the files named by the .cue or .m3u at path (read up to REF_BYTES)
bool ps1_library_refs_prepared;
const void *ps1_library_refs_data;
size_t ps1_library_refs_size;
static void read_refs(const char *path, const char *dir, bool cue)
{
	static char buf[REF_BYTES + 1];
 FILE *f=ps1_library_refs_prepared ? (ps1_library_refs_data ? fmemopen((void *)ps1_library_refs_data,ps1_library_refs_size,"rb") : NULL) : fopen(path,"rb");
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
		ps1_library_omit.refs++;
	}
	char self[PATH_LEN];
	ps1_library_norm_path("", path, self, sizeof(self));
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

void ps1_library_note_file(u32 dir_id, const char *dir, const char *name)
{
	char path[PATH_LEN];
	if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) {
		ps1_library_omit.files++;
		return;
	}
	const char *e = ps1_library_ext_of(name);
	if (!strcasecmp(e, ".cue") || !strcasecmp(e, ".m3u"))
		read_refs(path, dir, !strcasecmp(e, ".cue"));
	cand_t *c = grow(ps1_library_cands, ps1_library_ncands, &cands_cap, sizeof(*ps1_library_cands), MAX_CANDS);
	char *p = c ? strdup(path) : NULL;
	if (c)
		ps1_library_cands = c;
	if (!p) {
		if (ps1_library_ncands >= MAX_CANDS)
			ps1_library_omit.files++;
		else
			ps1_library_omit.nomem++;
		return;
	}
	ps1_library_cands[ps1_library_ncands] = (cand_t){0};
	ps1_library_cands[ps1_library_ncands].path = p;
	ps1_library_cands[ps1_library_ncands].dir_id = dir_id;
	ps1_library_ncands++;
}

int ps1_library_cmp_ref(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

void ps1_library_scan_free(void)
{
	for (int i = 0; i < ps1_library_ncands; i++)
		free(ps1_library_cands[i].path);
	for (int i = 0; i < ps1_library_nrefs; i++)
		free(ps1_library_refs[i]);
	free(ps1_library_cands);
	free(ps1_library_refs);
	ps1_library_cands = NULL;
	ps1_library_refs = NULL;
	ps1_library_ncands = cands_cap = ps1_library_nrefs = refs_cap = 0;
}

// Is the disc image at path a PlayStation disc as far as one cheap read can
// tell? ISO-9660 images (.iso/.img/.bin, and the first file of a .cue) need a
// volume descriptor whose system is not a PSP; Sega CD/Saturn/Dreamcast discs
// are recognised by their first sector; PBP files must hold a PS1 image.
// BreadBox verifies CHD in the shared worker and probes MDF/M3U below.
// Standalone PSX3D retains its folder-filtered handling.
bool ps1_library_image_at(const char *path, int depth)
{
	const char *e = ps1_library_ext_of(path);
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
   valid=ps1_library_image_at(track,depth+1); break;
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
		return ps1_library_image_at(track, depth + 1);
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

bool library_is_ps1_image(const char *path) { return ps1_library_image_at(path, 0); }

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
	const char *e = ps1_library_ext_of(path);
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
			} else if (!cue && !strcasecmp(ps1_library_ext_of(ref), ".cue"))
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
