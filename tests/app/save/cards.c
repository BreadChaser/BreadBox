// Host test for cards.c, the real memory card code (run.sh: ASan/UBSan) on
// top of the real safefile.c with its fault hooks. A card is loaded only from
// a good copy; a damaged one is kept; a failed write or repair keeps the card
// due (never "shadowed" as if it were on the SD card), is tried again after
// CARD_RETRY_MS and is reported; core-managed cards (card 2) get the same.
#define _GNU_SOURCE
#include <string.h>
#include <time.h>
#include "libretro.h"
#include "app_log.h"
#include "memory_cards.h"
#include "../../../systems/ps1/emu.h"
#include "clip.h"
#include "history.h"
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../../common/test_tmpdir.h"

#define CARD (128 * 1024)
static unsigned char ram[CARD];             // the core's SAVE_RAM (card 1)
char Mcd1Data[CARD], Mcd2Data[CARD];        // the core's cards
bool clip_replay_session;
static u64 now_ms = 1000;
static char last_osd[160];
static int osd_count, real_loads;
static char real_path[600];
static int failures, checks;
static char ctx[120];
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #c, ctx); } } while (0)

void *retro_get_memory_data(unsigned id) { (void)id; return ram; }
size_t retro_get_memory_size(unsigned id) { (void)id; return sizeof(ram); }
u64 osGetTime(void) { return now_ms; }
void log_printf(const char *fmt, ...) { (void)fmt; }
void emu_osd(const char *text) { snprintf(last_osd, sizeof(last_osd), "%s", text); osd_count++; }
static int alerts;
void emu_osd_alert(const char *text) { emu_osd(text); alerts++; }
void __real_LoadMcd(int mcd, char *str) { (void)mcd; real_loads++; snprintf(real_path, sizeof(real_path), "%s", str ? str : "(null)"); }
void __wrap_LoadMcd(int mcd, char *str);

// an SD card error on one file (run.sh links -Wl,--wrap=stat,--wrap=fopen)
enum { ON_STAT = 1, ON_OPEN = 2, ON_READ = 4 };
static struct { const char *name; int error; unsigned on; } sd_err;
static bool open_fail;   // every open for writing fails (cards_ownership.c)
static bool fd_is(FILE *f, const char *name)
{
	char link[64], got[700];
	snprintf(link, sizeof(link), "/proc/self/fd/%d", fileno(f));
	ssize_t n = readlink(link, got, sizeof(got) - 1);
	if (n <= 0) return false;
	got[n] = 0;
	size_t k = strlen(name);
	return (size_t)n > k && got[n - k - 1] == '/' && !strcmp(got + n - k, name);
}
int __real_stat(const char *path, struct stat *st);
int __wrap_stat(const char *path, struct stat *st)
{
	if (sd_err.name && (sd_err.on & ON_STAT) && !strcmp(path, sd_err.name)) { errno = sd_err.error; return -1; }
	return __real_stat(path, st);
}
FILE *__real_fopen(const char *path, const char *mode);
FILE *__wrap_fopen(const char *path, const char *mode)
{
	if (sd_err.name && (sd_err.on & ON_OPEN) && mode[0] == 'r' && !strcmp(path, sd_err.name)) { errno = sd_err.error; return NULL; }
	if (open_fail && strchr(mode, 'w')) { errno = ENOSPC; return NULL; }
	return __real_fopen(path, mode);
}

// safefile.c's fault hooks (SAFEFILE_TEST)
static int write_fail, rename_fail_all, close_fail;
int safe_test_rename(const char *from, const char *to)
{
	if (rename_fail_all || access(to, F_OK) == 0) return -1;
	return rename(from, to);
}
size_t safe_test_fwrite(const void *p, size_t size, size_t n, FILE *f) { return write_fail ? 0 : fwrite(p, size, n, f); }
size_t safe_test_fread(void *p, size_t size, size_t n, FILE *f)
{
	if (sd_err.name && (sd_err.on & ON_READ) && fd_is(f, sd_err.name)) { errno = sd_err.error; return 0; }
	return fread(p, size, n, f);
}
int safe_test_fclose(FILE *f) { int rc=fclose(f);return close_fail?-1:rc; }

// the PS1 checksums: byte 127 of the header and directory frames
static void sum(unsigned char *c)
{
	for (int f = 0; f < 16; f++) {
		unsigned char x = 0;
		for (int i = 0; i < 127; i++) x ^= c[f * 128 + i];
		c[f * 128 + 127] = x;
	}
}
static void blank(void *card)   // what the core starts with (formatted)
{
	memset(card, 0, CARD);
	memcpy(card, "MC", 2);
	for (int f = 1; f < 16; f++) ((unsigned char *)card)[f * 128] = 0xa0;
	sum(card);
}
static void pattern(void *card, int seed)
{
	unsigned char *c = card;
	for (int i = 0; i < CARD; i++) c[i] = (unsigned char)(i * seed + seed);
	c[0] = 'M'; c[1] = 'C';
	sum(c);
}
static void weaken(void *card)   // the header is fine, the directory is not
{
	((unsigned char *)card)[5 * 128 + 40] ^= 0x10;
}
static int history_count(const char *group, const char *ext)
{
	hist_entry_t e[32];
	int n = history_list(group, true, e, 32), k = 0;
	for (int i = 0; i < n; i++) k += !strcmp(e[i].ext, ext);
	return k;
}
static int count_kind(const char *group, hist_kind_t kind)
{
	hist_entry_t e[32];
	int n = history_list(group, true, e, 32), k = 0;
	for (int i = 0; i < n; i++) k += e[i].kind == kind;
	return k;
}
static void history_clear(void)
{
	const char *groups[] = { "hist/game", "hist/card2" };
	for (int g = 0; g < 2; g++) {
		DIR *d = opendir(groups[g]);
		struct dirent *de;
		char p[700];
		while (d && (de = readdir(d)))
			if (de->d_name[0] != '.') { snprintf(p, sizeof(p), "%s/%s", groups[g], de->d_name); remove(p); }
		if (d) closedir(d);
		rmdir(groups[g]);
	}
}
static void put(const char *path, const void *d, size_t n)
{
	FILE *f = fopen(path, "wb");
	fwrite(d, 1, n, f);
	fclose(f);
}
static long size_of(const char *path)
{
	struct stat st;
	return stat(path, &st) ? -1 : st.st_size;
}
static bool file_is(const char *path, const void *want)
{
	static unsigned char buf[CARD + 1];
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	size_t n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	return n == CARD && !memcmp(buf, want, CARD);
}
static void reset(void)
{
	cards_end();
	safe_remove("game.srm");
	safe_remove("card2.mcd");
	static const char *const bad[] = { "card2.mcd.bad", "card2.mcd.bad2", "game.srm.bad", "game.srm.bad2",
		"game.srm.bad3", "game.srm.bad4", "game.srm.bad5", "game.srm.bad6", "game.srm.tmp.bad", "game.srm.bak.bad" };
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		remove(bad[i]);   // safe_remove keeps every .bad
	safe_test_crash_after = -1;
	write_fail = rename_fail_all = 0;
	clip_replay_session = false;
	osd_count = real_loads = alerts = 0;
	history_clear();
	last_osd[0] = 0;
	blank(ram);
	blank(Mcd1Data);
	blank(Mcd2Data);
}

static void card1(void)
{
	unsigned char a[CARD], b[CARD];
	pattern(a, 3);
	pattern(b, 5);

	// no file: blank card, nothing written until the game changes it
	reset();
	snprintf(ctx, sizeof(ctx), "new card");
	unsigned toggles = host_pad_toggles;
	cards_begin("game.srm");
	CHECK(host_pad_toggles - toggles == 2);   // the pad never switches to analog by itself
	CHECK(!cards_check() && size_of("game.srm") < 0);
	memcpy(ram, a, CARD);
	CHECK(cards_check() && file_is("game.srm", a));
	memcpy(ram, b, CARD);
	CHECK(cards_check() && file_is("game.srm", b) && file_is("game.srm.bak", a));
	CHECK(!cards_check());   // unchanged: no write

	// wrong size, no backup: not loaded (empty card), reported before the
	// game runs (F1e) and never written while unanswered; the player plays
	// on: the file is kept and becomes .bad (byte for byte) at the first write
	reset();
	put("game.srm", "garbage", 7);
	snprintf(ctx, sizeof(ctx), "wrong size");
	cards_begin("game.srm");
	unsigned char empty[CARD];
	char label[600];
	blank(empty);
	CHECK(!memcmp(ram, empty, CARD) && size_of("game.srm") == 7);
	CHECK(cards_trouble(label, sizeof(label)) == CARD_TROUBLE_DAMAGED && !strcmp(label, "game.srm"));
	memcpy(ram, a, CARD);
	CHECK(!cards_check() && size_of("game.srm") == 7);   // locked: nothing written
	cards_save_now();
	CHECK(size_of("game.srm") == 7);
	CHECK(!cards_retry());   // still damaged
	cards_accept();
	CHECK(cards_trouble(label, sizeof(label)) == CARD_TROUBLE_NONE);
	CHECK(cards_check() && file_is("game.srm", a) && size_of("game.srm.bad") == 7);

	// J-02: damaged, no .bak, but Backups holds good copies: the newest good one is
	// offered and put back; the damaged file is kept as .bad, the copy stays in Backups
	reset();
	{
		unsigned char cut[40000], weak[CARD];
		memcpy(cut, a, sizeof(cut));
		memcpy(weak, b, CARD); weaken(weak);
		history_fake_now = 1000;
		CHECK(history_add("game", HIST_CARD_DAY, ".srm", a, CARD));
		history_fake_now = 2000;
		CHECK(history_add("game", HIST_CARD_SESSION, ".srm", b, CARD));
		history_fake_now = 3000;
		CHECK(history_add("game", HIST_CARD_PIN, ".srm", weak, CARD));   // newest, but weak
		history_fake_now = 0;
		put("game.srm", cut, sizeof(cut));
		snprintf(ctx, sizeof(ctx), "damaged card, copies in Backups");
		cards_begin("game.srm");
		CHECK(cards_trouble(label, sizeof(label)) == CARD_TROUBLE_DAMAGED);
		hist_entry_t copy;
		CHECK(cards_trouble_backup(&copy) && copy.when == 2000 && copy.kind == HIST_CARD_SESSION);
		CHECK(cards_trouble_restore(&copy));
		CHECK(cards_trouble(label, sizeof(label)) == CARD_TROUBLE_NONE && !memcmp(ram, b, CARD));
		CHECK(file_is("game.srm", b) && size_of("game.srm.bad") == (long)sizeof(cut));
		CHECK(history_count("game", ".srm") == 3);
		memcpy(ram, a, CARD);
		CHECK(cards_check() && file_is("game.srm", a));   // unlocked: saves again
		// no copy at all: nothing offered
		reset();
		put("game.srm", cut, sizeof(cut));
		cards_begin("game.srm");
		CHECK(cards_trouble(label, sizeof(label)) == CARD_TROUBLE_DAMAGED && !cards_trouble_backup(&copy));
	}

	// review finding 1: a second (third) damaged card is put back with older .bad files
	// there: none is removed, each damaged file takes the next free name; then the
	// player takes an empty card: kept again
	reset();
	{
		char label2[600];
		hist_entry_t copy;
		put("game.srm.bad", "older", 5);
		put("game.srm.bad2", "oldest!", 7);
		history_fake_now = 2000;
		CHECK(history_add("game", HIST_CARD_SESSION, ".srm", b, CARD));
		history_fake_now = 0;
		for (int round = 0; round < 2; round++) {
			snprintf(ctx, sizeof(ctx), "repeated restore %d", round);
			put("game.srm", "garbage!!", 9 + round);
			cards_end();
			cards_begin("game.srm");
			CHECK(cards_trouble(label2, sizeof(label2)) == CARD_TROUBLE_DAMAGED && cards_trouble_backup(&copy));
			CHECK(cards_trouble_restore(&copy) && !memcmp(ram, b, CARD) && file_is("game.srm", b));
			CHECK(size_of(round ? "game.srm.bad4" : "game.srm.bad3") == 9 + round);
		}
		snprintf(ctx, sizeof(ctx), "empty card after restores");
		put("game.srm", "garbage", 7);
		cards_end();
		cards_begin("game.srm");
		cards_accept();
		memcpy(ram, a, CARD);
		CHECK(cards_check() && file_is("game.srm", a) && size_of("game.srm.bad5") == 7);
		CHECK(size_of("game.srm.bad") == 5 && size_of("game.srm.bad2") == 7 && size_of("game.srm.bad3") == 9);
	}
	// review finding 2 (PS1 side): a power cut at every step of Restore: the next start
	// still finds the damaged card (asked again) or the restored copy, never a blank card
	for (int cut = 0; ; cut++) {
		reset();
		char label2[600];
		hist_entry_t copy;
		snprintf(ctx, sizeof(ctx), "restore cut %d", cut);
		put("game.srm.bad", "older", 5);
		history_fake_now = 2000;
		CHECK(history_add("game", HIST_CARD_SESSION, ".srm", b, CARD));
		history_fake_now = 0;
		put("game.srm", "garbage", 7);
		put("game.srm.tmp", "cut-off", 7);
		cards_begin("game.srm");
		CHECK(cards_trouble_backup(&copy));
		safe_test_crash_after = cut;
		bool ok = cards_trouble_restore(&copy);
		bool cut_off = safe_test_crash_after == -2;
		safe_test_crash_after = -1;
		cards_end();
		blank(ram);
		cards_begin("game.srm");   // the next start
		card_trouble_t t = cards_trouble(label2, sizeof(label2));
		CHECK(size_of("game.srm.bad") == 5);
		if (!cut_off) {
			CHECK(ok && t == CARD_TROUBLE_NONE && !memcmp(ram, b, CARD) && size_of("game.srm.bad2") == 7);
			CHECK(cut > 3);
			break;
		}
		CHECK(!ok && ((t == CARD_TROUBLE_DAMAGED && size_of("game.srm") == 7) || (t == CARD_TROUBLE_NONE && !memcmp(ram, b, CARD))));
		if (cut > 20) { CHECK(0); break; }
	}

	// "Try again" after the file was fixed: loaded, unlocked
	reset();
	put("game.srm", "garbage", 7);
	snprintf(ctx, sizeof(ctx), "retry");
	cards_begin("game.srm");
	CHECK(cards_trouble(label, sizeof(label)) == CARD_TROUBLE_DAMAGED);
	put("game.srm", b, CARD);
	CHECK(cards_retry() && !memcmp(ram, b, CARD) && cards_trouble(label, sizeof(label)) == CARD_TROUBLE_NONE);
	memcpy(ram, a, CARD);
	CHECK(cards_check() && file_is("game.srm", a) && file_is("game.srm.bak", b));

	// F1b: header intact, directory garbage, good backup: the backup is
	// loaded, the damaged file kept as .bad, the backup never pushed out
	reset();
	unsigned char w[CARD];
	memcpy(w, a, CARD);
	weaken(w);
	put("game.srm", w, CARD);
	put("game.srm.bak", b, CARD);
	snprintf(ctx, sizeof(ctx), "weak card, good backup");
	cards_begin("game.srm");
	CHECK(!memcmp(ram, b, CARD) && file_is("game.srm", b) && file_is("game.srm.bad", w) && alerts == 1);
	CHECK(file_is("game.srm.bak", b));
	// ... and if the file goes bad during the session, the next save keeps
	// it as .bad, not as the backup
	put("game.srm", w, CARD);
	memcpy(ram, a, CARD);
	CHECK(cards_check() && file_is("game.srm", a) && file_is("game.srm.bak", b) && file_is("game.srm.bad", w));

	// weak card, no good copy at all: loaded as it is (the game may read
	// most of it), the player told; saving rotates it as usual
	reset();
	put("game.srm", w, CARD);
	snprintf(ctx, sizeof(ctx), "weak card alone");
	cards_begin("game.srm");
	CHECK(!memcmp(ram, w, CARD) && alerts == 1 && strstr(last_osd, "looks damaged"));
	CHECK(cards_trouble(label, sizeof(label)) == CARD_TROUBLE_NONE);
	memcpy(ram, a, CARD);
	CHECK(cards_check() && file_is("game.srm", a) && file_is("game.srm.bak", w));

	// backups (history.h): a session copy of the card as loaded (newest
	// only), the first card of each day, a pinned copy before the game
	// empties it, and putting a copy back
	reset();
	put("game.srm", a, CARD);
	snprintf(ctx, sizeof(ctx), "card copies");
	cards_begin("game.srm");
	CHECK(history_count("game", ".srm") == 2);   // session + today
	cards_end();
	pattern(b, 5);
	put("game.srm", b, CARD);
	cards_begin("game.srm");
	{
		hist_entry_t e[8];
		int n = history_list("game", true, e, 8);
		CHECK(n == 2 && e[0].kind == HIST_CARD_SESSION && file_is(e[0].path, b));
		CHECK(e[1].kind == HIST_CARD_DAY && file_is(e[1].path, a));   // today's first card
	}
	cards_end();
	history_fake_now = time(NULL) + 2 * 86400;   // two days later
	cards_begin("game.srm");
	CHECK(count_kind("game", HIST_CARD_DAY) == 2 && count_kind("game", HIST_CARD_SESSION) == 1);
	// formatted by the game: the SD card's version is pinned first
	unsigned char saves[CARD];
	blank(saves);
	for (int f = 1; f <= 4; f++) saves[f * 128] = 0x51;   // four saves
	sum(saves);
	memcpy(ram, saves, CARD);
	CHECK(cards_check() && count_kind("game", HIST_CARD_PIN) == 0);   // more saves: no pin
	blank(ram);
	alerts = 0;
	CHECK(cards_check() && count_kind("game", HIST_CARD_PIN) == 1 && alerts == 1);
	{
		hist_entry_t e[16];
		int n = history_list("game", true, e, 16);
		CHECK(n >= 1 && e[0].kind == HIST_CARD_PIN && file_is(e[0].path, saves) && card_saves(saves, CARD) == 4);
		// put it back: the card now is pinned too, the copy written and in memory
		CHECK(cards_put_back(&e[0]));
		CHECK(!memcmp(ram, saves, CARD) && file_is("game.srm", saves) && count_kind("game", HIST_CARD_PIN) == 2);
		CHECK(!cards_check());   // the card in memory is what the SD card holds
	}
	history_fake_now = 0;
	// replay sessions make no copies
	reset();
	put("game.srm", a, CARD);
	clip_replay_session = true;
	cards_begin("game.srm");
	CHECK(history_count("game", ".srm") == 0);

	// cut-off card, good backup: the backup is loaded and put back
	reset();
	put("game.srm", a, CARD / 2);
	put("game.srm.bak", b, CARD);
	snprintf(ctx, sizeof(ctx), "backup");
	cards_begin("game.srm");
	CHECK(!memcmp(ram, b, CARD) && file_is("game.srm", b) && strstr(last_osd, "restored"));
	CHECK(!cards_check());

	// the repair fails: loaded, told so, and NOT taken as saved: the next
	// check writes it again without any change
	reset();
	put("game.srm", a, 10);
	put("game.srm.bak", b, CARD);
	snprintf(ctx, sizeof(ctx), "failed repair");
	write_fail = rename_fail_all = 1;
	cards_begin("game.srm");
	CHECK(!memcmp(ram, b, CARD) && strstr(last_osd, "could not fix"));
	write_fail = rename_fail_all = 0;
	CHECK(cards_check() && file_is("game.srm", b) && file_is("game.srm.bak", b));
	CHECK(size_of("game.srm.bad") == 10);

	// a failing write: reported, retried after the delay, at exit at once
	reset();
	snprintf(ctx, sizeof(ctx), "write fails");
	cards_begin("game.srm");
	memcpy(ram, a, CARD);
	write_fail = 1;
	CHECK(!cards_check() && strstr(last_osd, "Could not save") && size_of("game.srm") < 0);
	write_fail = 0;
	int shown = osd_count;
	CHECK(!cards_check() && osd_count == shown);   // waits CARD_RETRY_MS
	now_ms += 31 * 1000;
	CHECK(cards_check() && file_is("game.srm", a));
	memcpy(ram, b, CARD);
	write_fail = 1;
	CHECK(!cards_check());
	write_fail = 0;
	cards_save_now();               // leaving the game: tried at once
	CHECK(file_is("game.srm", b));

	// input-replay sessions never write the player's card
	reset();
	snprintf(ctx, sizeof(ctx), "replay");
	clip_replay_session = true;
	cards_begin("game.srm");
	memcpy(ram, a, CARD);
	CHECK(!cards_check());
	cards_save_now();
	CHECK(size_of("game.srm") < 0);
}

static void card2(void)
{
	unsigned char a[CARD], b[CARD], empty[CARD];
	pattern(a, 7);
	pattern(b, 9);
	blank(empty);
	char path[600];

	// no file yet: the core's empty card stays, its file name is taken away
	// (memory only); PSX3D writes the card when the game changes it
	reset();
	snprintf(ctx, sizeof(ctx), "card 2 new");
	cards_begin("game.srm");
	snprintf(path, sizeof(path), "card2.mcd");
	__wrap_LoadMcd(2, path);
	CHECK(path[0] == 0 && real_loads == 1 && real_path[0] == 0);
	CHECK(!memcmp(Mcd2Data, empty, CARD) && size_of("card2.mcd") < 0);
	CHECK(!cards_check());
	memcpy(Mcd2Data, a, CARD);
	CHECK(cards_check() && file_is("card2.mcd", a));
	memcpy(Mcd2Data, b, CARD);
	cards_save_now();
	CHECK(file_is("card2.mcd", b) && file_is("card2.mcd.bak", a));

	// existing card loads; damaged one with a backup: the backup
	reset();
	put("card2.mcd", a, CARD);
	snprintf(ctx, sizeof(ctx), "card 2 load");
	snprintf(path, sizeof(path), "card2.mcd");
	__wrap_LoadMcd(2, path);
	CHECK(!memcmp(Mcd2Data, a, CARD));
	reset();
	put("card2.mcd", a, 100);
	put("card2.mcd.bak", b, CARD);
	snprintf(path, sizeof(path), "card2.mcd");
	__wrap_LoadMcd(2, path);
	CHECK(!memcmp(Mcd2Data, b, CARD) && file_is("card2.mcd", b) && size_of("card2.mcd.bad") == 100);

	// a damaged card without a backup is not read (empty card), reported,
	// locked until the player answers, and kept
	reset();
	put("card2.mcd", a, 5000);
	snprintf(ctx, sizeof(ctx), "card 2 damaged");
	snprintf(path, sizeof(path), "card2.mcd");
	__wrap_LoadMcd(2, path);
	char label[600];
	CHECK(!memcmp(Mcd2Data, empty, CARD) && size_of("card2.mcd") == 5000);
	CHECK(cards_trouble(label, sizeof(label)) == CARD_TROUBLE_DAMAGED && !strcmp(label, "card2.mcd"));
	memcpy(Mcd2Data, a, CARD);
	CHECK(!cards_check() && size_of("card2.mcd") == 5000);
	cards_accept();
	CHECK(cards_check() && file_is("card2.mcd", a) && size_of("card2.mcd.bad") == 5000);
	// its copies live under the card's own name
	cards_end();
	snprintf(path, sizeof(path), "card2.mcd");
	__wrap_LoadMcd(2, path);
	CHECK(history_count("card2", ".mcd") == 2 && history_count("game", ".mcd") == 0);

	// Header-bearing files use the same atomic owner and retain their prefix.
	reset();
	unsigned char *gme = calloc(1, CARD + 3904);blank(gme+3904);
	put("card2.mcd", gme, CARD + 3904);
	free(gme);
	snprintf(ctx, sizeof(ctx), "card 2 header file");
	snprintf(path, sizeof(path), "card2.mcd");
	__wrap_LoadMcd(2, path);
	CHECK(!path[0] && !real_path[0]);
	memcpy(Mcd2Data, a, CARD);
	CHECK(cards_check() && size_of("card2.mcd") == CARD + 3904);
	snprintf(path, sizeof(path), "none");
	__wrap_LoadMcd(2, path);
	CHECK(!strcmp(real_path, "none"));
}

// Harsh review 0.9.0 finding 1: an SD card error while a card is read (stat, open or read; EIO or
// EACCES) is never a new empty card. The card is reported and locked: nothing the game does is
// written over the file, not even after "play on"; once the card answers, Try again loads it.
static void sd_errors(void)
{
	static const unsigned kinds[] = { ON_STAT, ON_OPEN, ON_STAT | ON_OPEN, ON_READ };
	static const int errors[] = { EIO, EACCES };
	unsigned char a[CARD], b[CARD], empty[CARD];
	pattern(a, 11);
	pattern(b, 13);
	blank(empty);
	char label[600], path[600];
	for (int e = 0; e < 2; e++)
		for (int k = 0; k < 4; k++) {
			reset();
			snprintf(ctx, sizeof(ctx), "card 1 SD error %d kind %u", errors[e], kinds[k]);
			put("game.srm", a, CARD);
			sd_err.name = "game.srm"; sd_err.error = errors[e]; sd_err.on = kinds[k];
			cards_begin("game.srm");
			CHECK(cards_trouble(label, sizeof(label)) == CARD_TROUBLE_MEMORY && !memcmp(ram, empty, CARD));
			memcpy(ram, b, CARD);
			CHECK(!cards_check());
			cards_accept();   // "play without saving": still never written over
			CHECK(!cards_check() && cards_save_now() != CARD_WRITE_SAVED);
			sd_err.name = NULL;
			CHECK(file_is("game.srm", a) && size_of("game.srm.bak") < 0 && size_of("game.srm.bad") < 0);
			// the card answers again: the next game start reads it
			cards_end();
			cards_begin("game.srm");
			CHECK(cards_trouble(label, sizeof(label)) == CARD_TROUBLE_NONE && !memcmp(ram, a, CARD));

			reset();
			snprintf(ctx, sizeof(ctx), "card 2 SD error %d kind %u", errors[e], kinds[k]);
			put("card2.mcd", a, CARD);
			cards_begin("game.srm");
			sd_err.name = "card2.mcd"; sd_err.error = errors[e]; sd_err.on = kinds[k];
			snprintf(path, sizeof(path), "card2.mcd");
			__wrap_LoadMcd(2, path);
			CHECK(cards_trouble(label, sizeof(label)) == CARD_TROUBLE_MEMORY && !memcmp(Mcd2Data, empty, CARD));
			memcpy(Mcd2Data, b, CARD);
			CHECK(!cards_check());
			CHECK(!cards_retry());   // still erring: still locked
			sd_err.name = NULL;
			CHECK(file_is("card2.mcd", a) && size_of("card2.mcd.bak") < 0);
			CHECK(cards_retry() && !memcmp(Mcd2Data, a, CARD));   // Try again, the card answers
		}
}

// A core card file with a header (prefix 64: VGS, 3904: DexDrive; 0: raw) around pattern seed.
static unsigned char *card_file(size_t prefix, int seed)
{
	unsigned char *f = calloc(1, CARD + prefix);
	for (size_t i = 0; i < prefix; i++) f[i] = (unsigned char)(i % 251);
	pattern(f + prefix, seed);
	return f;
}
static void put_card(const char *path, size_t prefix, int seed)
{
	unsigned char *f = card_file(prefix, seed);
	put(path, f, CARD + prefix);
	free(f);
}

// Review A findings 1 and 4 (ASan): Try again after the card file changed its format (header
// sizes both ways, raw and header) never writes past the image; a valid card is never refused
// because its other copy (file or backup) cannot be sized; each fallback direction loads.
static void card_formats(void)
{
	static const size_t prefixes[] = { 0, 64, 3904 };
	unsigned char want[CARD], other[CARD];
	pattern(want, 21);
	pattern(other, 23);
	char path[600], label[600];
	for (int from = 0; from < 3; from++)
		for (int to = 0; to < 3; to++) {
			if (from == to) continue;
			reset();
			snprintf(ctx, sizeof(ctx), "retry after the format changed: prefix %zu -> %zu", prefixes[from], prefixes[to]);
			put_card("card2.mcd", prefixes[from], 23);
			cards_begin("game.srm");
			sd_err.name = "card2.mcd"; sd_err.error = EIO; sd_err.on = ON_OPEN;
			snprintf(path, sizeof(path), "card2.mcd");
			__wrap_LoadMcd(2, path);
			CHECK(cards_trouble(label, sizeof(label)) == CARD_TROUBLE_MEMORY);
			sd_err.name = NULL;
			put_card("card2.mcd", prefixes[to], 21);
			CHECK(cards_retry() && !memcmp(Mcd2Data, want, CARD));
			memcpy(Mcd2Data, other, CARD);
			CHECK(cards_check());   // written in its new format, header kept
			unsigned char *expect = card_file(prefixes[to], 23);
			FILE *f = fopen("card2.mcd", "rb");
			unsigned char *got = malloc(CARD + 3904 + 1);
			size_t n = f ? fread(got, 1, CARD + 3904 + 1, f) : 0;
			if (f) fclose(f);
			CHECK(n == CARD + prefixes[to] && !memcmp(got, expect, n));
			free(got);
			free(expect);
		}
	for (int p = 0; p < 3; p++) {
		// a valid card beside a backup the SD card cannot stat: the card loads
		reset();
		snprintf(ctx, sizeof(ctx), "valid card (prefix %zu), backup stat error", prefixes[p]);
		put_card("card2.mcd", prefixes[p], 21);
		put_card("card2.mcd.bak", prefixes[p], 23);
		cards_begin("game.srm");
		sd_err.name = "card2.mcd.bak"; sd_err.error = EIO; sd_err.on = ON_STAT;
		snprintf(path, sizeof(path), "card2.mcd");
		__wrap_LoadMcd(2, path);
		sd_err.name = NULL;
		CHECK(cards_trouble(label, sizeof(label)) == CARD_TROUBLE_NONE && !memcmp(Mcd2Data, want, CARD));
		// a card file the SD card cannot stat beside a valid backup: the backup loads, the file
		// stays at its name (never set aside while it cannot be read)
		reset();
		snprintf(ctx, sizeof(ctx), "card stat error (prefix %zu), valid backup", prefixes[p]);
		put_card("card2.mcd", prefixes[p], 23);
		put_card("card2.mcd.bak", prefixes[p], 21);
		cards_begin("game.srm");
		sd_err.name = "card2.mcd"; sd_err.error = EIO; sd_err.on = ON_STAT;
		snprintf(path, sizeof(path), "card2.mcd");
		__wrap_LoadMcd(2, path);
		CHECK(cards_trouble(label, sizeof(label)) == CARD_TROUBLE_NONE && !memcmp(Mcd2Data, want, CARD));
		sd_err.name = NULL;
		unsigned char *expect = card_file(prefixes[p], 23);
		FILE *f = fopen("card2.mcd", "rb");
		unsigned char *got = malloc(CARD + 3904 + 1);
		size_t n = f ? fread(got, 1, CARD + 3904 + 1, f) : 0;
		if (f) fclose(f);
		CHECK(n == CARD + prefixes[p] && !memcmp(got, expect, n) && size_of("card2.mcd.bad") < 0);
		free(got);
		free(expect);
	}
}

int main(void)
{
	char dir[4096];
	if (!test_mkdtemp(dir, sizeof(dir), "psx3d-cards") || chdir(dir)) return 2;
	history_set_root("hist");
	card1();
	card2();
	sd_errors();
	card_formats();
	reset();
	remove("game.srm.bad");
	rmdir("hist");
	rmdir(dir);
	printf("memory cards: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
