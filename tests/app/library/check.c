// Host test of the PS1 library functions BreadBox runs: launch paths and multi-disc
// playlists (atomic writes, failures, collisions), the missing-file and PS1-image checks
// made at launch, and disc grouping. The real systems/ps1/library.c is compiled; the
// library itself comes from the shared index in BreadBox (tests/bb_index, tests/bb_library),
// so the entries here are built directly.
//
// "sdmc:/..." paths are plain relative folders here, so the test chdirs into
// its temp directory and the frontend's PSX3D_DIR becomes ./sdmc:/3ds/BreadBox.
#define _GNU_SOURCE
#include <dirent.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include "3ds.h"

// the few frontend calls library.c makes
static u64 osGetTime(void) { return 0; }
void log_printf(const char *fmt, ...)
{
	if (!getenv("LIBRARY_TEST_LOG"))
		return;
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

static unsigned cover_changes;
void covers_library_begin(void) { assert(!cover_changes); cover_changes++; }
void covers_library_end(void) { assert(cover_changes==1); cover_changes--; }
static u64 svcGetSystemTick(void) { return 0; }
// the CHD content probe has its own suite (tests/bb_disc): every .chd here is a PS1 disc
bool bb_disc_is_ps1_chd(const char *path) { (void)path; return true; }
#ifndef SYSCLOCK_ARM11
#define SYSCLOCK_ARM11 268111856ULL
#endif
#include "../../../systems/ps1/library.c"
#include "../../common/test_tmpdir.h"

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
	printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void mkdirs(const char *path)
{
	char p[PATH_MAX];
	snprintf(p, sizeof(p), "%s", path);
	for (char *s = p + 1; *s; s++)
		if (*s == '/') {
			*s = 0;
			mkdir(p, 0777);
			*s = '/';
		}
	mkdir(p, 0777);
}

// root/rel with text in it (parents are made)
static void put(const char *root, const char *rel, const char *text)
{
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", root, rel);
	char *slash = strrchr(path, '/');
	*slash = 0;
	mkdirs(path);
	*slash = '/';
	FILE *f = fopen(path, "wb");
	if (!f) {
		printf("cannot create %s\n", path);
		exit(2);
	}
	// disc images get a real (tiny) PS1 file system unless the text says
	// otherwise: the scanner reads the volume descriptor (ps1_image_at)
	const char *e = strrchr(path, '.');
	if (e && !strcmp(text, "x") && (!strcasecmp(e, ".bin") || !strcasecmp(e, ".iso") || !strcasecmp(e, ".img"))) {
		static unsigned char img[17 * 2048];
		memset(img, 0, sizeof(img));
		img[16 * 2048] = 1;
		memcpy(img + 16 * 2048 + 1, "CD001", 5);
		memcpy(img + 16 * 2048 + 8, "PLAYSTATION", 11);
		fwrite(img, 1, sizeof(img), f);
	} else
		fputs(text, f);
	fclose(f);
}

// a file with exactly these bytes
static void put_bytes(const char *root, const char *rel, const void *data, size_t n, long at)
{
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", root, rel);
	char *slash = strrchr(path, '/');
	*slash = 0;
	mkdirs(path);
	*slash = '/';
	FILE *f = fopen(path, "wb");
	static unsigned char zero[17 * 2352 + 64];
	fwrite(zero, 1, (size_t)at, f);
	fwrite(data, 1, n, f);
	fwrite(zero, 1, 64, f);
	fclose(f);
}

static bool ends_with(const char *s, const char *tail)
{
	size_t a = strlen(s), b = strlen(tail);
	return a >= b && !strcmp(s + a - b, tail);
}

static int case_no;
static char root[PATH_MAX];
// a fresh, empty root for the next case (relative: "sdmc:/lib/<n>")
static const char *fresh(void)
{
	snprintf(root, sizeof(root), "sdmc:/lib/%d", ++case_no);
	mkdirs(root);
	return root;
}

static char *slurp(const char *path)
{
	static char buf[4096];
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = 0;
	return buf;
}

static bool exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0;
}

// a library entry as the PS1 adapter builds it from the shared index
static game_t entry(const char *title, char **discs, int n)
{
	game_t g = { 0 };
	g.title = (char *)title;
	g.cover_key = (char *)title;
	g.ndiscs = n;
	for (int i = 0; i < n; i++) g.discs[i] = discs[i], g.disc_no[i] = i + 1;
	return g;
}

static void test_grouping(void)
{
	// discs of one game group into one entry; its scan-time group is freed with the rest
	library_free();
	games = calloc(MAX_GAMES, sizeof(*games));   // as the index publication does
	add_disc(1, "sdmc:/lib/x/G (Disc 1).chd");
	add_disc(1, "sdmc:/lib/x/G (Disc 2).chd");
	CHECK(ngames == 1 && games[0].group && games[0].ndiscs == 2);
	library_free();
	CHECK(ngames == 0);
	// region tags tell copies apart; language lists and revisions do not
	CHECK(interesting_tag("Italy", 5) && !interesting_tag("En,Fr,De", 8) && !interesting_tag("Rev 1", 5) &&
		interesting_tag("Revelations", 11) && interesting_tag("Demo", 4));
}

static void test_launch_path(void)
{
	const char *r = fresh();
	put(r, "Solo.chd", "x");
	put(r, "Duo (Disc 1).chd", "x");
	put(r, "Duo (Disc 2).chd", "x");
	char solo_disc[PATH_MAX], duo1[PATH_MAX], duo2[PATH_MAX];
	snprintf(solo_disc, sizeof(solo_disc), "%s/Solo.chd", r);
	snprintf(duo1, sizeof(duo1), "%s/Duo (Disc 1).chd", r);
	snprintf(duo2, sizeof(duo2), "%s/Duo (Disc 2).chd", r);
	char *sd[1] = { solo_disc }, *dd[2] = { duo1, duo2 };
	game_t solo_g = entry("Solo", sd, 1), duo_g = entry("Duo", dd, 2);
	duo_g.cover_key = "Duo (Disc 1)";
	const game_t *solo = &solo_g, *duo = &duo_g;
	char buf[PATH_LEN];

	// one disc: the disc itself, nothing written
	CHECK(library_launch_path(solo, buf, sizeof(buf)) == buf);
	CHECK(ends_with(buf, "/Solo.chd"));

	// several: a playlist, in place when it returns
	const char *m3u = PSX3D_DIR "/m3u/Duo _Disc 1_.m3u";
	CHECK(library_launch_path(duo, buf, sizeof(buf)) == buf);
	CHECK(!strcmp(buf, m3u));
	char want[PATH_MAX];
	snprintf(want, sizeof(want), "/lib/%d/Duo (Disc 1).chd\n/lib/%d/Duo (Disc 2).chd\n", case_no, case_no);
	CHECK(slurp(m3u) && !strcmp(slurp(m3u), want));
	CHECK(!exists(PSX3D_DIR "/m3u/Duo _Disc 1_.m3u.tmp"));
	CHECK(!exists(PSX3D_DIR "/m3u/Duo _Disc 1_.m3u.bak"));

	// the disc swap order follows the game, a rewrite replaces the old list
	char *old = strdup(slurp(m3u));
	CHECK(library_launch_path(duo, buf, sizeof(buf)) == buf);
	CHECK(!strcmp(slurp(m3u), old));

	// the card is full: the write fails at the end (/dev/full), the player
	// gets NULL, the previous list stays as it was
	const char *tmp = PSX3D_DIR "/m3u/Duo _Disc 1_.m3u.tmp";
	if (symlink("/dev/full", tmp) == 0) {
		CHECK(library_launch_path(duo, buf, sizeof(buf)) == NULL);
		CHECK(slurp(m3u) && !strcmp(slurp(m3u), old));
		CHECK(!exists(PSX3D_DIR "/m3u/Duo _Disc 1_.m3u.bak"));
		struct stat st;
		CHECK(lstat(tmp, &st) != 0);   // nothing left behind
	} else
		printf("skip: no symlink to /dev/full\n");

	// the list cannot be created (read-only folder)
	chmod(PSX3D_DIR "/m3u", 0555);
	if (access(PSX3D_DIR "/m3u", W_OK) != 0) {
		CHECK(library_launch_path(duo, buf, sizeof(buf)) == NULL);
		CHECK(slurp(m3u) && !strcmp(slurp(m3u), old));
	} else
		printf("skip: running as root, a folder cannot be made read-only\n");
	chmod(PSX3D_DIR "/m3u", 0777);

	// and it works again once there is room
	CHECK(library_launch_path(duo, buf, sizeof(buf)) == buf);
	CHECK(!strcmp(slurp(m3u), old));
	char old2[4096];
	snprintf(old2, sizeof(old2), "%s", old);
	free(old);

	// no folder for the lists yet: it is made
	snprintf(want, sizeof(want), "%s.gone", PSX3D_DIR "/m3u");
	CHECK(rename(PSX3D_DIR "/m3u", want) == 0);
	CHECK(library_launch_path(duo, buf, sizeof(buf)) == buf);
	CHECK(slurp(m3u) && !strcmp(slurp(m3u), old2));
}

// the checks a launch makes: missing disc or track, and that an image is a PS1 disc
static void test_launch_checks(void)
{
	const char *r = fresh();
	put(r, "Ridge Racer (USA).bin", "x");                       // PS1, cooked
	unsigned char pvd[40] = { 1, 'C', 'D', '0', '0', '1' };
	memcpy(pvd + 8, "PLAYSTATION", 11);
	put_bytes(r, "Raw (USA).bin", pvd, sizeof(pvd), 16 * 2352 + 24);   // PS1, raw mode 2
	put(r, "Cart.bin", "x not a disc");                         // a cartridge .bin
	put_bytes(r, "Saturn.iso", "SEGA SEGASATURN ", 16, 0);
	unsigned char psp[40] = { 1, 'C', 'D', '0', '0', '1' };
	memcpy(psp + 8, "PSP GAME", 8);
	put_bytes(r, "Psp.iso", psp, sizeof(psp), 16 * 2048);
	char p[PATH_MAX];
	const char *ps1[] = { "Ridge Racer (USA).bin", "Raw (USA).bin" }, *other[] = { "Cart.bin", "Saturn.iso", "Psp.iso" };
	for (int i = 0; i < 2; i++) { snprintf(p, sizeof(p), "%s/%s", r, ps1[i]); CHECK(library_is_ps1_image(p)); }
	for (int i = 0; i < 3; i++) { snprintf(p, sizeof(p), "%s/%s", r, other[i]); CHECK(!library_is_ps1_image(p)); }
	char miss[160];
	r = fresh();
	put(r, "Gone.cue", "FILE \"Gone (Track 1).bin\" BINARY\n");
	snprintf(p, sizeof(p), "%s/Gone.cue", r);
	CHECK(library_missing_file(p, miss, sizeof(miss)) && !strcmp(miss, "Gone (Track 1).bin"));
	snprintf(p, sizeof(p), "%s/Nothing.bin", r);
	CHECK(library_missing_file(p, miss, sizeof(miss)) && !strcmp(miss, "Nothing.bin"));
	put(r, "Here.cue", "FILE \"Here.bin\" BINARY\n");
	put(r, "Here.bin", "x");
	snprintf(p, sizeof(p), "%s/Here.cue", r);
	CHECK(!library_missing_file(p, miss, sizeof(miss)) && library_is_ps1_image(p));
}


static void test_playlist_hash(void)
{
	game_t a = { 0 }, b = { 0 };
	char *da[2] = { "sdmc:/x/1.cue", "sdmc:/x/2.cue" };
	a.cover_key = "\xe3\x83\x89\xe3\x83\xa9\xe3\x82\xaf\xe3\x82\xa8";   // Japanese title
	b.cover_key = "\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\x8a";
	a.ndiscs = b.ndiscs = 2;
	a.discs[0] = b.discs[0] = da[0];
	a.discs[1] = b.discs[1] = da[1];
	char pa[PATH_MAX], pb[PATH_MAX];
	CHECK(library_launch_path(&a, pa, sizeof(pa)) && library_launch_path(&b, pb, sizeof(pb)) && strcmp(pa, pb));
}


int main(void)
{
	char dir[4096];
	if (!test_mkdtemp(dir, sizeof(dir), "bb-library") || chdir(dir)) {
		printf("no temp directory\n");
		return 2;
	}
	mkdirs("sdmc:/3ds/BreadBox");
	mkdirs("sdmc:/lib");

	test_grouping();
	test_launch_path();
	test_launch_checks();
	test_playlist_hash();
	library_free();

	char cmd[PATH_MAX + 16];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
	if (system(cmd))
		printf("could not remove %s\n", dir);
	printf("library: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
