#include "fs_path.h"
#include "bb_update.h"
#include "room.h"
#include "breadbox_import.h"
#include "ht_trace.h"
// Box art. A low-priority worker thread fetches covers from the free
// libretro-thumbnails database, decodes them, shrinks them
// to fit 256x256 (keeping their aspect) and caches that on the SD card as a
// finished GPU texture (RGB565, tiled; see covers_pack.c),
// so each cover is downloaded and decoded once and the UI only copies it.
// The database names pictures after redump names, but a game's file can be
// called anything: boxart.c finds the picture by file name, disc serial or
// title (see boxart.h). A picture the database does not have is remembered
// (covers/state.txt), so it is not asked for at every start.
// A player's own PNG, covers/<file name without extension>.png, wins over
// every download. Covers cached by older versions (128x128 RGBA8 .bin) show at
// once and are replaced by a full size download in the background.
// The UI asks for the covers around the selection; ready ones get uploaded
// to textures on the main thread.
// Every cached cover also has a 64x64 copy; all of them live in one file
// (covers/thumbs.pk) that is read once when the library opens. A cover that is
// not loaded yet shows its small copy at once, so scrolling fast or jumping
// to a letter never shows empty cards while the full size ones come from the
// SD card.
#include <ctype.h>
#include <stdatomic.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <strings.h>
#include <curl/curl.h>
#include "covers_png.h"
#include "covers_pack.h"
#include "boxart.h"
#include "safefile.h"
#include "app_paths.h"
#include "app_log.h"
#include "platform_video.h"
#include "dev_switches.h"
#include "library.h"
#include "covers.h"
#include "app_main.h"
#include "devtools.h"
#include "presetupdate.h"
#include "bb_registry.h"
#include "bb_index.h"

// External IDs never overlap PS1 indexes. Borrowed records obey the same
// covers_library_begin/end publication boundary as the PS1 library.
typedef struct {game_t game;
 char title[BB_TITLE_LEN],key[256],path[PATH_LEN];
} cover_record;
static const game_t *cover_game(int id, cover_record *scratch)
{
 if(id<0)return NULL;
 if(id>=65536) {
  unsigned system=(unsigned)id/65536-1;
  const bb_system_t *s=system<bb_system_count?bb_systems[system]:NULL;
  if(!s)return NULL;
  if(s->cover_item) {
   bb_cover_item it={0};
   if(!s->cover_item(id%65536,&it))return NULL;
   scratch->game=(game_t){.title=(char *)it.title,.cover_key=(char *)it.key,.group=(char *)it.code,.ndiscs=it.path!=NULL};
   scratch->game.discs[0]=(char *)it.path;return &scratch->game;
  }
  bb_entry entry;
  if(!bb_index_get(s,id%65536,scratch->path,sizeof(scratch->path),&entry))return NULL;
  snprintf(scratch->title,sizeof(scratch->title),"%s",entry.title);
  snprintf(scratch->key,sizeof(scratch->key),"%s/%s",s->id,entry.cover[0]?entry.cover:entry.title);
  scratch->game=(game_t){.title=scratch->title,.cover_key=scratch->key,.ndiscs=1};
  scratch->game.discs[0]=scratch->path;return &scratch->game;
 }
 return library_get(id);
}


#define COVER_DIR PSX3D_DIR "/covers"
#define COVER_STATE COVER_DIR "/state.txt"
#ifndef BOXART_DIR
#define BOXART_DIR "romfs:/covers"   // names.txt, serials.idx (tools/boxart/make_table.py)
#endif
#ifndef THUMB_URL
#define THUMB_URL "https://thumbnails.libretro.com/Sony%20-%20PlayStation/Named_Boxarts/"
#endif
#ifndef BB_THUMB_ROOT
#define BB_THUMB_ROOT "https://thumbnails.libretro.com/"
#endif
#define SLOTS 24

// cache file: a header, then covers_pack's COVER_PACK_BYTES
#define CV2_MAGIC 0x32564350u   // "PCV2"
#define CV2_BYTES (16 + COVER_PACK_BYTES)
typedef struct { u32 magic; u16 w, h; u32 levels, bytes; } cv2_head_t;

typedef struct { u8 *data; u16 w, h; bool lowres; } cover_t;

typedef struct {
	int game;            // library index, -1 = free
	volatile int state;  // 0 empty, 1 wanted, 2 pixels ready, 3 uploaded, 4 failed, 5 loading
	u8 *pixels;          // COVER_PACK_BYTES until uploaded (then freed)
	u16 w, h;            // the picture's size inside the texture
	C3D_Tex tex;
	bool tex_ok;
	bool shown;          // the texture holds this game's cover (it can be drawn)
	bool lowres;         // it is the old 128x128 cover: fetch the full size one
	bool upgrading;      // a worker is on it (state stays 3: the old picture stays up)
	bool refresh;        // developer-requested HTTPS refresh; keep disk cache until success
	u32 last_used;
	u64 retry_at;        // transient failures retry without scrolling away
	u32 rank;            // order the UI asked for it in its latest frame (0 = selected)
} slot_t;

static slot_t slots[SLOTS];

#define THUMB_FILE COVER_DIR "/thumbs.pk"
#define THUMB_MAGIC 0x31485450u   // "PTH1"
#define TSLOTS 40                 // small cover textures (8 KiB each)
typedef struct { u32 magic, count, bytes, zero; } thumb_head_t;
typedef struct { u64 key; u16 w, h; u32 zero; u8 px[COVER_THUMB_BYTES]; } thumb_t;   // also the file record
static thumb_t **thumbs;          // sorted by key (under lock)
static int nthumbs, thumbs_cap;
static atomic_bool thumbs_loaded, thumbs_loading;
static bool thumbs_dirty;
static u64 thumbs_dirty_at;
static u8 *thumb_tried;           // per game: the background fill has looked at it
static int thumb_tried_n;
static u64 *own_pngs;             // the player's PNGs in the covers folder (name hashes, sorted)
static int nown_pngs;
static atomic_bool own_pngs_ok;          // the folder was listed
static int focus_game = -1;
typedef struct { int game; u32 used; u16 w, h, tw, th; C3D_Tex tex; bool tex_ok; } tslot_t;
static tslot_t tslots[TSLOTS];
static u32 pump_frame;            // frames the covers were drawn in
static int thumb_uploads_left;    // this frame's budget
static unsigned thumb_made, thumb_filled, thumb_load_ms, thumb_save_ms;
#define WORKERS 1   // one shared PS1/GBA queue and downloader
static Thread workers[WORKERS];
static LightLock lock;
static void covers_lock(void) {
#ifdef HT_TRACE
 uint64_t t=ht_begin();
 LightLock_Lock(&lock);
 if(t && svcGetSystemTick()-t>SYSCLOCK_ARM11*3/10000)ht_mark("covers lock ticks",(u32)(svcGetSystemTick()-t));
#else
 LightLock_Lock(&lock);
#endif
}

static LightEvent wake;
static atomic_bool quit;
static u64 net_retry_at;   // after a network failure, wait before trying again
static u64 net_backoff;    // that wait: 15 s, doubling to 10 min, 0 after a success
static LightLock state_lock;   // boxart.c's memory of misses and own pictures
static bool apt_was_paused;
static unsigned apt_suspended;
static unsigned attempts, downloaded, cache_hits, aborted_count, failures, upgraded;
static unsigned miss_skips, other_names;   // not asked for (remembered miss); fetched under another name
static unsigned uploads, upload_max_us, upload_total_us;   // main thread: texture copies
// library frame intervals (a developer diagnostic: is the carousel smooth?)
static u64 frame_last;
static unsigned frames, frames_over_25ms, frames_over_40ms, frame_max_us;
static u64 frame_total_us;
// how fast covers appear while scrolling (developer diagnostics, "covers scroll=")
bool covers_slow;                  // "covers fast=off": as before small covers (an A/B in one build)
unsigned covers_sd_ms;             // "covers sdms=N": cache reads take N ms per 128 KB longer
                                   // (the console's SD card, in an emulator)

static void sd_model(size_t bytes)
{
	if (covers_sd_ms)
		svcSleepThread((long long)covers_sd_ms * 1000000LL * bytes / COVER_PACK_BYTES);
}
static int last_curl;
static long last_http;
static unsigned stop_ms, max_stop_ms;
static atomic_bool paused; // gameplay / HOME / sleep: do not start work
static atomic_int busy;    // workers currently fetching/decoding a cover or presets
static bool initialized, library_was_paused;
static unsigned library_changes; // UI-owned; blocks resume until publication is complete
static bool try_open, try_was_paused;
static unsigned gpu_generation;
static bool presets_claimed; // a worker runs the preset check (under lock)
static aptHookCookie apt_cookie;
void covers_stop_wait(void);

// The system tells us about HOME, sleep and closing right away (from inside
// aptMainLoop). Stop network work immediately instead of whenever the main
// loop next looks.
static void on_apt(APT_HookType hook, void *param)
{
	log_printf("covers: apt hook %d, busy %d\n", hook, busy);
	(void)param;

	switch (hook) {
	case APTHOOK_ONSUSPEND:
	case APTHOOK_ONSLEEP:
		if (!apt_suspended) apt_was_paused = library_changes ? library_was_paused : paused;
		apt_suspended |= hook == APTHOOK_ONSUSPEND ? 1 : 2;
		covers_stop_wait();
		break;
	case APTHOOK_ONRESTORE:
	case APTHOOK_ONWAKEUP:
		apt_suspended &= ~(hook == APTHOOK_ONRESTORE ? 1u : 2u);
		if (apt_suspended) break;
        if(bb_update_installing() || bb_update_committed()){paused=true;break;}
		paused = apt_was_paused || library_changes;
		net_retry_at = osGetTime() + 1000;   // the network needs a moment back
		LightEvent_Signal(&wake);
		LightEvent_Signal(&wake);
		break;
	case APTHOOK_ONEXIT:
		quit = true;                         // aborts transfers in progress
		LightEvent_Signal(&wake);
		LightEvent_Signal(&wake);
		break;
	default:
		break;
	}
}
static u32 use_clock;
static volatile u32 frame_base;   // use_clock when the UI's current frame began (covers_focus)
static u32 *soc_buf;
static bool soc_started;

// The network. Only one thread at a time may use it (net_lock): all our
// socket calls share one session with the 3DS socket service, and a thread
// blocked in poll() there holds up every other socket call, ours and, while
// suspended, the HOME Menu's. That froze the 3DS on HOME. So curl is driven
// by hand with nonblocking polls and 10ms sleeps, and every socket
// is closed (net_close) before HOME or sleep.
static LightLock net_lock;
static CURL *easy;
static CURLM *multi;
static CURLSH *share;            // DNS and TLS sessions, kept across net_close
static volatile bool net_open;   // multi holds connections

// ---------------------------------------------------------------- names

// libretro-thumbnails replaces these characters with '_' in file names
static void thumb_name(const char *key, char *out, size_t len)
{
	size_t n = 0;
	for (const char *p = key; *p && n < len - 1; p++)
		out[n++] = strchr("&*/:`<>?\\|\"", *p) ? '_' : *p;
	out[n] = 0;
}

static void url_escape(const char *in, char *out, size_t len)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t n = 0;
	for (const unsigned char *p = (const unsigned char *)in; *p && n < len - 4; p++) {
		if (isalnum(*p) || strchr("-_.~", *p)) {
			out[n++] = *p;
		} else {
			out[n++] = '%';
			out[n++] = hex[*p >> 4];
			out[n++] = hex[*p & 15];
		}
	}
	out[n] = 0;
}

static void cache_path(const char *key, const char *ext, char *out, size_t len)
{
	char name[256];
 const char *slash=strchr(key,'/');
 char id[16];if(slash && (size_t)(slash-key)<sizeof(id)) {
  snprintf(id,sizeof(id),"%.*s",(int)(slash-key),key);
  if(!bb_system_by_id(id))return;
  thumb_name(slash+1,name,sizeof(name));
  char folder[512],file[280];
  if(snprintf(file,sizeof(file),"%s%s",name,ext)>=(int)sizeof(file) || !fs_path_join(folder,sizeof(folder),COVER_DIR,id) || !fs_path_join(out,len,folder,file))out[0]=0;
  return;
 }
	thumb_name(key, name, sizeof(name));
	char file[280];
 if(snprintf(file,sizeof(file),"%s%s",name,ext)>=(int)sizeof(file) || !fs_path_join(out,len,COVER_DIR,file))out[0]=0;
}

static u64 name_hash(const char *s)
{
	u64 h = 1469598103934665603ull;   // FNV-1a
	for (; *s; s++)
		h = (h ^ (u8)*s) * 1099511628211ull;
	return h;
}

// ---------------------------------------------------------------- small covers

// caller holds lock: the index of key, or where it would go (-1 - index)
static int thumb_find(u64 key)
{
	int lo = 0, hi = nthumbs;
	while (lo < hi) {
		int m = (lo + hi) / 2;
		if (thumbs[m]->key == key) return m;
		if (thumbs[m]->key < key) lo = m + 1;
		else hi = m;
	}
	return -1 - lo;
}

// caller holds lock; t is then owned by the list
static bool thumb_insert(thumb_t *t)
{
	int i = thumb_find(t->key);
	if (i >= 0) {
		free(thumbs[i]);
		thumbs[i] = t;
		return true;
	}
	i = -1 - i;
	if (nthumbs == thumbs_cap) {
		int cap = thumbs_cap ? thumbs_cap * 2 : 256;
		thumb_t **n = realloc(thumbs, cap * sizeof(*n));
		if (!n) return false;
		thumbs = n;
		thumbs_cap = cap;
	}
	memmove(thumbs + i + 1, thumbs + i, (nthumbs - i) * sizeof(*thumbs));
	thumbs[i] = t;
	nthumbs++;
	return true;
}

// A cover's small copy, made from the finished cover. replace: the cover
// changed (a download, the player's PNG); else only if there is none yet.
static void thumb_make(const char *key, const cover_t *c, bool replace)
{
	u64 k = name_hash(key);
	covers_lock();
	bool have = thumb_find(k) >= 0;
	LightLock_Unlock(&lock);
	if (have && !replace)
		return;
	thumb_t *t = malloc(sizeof(*t));
	if (!t)
		return;
	int tw, th;
	covers_thumb(c->data, c->w, c->h, t->px, &tw, &th);
	t->key = k;
	t->w = c->w;
	t->h = c->h;
	t->zero = 0;
	covers_lock();
	if (thumb_insert(t)) {
		if (!thumbs_dirty) thumbs_dirty_at = osGetTime();
		thumbs_dirty = true;
		for (int i = 0; i < TSLOTS; i++) {
   cover_record scratch; const game_t *g=cover_game(tslots[i].game,&scratch);
   if (g && name_hash(g->cover_key)==k) tslots[i].game=-1;
  }
	} else free(t);
	LightLock_Unlock(&lock);
	__atomic_add_fetch(&thumb_made, 1, __ATOMIC_RELAXED);
}

static void thumbs_free(void)
{
	covers_lock();
	for (int i = 0; i < nthumbs; i++)
		free(thumbs[i]);
	free(thumbs);
	thumbs = NULL;
	nthumbs = thumbs_cap = 0;
	thumbs_loaded = false;
	thumbs_dirty = false;
	LightLock_Unlock(&lock);
}

static int cmp_u64(const void *a, const void *b)
{
	u64 x = *(const u64 *)a, y = *(const u64 *)b;
	return x < y ? -1 : x > y;
}

// Which games have a PNG of the player's own: one look at the folder instead
// of a failed lookup in it for every cover (a SELECT rescan looks again).
static void own_pngs_scan(void)
{
	u64 *list = NULL;
	int n = 0, cap = 0;
	DIR *d = opendir(COVER_DIR);
	struct dirent *e;
	while (d && (e = readdir(d))) {
		// a big folder takes seconds on the Old 3DS: the menu never waits for it
		// (covers_stop_wait); looked at again when the worker goes on
		if (paused || quit) {
			closedir(d);
			free(list);
			return;
		}
		size_t len = strlen(e->d_name);
		if (len < 5 || strcasecmp(e->d_name + len - 4, ".png"))
			continue;
		if (n == cap) {
			u64 *m = realloc(list, (cap = cap ? cap * 2 : 32) * sizeof(*m));
			if (!m) break;
			list = m;
		}
		char name[256];
		snprintf(name, sizeof(name), "%.*s", (int)(len - 4), e->d_name);
		list[n++] = name_hash(name);
	}
	if (d) closedir(d);
	if (n)
		qsort(list, n, sizeof(*list), cmp_u64);
	covers_lock();
	free(own_pngs);
	own_pngs = list;
	nown_pngs = n;
	own_pngs_ok = d != NULL;
	LightLock_Unlock(&lock);
}

// false: the player has no PNG for this game (true: there may be one)
static bool own_png_maybe(const char *key)
{
	char name[256];
	thumb_name(key, name, sizeof(name));
	u64 h = name_hash(name);
	covers_lock();
	bool maybe = !own_pngs_ok || covers_slow || (own_pngs && bsearch(&h, own_pngs, nown_pngs, sizeof(*own_pngs), cmp_u64));
	LightLock_Unlock(&lock);
	return maybe;
}

// Read covers/thumbs.pk (worker). false: stopped by a pause, try again later.
static bool thumbs_load(void)
{
	u64 t0 = osGetTime();
	safe_recover(THUMB_FILE);
	FILE *f = fopen(THUMB_FILE, "rb");
	char *vbuf = f ? malloc(32 * 1024) : NULL;   // 32 KB SD requests, not stdio's 1 KB
	if (vbuf) setvbuf(f, vbuf, _IOFBF, 32 * 1024);
	thumb_head_t head;
	bool ok = f && fread(&head, sizeof(head), 1, f) == 1 && head.magic == THUMB_MAGIC &&
	          head.bytes == COVER_THUMB_BYTES && head.count <= 8192;
	int count = ok ? (int)head.count : 0;
	for (int i = 0; i < count; i++) {
		if (quit || paused) {
			fclose(f);
			free(vbuf);
			thumbs_free();
			return false;
		}
		thumb_t *t = malloc(sizeof(*t));
		if (!t || fread(t, sizeof(*t), 1, f) != 1 || !t->w || t->w > COVER_TEX || !t->h || t->h > COVER_TEX) {
			free(t);
			log_printf("covers: %s damaged at %d of %d, made again\n", THUMB_FILE, i, count);
			break;
		}
		covers_lock();
		if (!thumb_insert(t)) free(t);
		LightLock_Unlock(&lock);
	}
	if (f) fclose(f);
	free(vbuf);
	sd_model((size_t)nthumbs * sizeof(thumb_t));
	covers_lock();
	thumbs_loaded = true;
	thumbs_dirty = nthumbs != count;   // damaged or missing: write it again
	thumbs_dirty_at = 0;
	LightLock_Unlock(&lock);
	thumb_load_ms = (unsigned)(osGetTime() - t0);
	log_printf("covers: %d small covers in %ums\n", nthumbs, thumb_load_ms);
	return true;
}

// Write covers/thumbs.pk next to the old one, then put it in place (worker).
// A pause throws the new file away; it is written again later.
static void thumbs_save(void)
{
	u64 t0 = osGetTime();
	covers_lock();
	int count = nthumbs;
	thumbs_dirty = false;
	LightLock_Unlock(&lock);
	FILE *f = safe_begin(THUMB_FILE);
	thumb_head_t head = { THUMB_MAGIC, count, COVER_THUMB_BYTES, 0 };
	bool ok = f && fwrite(&head, sizeof(head), 1, f) == 1;
	for (int i = 0; ok && i < count; i++) {
		if (quit || paused) {
			ok = false;
			break;
		}
		// records only ever get added or replaced, never removed, while the
		// library is open: index i is still a whole record
		covers_lock();
		thumb_t rec = *thumbs[i < nthumbs ? i : nthumbs - 1];
		LightLock_Unlock(&lock);
		ok = fwrite(&rec, sizeof(rec), 1, f) == 1;
	}
	if (f) safe_finish(f, THUMB_FILE, ok);
	if (!ok) {
		covers_lock();
		thumbs_dirty = true;
		LightLock_Unlock(&lock);
	}
	thumb_save_ms = (unsigned)(osGetTime() - t0);
	log_printf("covers: %d small covers %s in %ums\n", count, ok ? "saved" : "not saved", thumb_save_ms);
}

// ---------------------------------------------------------------- decode

static u32 *decode_png(const void *data, size_t size, const char *file, int *w, int *h)
{
	return covers_png_decode(data, size, file, COVER_TEX, w, h);
}

// RGBA8 picture -> a finished cover (consumes nothing; the caller frees px)
static bool pack_cover(const u32 *px, int w, int h, cover_t *c)
{
	c->data = malloc(COVER_PACK_BYTES);
	if (!c->data)
		return false;
	covers_pack(px, w, h, c->data);
	c->w = w;
	c->h = h;
	c->lowres = false;
	return true;
}

static bool read_cv2(const char *path, cover_t *c)
{
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	cv2_head_t head;
	bool ok = fread(&head, sizeof(head), 1, f) == 1 && head.magic == CV2_MAGIC && head.w >= 1 &&
	          head.w <= COVER_TEX && head.h >= 1 && head.h <= COVER_TEX && head.levels == COVER_LEVELS &&
	          head.bytes == COVER_PACK_BYTES;
	c->data = ok ? malloc(COVER_PACK_BYTES) : NULL;
	ok = c->data && fread(c->data, 1, COVER_PACK_BYTES, f) == COVER_PACK_BYTES;
	fclose(f);
	sd_model(COVER_PACK_BYTES);
	if (!ok) {
		free(c->data);
		c->data = NULL;
		return false;
	}
	c->w = head.w;
	c->h = head.h;
	c->lowres = false;
	return true;
}

static void write_cv2(const char *path, const cover_t *c)
{
	FILE *f = fopen(path, "wb");
	if (!f) return;
	cv2_head_t head = { CV2_MAGIC, c->w, c->h, COVER_LEVELS, COVER_PACK_BYTES };
	bool ok = fwrite(&head, sizeof(head), 1, f) == 1 && fwrite(c->data, 1, COVER_PACK_BYTES, f) == COVER_PACK_BYTES;
	if (fclose(f) || !ok)
		remove(path);   // never leave a short file that looks like a cache
}

// The 128x128 RGBA8 cache of versions before this one (.bin, 64 KiB).
static bool read_legacy(const char *path, cover_t *c)
{
	const int n = COVER_LEGACY_SIZE * COVER_LEGACY_SIZE;
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	u32 *px = malloc(n * 4);
	bool ok = px && fread(px, 4, n, f) == (size_t)n;
	fclose(f);
	if (ok) {
		for (int i = 0; i < n; i++) {   // flatten like the decoder does
			u32 a = px[i] & 255;
			if (a == 255) continue;
			u32 r = ((px[i] >> 24) * a + COVER_BG_R * (255 - a) + 127) / 255;
			u32 g = ((px[i] >> 16 & 255) * a + COVER_BG_G * (255 - a) + 127) / 255;
			u32 b = ((px[i] >> 8 & 255) * a + COVER_BG_B * (255 - a) + 127) / 255;
			px[i] = r << 24 | g << 16 | b << 8 | 255;
		}
		ok = pack_cover(px, COVER_LEGACY_SIZE, COVER_LEGACY_SIZE, c);
	}
	free(px);
	return ok;
}

// ---------------------------------------------------------------- download

// Stage the compressed file on SD rather than retaining another 0.5 MiB in
// Old 3DS's small library heap while TLS and decoded covers are resident.
typedef struct { FILE *file; size_t len, limit; } cover_file_t;
static size_t on_data(void *p, size_t sz, size_t n, void *ud)
{
	cover_file_t *m = ud;
	if (sz && n > (m->limit - m->len) / sz) return 0;
	size_t add = sz * n;
	size_t wrote = fwrite(p, 1, add, m->file);
	m->len += wrote;
	return wrote;
}

// close every connection (caller holds net_lock)
static void net_close(void)
{
	if (multi)
		curl_multi_cleanup(multi);
	multi = NULL;
	net_open = false;
}

// caller holds net_lock
static int transfer_progress(void *unused, curl_off_t total, curl_off_t now,
                             curl_off_t up_total, curl_off_t up_now)
{
	(void)unused; (void)total; (void)now; (void)up_total; (void)up_now;
	return quit || paused;
}

// One transfer into a file on the SD card, at most limit bytes (caller holds
// net_lock). *aborted: HOME, sleep, exit or a game start stopped it.
static CURLcode fetch_file(const char *url, const char *path, size_t limit, long *code, bool *aborted)
{
	CURL *c = easy;
	*code = 0;
	*aborted = false;
	if (!c || (!multi && !(multi = curl_multi_init())))
		return CURLE_FAILED_INIT;
	cover_file_t m = { .file = fopen(path, "wb"), .limit = limit };
	if (!m.file) return CURLE_WRITE_ERROR;
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
	curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/certs.pem");
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, transfer_progress);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "BreadBox/" BB_VERSION);
	// the handle is reused for every cover: one TLS handshake (seconds on
	// the 3DS CPU) instead of one per cover
	curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
	curl_easy_setopt(c, CURLOPT_SHARE, share);
	curl_easy_setopt(c, CURLOPT_DNS_CACHE_TIMEOUT, -1L);   // lookups can't be aborted: do them once

	CURLcode rc = CURLE_ABORTED_BY_CALLBACK;
	int running = 1;
	curl_multi_add_handle(multi, c);
	net_open = true;
	__atomic_add_fetch(&attempts, 1, __ATOMIC_RELAXED);
	for (;;) {
		if (quit || paused) {   // HOME, sleep or exit: retried later
			*aborted = true;
			break;
		}
		if (curl_multi_perform(multi, &running) != CURLM_OK || !running)
			break;
		// Never wait inside SOC: a blocking poll monopolizes its shared IPC
		// session, including the developer server and HOME socket calls.
		curl_multi_poll(multi, NULL, 0, 0, NULL);
		svcSleepThread(10000000LL);
	}
	CURLMsg *msg;
	int left;
	while (!*aborted && (msg = curl_multi_info_read(multi, &left)))
		if (msg->msg == CURLMSG_DONE)
			rc = msg->data.result;
	if (rc == CURLE_ABORTED_BY_CALLBACK && (quit || paused)) *aborted = true;
	curl_multi_remove_handle(multi, c);
	if (*aborted)
		net_close();
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, code);
	{
		double tc = 0, ta = 0, tt = 0, sp = 0;
		curl_easy_getinfo(c, CURLINFO_CONNECT_TIME, &tc);
		curl_easy_getinfo(c, CURLINFO_APPCONNECT_TIME, &ta);
		curl_easy_getinfo(c, CURLINFO_TOTAL_TIME, &tt);
/* Preserve the validated release layout; this supported legacy query is diagnostic only. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
		curl_easy_getinfo(c, CURLINFO_SPEED_DOWNLOAD, &sp);
#pragma GCC diagnostic pop
		log_printf("covers: http %ld %uKB connect %.2fs tls %.2fs total %.2fs (%.0fKB/s)\n",
			*code, (unsigned)(m.len / 1024), tc, ta, tt, sp / 1024);
	}
	if (fclose(m.file) && rc == CURLE_OK) rc = CURLE_WRITE_ERROR;
	last_curl = rc;
	last_http = *code;
	if (*aborted) __atomic_add_fetch(&aborted_count, 1, __ATOMIC_RELAXED);
	return rc;
}

static bool network_error(CURLcode rc)
{
	return rc == CURLE_COULDNT_CONNECT || rc == CURLE_COULDNT_RESOLVE_HOST || rc == CURLE_OPERATION_TIMEDOUT;
}

// no Wi-Fi, no server: every try waits twice as long as the one before
static void net_failed(void)
{
	net_backoff = net_backoff ? net_backoff * 2 : 15000;
	if (net_backoff > 600000) net_backoff = 600000;
	net_retry_at = osGetTime() + net_backoff;
}

static u32 *download(const char *key, bool *retry, int *pw, int *ph)
{
	char name[256], esc[768], url[1024];
	thumb_name(key, name, sizeof(name));
	url_escape(name, esc, sizeof(esc));
	snprintf(url, sizeof(url), "%s%s.png", THUMB_URL, esc);
 const char *slash=strchr(key,'/');
 char id[16];
 if(slash && (size_t)(slash-key)<sizeof(id)) {
  snprintf(id,sizeof(id),"%.*s",(int)(slash-key),key);
  const bb_system_t *system=bb_system_by_id(id);
  if(!system || !system->cover_db)return NULL;
  char database[256];url_escape(system->cover_db,database,sizeof(database));
  thumb_name(slash+1,name,sizeof(name));url_escape(name,esc,sizeof(esc));
  snprintf(url,sizeof(url),BB_THUMB_ROOT "%s/Named_Boxarts/%s.png",database,esc);
 }

	long code;
	bool aborted;
	CURLcode rc = fetch_file(url, COVER_DIR "/download.tmp", 4 * 1024 * 1024, &code, &aborted);
	*retry = aborted || rc != CURLE_OK || code != 404;
	u64 td = osGetTime();

	u32 *px = NULL;
	if (rc == CURLE_OK && code == 200) {
		px = decode_png(NULL, 0, COVER_DIR "/download.tmp", pw, ph);
		log_printf("covers: decode %llums\n", osGetTime() - td);
	}
	else if (network_error(rc)) {
		net_failed();
		log_printf("covers: network error %d, retrying in %llus\n", rc, net_backoff / 1000);
	} else {
		log_printf("covers: %s -> %d / HTTP %ld\n", name, rc, code);
		if (!aborted && code != 404)
			net_failed();   // server trouble or a broken picture: not again at once
	}
	if (px) {
		net_backoff = 0;
		__atomic_add_fetch(&downloaded, 1, __ATOMIC_RELAXED);
	} else if (!aborted) __atomic_add_fetch(&failures, 1, __ATOMIC_RELAXED);
	remove(COVER_DIR "/download.tmp");
	return px;
}

// ---------------------------------------------------------------- system logos
// A system's mark for the shelf spines (bb_system_t.logo_url) is not part of
// the app: the worker fetches it once, over the covers' connection and lock,
// into sdmc:/3ds/BreadBox/logos/<id>.png, and it is read from there from then
// on. Until then (and offline) the shelf draws its generic mark.
#define LOGO_DIR PSX3D_DIR "/logos"
#define LOGO_MAX 256   // px, either way
static struct {
	volatile bool wanted, have, tried;   // asked for / on the SD card / given up this session
	bool checked;                        // main thread: looked for the file
	volatile bool decode;                // main thread: wants the saved picture (the worker decodes it)
	u32 *volatile px;
	int w, h;
} logos[BB_MAX_SYSTEMS];

static int logo_index(const bb_system_t *s)
{
	for (unsigned i = 0; i < bb_system_count && i < BB_MAX_SYSTEMS; i++)
		if (bb_systems[i] == s) return (int)i;
	return -1;
}
static void logo_path(const bb_system_t *s, const char *ext, char *out, size_t len)
{
	snprintf(out, len, LOGO_DIR "/%s%s", s->id, ext);
}
const u32 *covers_logo(const bb_system_t *s, int *w, int *h)
{
	int i = s && s->logo_url ? logo_index(s) : -1;
	if (i < 0) return NULL;
	if (!logos[i].px) {
		char path[128];
		logo_path(s, ".png", path, sizeof(path));
		if (!logos[i].checked) {
			struct stat st;
			logos[i].checked = true;
			if (!stat(path, &st)) logos[i].have = true;
		}
		if (logos[i].have && !logos[i].decode) {   // decoded by the worker: a PNG decode is frames of the UI
			logos[i].decode = true;
			LightEvent_Signal(&wake);
		}
	}
	if (logos[i].px) {
		*w = logos[i].w;
		*h = logos[i].h;
		return logos[i].px;
	}
	if (!logos[i].have && !logos[i].wanted) {
		logos[i].wanted = true;
		LightEvent_Signal(&wake);
	}
	return NULL;
}
// Worker thread, nothing else to do: fetch one wanted logo. true if it tried.
static bool logos_work(void)
{
	for (int i = 0; i < BB_MAX_SYSTEMS && i < (int)bb_system_count; i++) {   // a saved logo the shelf wants
		if (!logos[i].decode || logos[i].px || !logos[i].have) continue;
		char path[128];
		int w, h;
		logo_path(bb_systems[i], ".png", path, sizeof(path));
		u32 *px = covers_mark_decode(path, LOGO_MAX, &w, &h);
		if (!px) {   // damaged: forget it, fetched again next session
			remove(path);
			logos[i].have = false;
			logos[i].decode = false;
			return true;
		}
		logos[i].w = w;
		logos[i].h = h;
		__atomic_store_n(&logos[i].px, px, __ATOMIC_RELEASE);
		return true;
	}
	for (int i = 0; i < BB_MAX_SYSTEMS && i < (int)bb_system_count; i++) {
		const bb_system_t *s = bb_systems[i];
		if (!logos[i].wanted || logos[i].have || logos[i].tried || !s->logo_url) continue;
		if (quit || paused || sw_nonet || osGetTime() < net_retry_at) return false;
		char path[128], tmp[128];
		mkdir(LOGO_DIR, 0777);
		logo_path(s, ".png", path, sizeof(path));
		logo_path(s, ".tmp", tmp, sizeof(tmp));
		long code;
		bool aborted;
		LightLock_Lock(&net_lock);
		CURLcode rc = fetch_file(s->logo_url, tmp, 256 * 1024, &code, &aborted);
		if (!aborted && rc != CURLE_OK && network_error(rc)) net_failed();
		LightLock_Unlock(&net_lock);
		bool ok = false;
		if (!aborted && rc == CURLE_OK && code == 200) {
			int w, h;
			u32 *px = covers_mark_decode(tmp, LOGO_MAX, &w, &h);   // only a picture is kept
			if (px) {
				remove(path);
				ok = !rename(tmp, path);
				if (ok) {   // and kept decoded for the shelf
					logos[i].w = w;
					logos[i].h = h;
					__atomic_store_n(&logos[i].px, px, __ATOMIC_RELEASE);
				} else
					free(px);
			}
		}
		remove(tmp);
		log_printf("covers: logo %s -> %s (curl %d, HTTP %ld)\n", s->id, ok ? "saved" : "none", rc, code);
		if (ok) {
			logos[i].have = true;
		} else if (!aborted && !network_error(rc)) {
			logos[i].tried = true;   // the server has no such picture: not again this session
		}
		return true;
	}
	return false;
}

// Preset updates (presetupdate.c) use the same connection, lock and pauses.
static int presets_fetch(const char *url, const char *path, size_t limit, long *http)
{
	bool aborted;
	CURLcode rc = fetch_file(url, path, limit, http, &aborted);
	if (aborted) return PU_FETCH_ABORT;
	if (network_error(rc)) {
		net_failed();
		return PU_FETCH_NET;
	}
	return rc == CURLE_OK ? PU_FETCH_OK : PU_FETCH_FAIL;
}
static bool presets_stop(void) { return quit || paused; }

// ---------------------------------------------------------------- worker

static void state_flush(void)
{
	LightLock_Lock(&state_lock);
	boxart_misses_save(COVER_STATE, BB_VERSION);
	LightLock_Unlock(&state_lock);
}

// A cover that has to come from the network (nothing cached): a finished
// cover, or false. *retry false: the database has no such picture. A picture
// it did not have lately is not asked for again (boxart_miss_*).
static bool fetch_cover(const game_t *g, bool *retry, cover_t *c)
{
	if (quit || sw_nonet)
		return false;
	LightLock_Lock(&state_lock);
	bool missed = boxart_miss_known(g->cover_key, osGetTime());
	LightLock_Unlock(&state_lock);
	if (missed) {
		__atomic_add_fetch(&miss_skips, 1, __ATOMIC_RELAXED);
		*retry = false;
		return false;
	}
	// the picture named like the file, else the disc's serial, else the same title
	char own[BOXART_NAME], serial[10] = "", cand[BOXART_MAX_CANDIDATES][BOXART_NAME], disc[PATH_LEN];
	boxart_sanitize(g->cover_key, own, sizeof(own));
	// a rescan can free g while we read the disc: keep our own copy of its path
	snprintf(disc, sizeof(disc), "%s", g->ndiscs > 0 ? g->discs[0] : "");  
	if (!strchr(g->cover_key, '/') && !boxart_name_known(BOXART_DIR "/names.txt", own) && disc[0])
		boxart_disc_serial(disc, serial);
	int n;
 const char *slash=strchr(g->cover_key,'/');char id[16]={0};
 if(slash && (size_t)(slash-g->cover_key)<sizeof(id))snprintf(id,sizeof(id),"%.*s",(int)(slash-g->cover_key),g->cover_key);
 const bb_system_t *system=id[0]?bb_system_by_id(id):NULL;
 if(system && system->cover_candidates) {
  n=system->cover_candidates(disc,g->group,cand,BOXART_MAX_CANDIDATES);
  for(int i=0;i<n;i++) {char tmp[BOXART_NAME];snprintf(tmp,sizeof(tmp),"%s/%.230s",id,cand[i]);strcpy(cand[i],tmp);}
 } else if(system) {
  n=1;snprintf(cand[0],sizeof(cand[0]),"%s/%.230s",id,slash+1);
 } else
 n = boxart_candidates(BOXART_DIR "/names.txt", BOXART_DIR "/serials.idx", g->cover_key, serial,
	                          cand, BOXART_MAX_CANDIDATES);
	bool ok = false, all_404 = n > 0;
	LightLock_Lock(&net_lock);
	for (int i = 0; i < n && all_404 && !ok; i++) {
		if (quit || paused || osGetTime() < net_retry_at) {
			all_404 = false;
			break;
		}
		int w = 0, h = 0;
		bool r = true;
		u32 *px = download(cand[i], &r, &w, &h);
		ok = px && pack_cover(px, w, h, c);
		free(px);
		if (ok && strcmp(cand[i], own))
			__atomic_add_fetch(&other_names, 1, __ATOMIC_RELAXED);
		if (ok || r)
			all_404 = false;   // got it, or no word from the database: not "it has none"
	}
	LightLock_Unlock(&net_lock);
	if (!ok && all_404) {
		*retry = false;
		LightLock_Lock(&state_lock);
		boxart_miss_add(g->cover_key, osGetTime());
		LightLock_Unlock(&state_lock);
		state_flush();
		log_printf("covers: %s: the database has none (%d names tried), asking again in 14 days\n", g->cover_key, n);
	}
	return ok;
}

// The cover cache holds the picture for this game's file name; a PNG of the
// player's own beats it. Marks the cache with the PNG's size and time, so a
// changed PNG is picked up and a removed one gives the game back to the download.
static bool load_user_cover(const char *key, const char *own, const char *path, cover_t *c, bool *fresh)
{
	struct stat ps, cs;
	long size = 0, mtime = 0;
	bool png = (strchr(key, '/') || own_png_maybe(key)) && !stat(own, &ps) && S_ISREG(ps.st_mode);
	bool marked = boxart_user_get(key, &size, &mtime);
	if (!png) {
		if (marked) {   // the PNG is gone: the cover made from it goes too
			remove(path);
			LightLock_Lock(&state_lock);
			boxart_user_clear(key);
			LightLock_Unlock(&state_lock);
			state_flush();
		}
		return false;
	}
	if (marked && size == (long)ps.st_size && mtime == (long)ps.st_mtime && !stat(path, &cs) && read_cv2(path, c)) {
		__atomic_add_fetch(&cache_hits, 1, __ATOMIC_RELAXED);
		return true;
	}
	int w, h;
	u32 *px = decode_png(NULL, 0, own, &w, &h);
	if (!px) {
		log_printf("covers: %s: own picture cannot be read, using the usual cover\n", own);
		return false;
	}
	bool ok = pack_cover(px, w, h, c);
	free(px);
	if (ok) {
		*fresh = true;
		write_cv2(path, c);
		LightLock_Lock(&state_lock);
		boxart_user_set(key, (long)ps.st_size, (long)ps.st_mtime);
		LightLock_Unlock(&state_lock);
		state_flush();
	}
	return ok;
}

// *fresh: the cover is new (not the cached one): its small copy is made again
static bool load_cover(const game_t *g, bool *retry, bool refresh, cover_t *c, bool *fresh)
{
	char path[PATH_LEN], legacy[PATH_LEN], own[PATH_LEN];
	cache_path(g->cover_key, ".cv2", path, sizeof(path));
	cache_path(g->cover_key, ".png", own, sizeof(own));

 if (strchr(g->cover_key,'/') && g->ndiscs) {
  // Beside the ROM, then its covers/ folder, then the shared covers/gba folder.
  char local[PATH_LEN], stem[256];
  snprintf(local,sizeof(local),"%s",g->discs[0]);
  char *base=strrchr(local,'/'); base=base?base+1:local;
  snprintf(stem,sizeof(stem),"%.255s",base); char *dot=strrchr(stem,'.'); if(dot)*dot=0;
  char *ext=strrchr(base,'.'); if(ext)*ext=0;
  size_t end=strlen(local); snprintf(local+end,sizeof(local)-end,".png");
  struct stat st;
  if (stat(local,&st)) { *base=0; size_t dir=strlen(local); snprintf(local+dir,sizeof(local)-dir,"covers/%s.png",stem); }
  if (!stat(local,&st)) snprintf(own,sizeof(own),"%s",local);
 }
 // 1. the player's own PNG, then our cache (a finished texture)
	if (!refresh && load_user_cover(g->cover_key, own, path, c, fresh))
		return true;
	if (!refresh && read_cv2(path, c)) {
		__atomic_add_fetch(&cache_hits, 1, __ATOMIC_RELAXED);
		return true;
	}
	// 2. the old small cache: show it now, the full size one comes later
	cache_path(g->cover_key, ".bin", legacy, sizeof(legacy));
	if (!refresh && read_legacy(legacy, c)) {
		c->lowres = !sw_nonet;
		__atomic_add_fetch(&cache_hits, 1, __ATOMIC_RELAXED);
		return true;
	}
	// 3. download
	if (!fetch_cover(g, retry, c))
		return false;
	*fresh = true;
	write_cv2(path, c);
	remove(legacy);
	LightLock_Lock(&state_lock);
	boxart_user_clear(g->cover_key);
	LightLock_Unlock(&state_lock);
	state_flush();
	return true;
}

// Replace an old small cover with the full size one. A picture the database
// does not have (404) makes the small one final.
static bool upgrade_cover(const game_t *g, bool *retry, cover_t *c)
{
	char path[PATH_LEN], legacy[PATH_LEN];
	cache_path(g->cover_key, ".cv2", path, sizeof(path));
	cache_path(g->cover_key, ".bin", legacy, sizeof(legacy));
	*retry = true;
	bool ok = fetch_cover(g, retry, c);
	if (!ok && !*retry)
		ok = read_legacy(legacy, c);
	if (ok) {
		write_cv2(path, c);
		remove(legacy);
		__atomic_add_fetch(&upgraded, 1, __ATOMIC_RELAXED);
	}
	return ok;
}


// Nothing to load for the screen: make small copies of cached covers that
// have none yet (nearest to the selection first), and save the small cover
// file a few seconds after the last change. true: did something.
static bool thumbs_idle_work(void)
{
	if (!thumbs_loaded || quit || paused)
		return false;
	int n, game = -1;
	bool save = false;
	covers_lock();
	// Claim before even reading the library count: stop_wait must cover every
	// worker access to the list, including idle thumbnail selection.
	if (paused) {
		LightLock_Unlock(&lock);
		return false;
	}
	n = library_count();
	if (thumb_tried_n != n) {
		free(thumb_tried);
		thumb_tried = calloc(n ? n : 1, 1);
		thumb_tried_n = thumb_tried ? n : 0;
	}
	for (int d = 0; thumb_tried && d < n && game < 0; d++)
		for (int side = 0; side < 2 && game < 0; side++) {
			int i = (focus_game < 0 ? 0 : focus_game) + (side ? -d : d);
			if (i < 0 || i >= n || thumb_tried[i])
				continue;
			thumb_tried[i] = 1;
			const game_t *g = library_get(i);
			if (g && thumb_find(name_hash(g->cover_key)) < 0)
				game = i;
		}
	if (game < 0 && thumbs_dirty && osGetTime() - thumbs_dirty_at > 3000)
		save = true;
	if (game >= 0 || save)
		__atomic_add_fetch(&busy, 1, __ATOMIC_SEQ_CST);
	LightLock_Unlock(&lock);
	if (save)
		thumbs_save();
	else if (game >= 0) {
		cover_record scratch;
		const game_t *g = cover_game(game, &scratch);
		char path[PATH_LEN];
		cover_t c = { 0 };
		if (g) {
			cache_path(g->cover_key, ".cv2", path, sizeof(path));
			bool ok = read_cv2(path, &c);
			if (!ok) {
				cache_path(g->cover_key, ".bin", path, sizeof(path));
				ok = read_legacy(path, &c);
			}
			if (ok) {
				thumb_make(g->cover_key, &c, false);
				__atomic_add_fetch(&thumb_filled, 1, __ATOMIC_RELAXED);
			}
			free(c.data);
		}
		// leave the UI core some room on Old 3DS (the worker shares core 0 there)
		svcSleepThread(g_new3ds ? 1000000LL : 8000000LL);
	}
	if (game >= 0 || save)
		__atomic_sub_fetch(&busy, 1, __ATOMIC_SEQ_CST);
	return game >= 0 || save;
}

static void worker_main(void *arg)
{
	(void)arg;
	while (!quit) {
		// timed: a one-shot signal can be lost when two arrive together
		// (LightEvent_Signal does nothing on an already-signalled event),
		// so never rely on one to see quit
		LightEvent_WaitTimeout(&wake, 100000000LL);
		for (;;) {
			while (paused && !quit)
				svcSleepThread(100000000LL);
			if (quit)
				break;
			// the small covers first: they show while the rest loads
			if ((!thumbs_loaded || !own_pngs_ok) && !quit) {
				covers_lock();
				bool mine = (!thumbs_loaded || !own_pngs_ok) && !thumbs_loading && !paused;
				if (mine) {
					thumbs_loading = true;
					__atomic_add_fetch(&busy, 1, __ATOMIC_SEQ_CST);
				}
				LightLock_Unlock(&lock);
				if (mine) {
					if (!own_pngs_ok) own_pngs_scan();
					if (!thumbs_loaded) {HT("thumbs enter");thumbs_load();HT("thumbs leave");}
					thumbs_loading = false;
					__atomic_sub_fetch(&busy, 1, __ATOMIC_SEQ_CST);
					continue;
				}
			}
			int pick = -1, game = -1, best = 1 << 30;
			bool refresh = false, upgrade = false;
			covers_lock();
			for (int i = 0; i < SLOTS; i++) {
				if (slots[i].state != 1 || osGetTime() < slots[i].retry_at)
					continue;
				// not asked for in the last few frames: scrolled away
				if (slots[i].last_used + 40 < use_clock) {
					slots[i].state = 0;
					slots[i].game = -1;
					continue;
				}
				int d = (int)slots[i].rank;   // the UI asks for the selection first, then outward
				if (d < best) {
					best = d;
					pick = i;
					game = slots[i].game;
				}
			}
			// Nothing new to load: swap a shown old small cover for the full size one.
			if (pick < 0 && !quit && !paused && !sw_nonet) {
				best = 1 << 30;
				for (int i = 0; i < SLOTS; i++) {
					if (slots[i].state != 3 || !slots[i].lowres || slots[i].upgrading ||
					    osGetTime() < slots[i].retry_at || slots[i].last_used + 40 < use_clock)
						continue;
					if ((int)slots[i].rank < best) {
						best = slots[i].rank;
						pick = i;
						game = slots[i].game;
						upgrade = true;
					}
				}
			}
			if (pick >= 0 && !quit && !paused) {
				__atomic_add_fetch(&busy, 1, __ATOMIC_SEQ_CST);
				refresh = slots[pick].refresh;
				if (upgrade)
					slots[pick].upgrading = true;   // stays shown meanwhile
				else
					slots[pick].state = 5;   // ours: the other worker skips it
			} else pick = -1;
			// Nothing to fetch for the library: the daily preset check.
			bool presets = pick < 0 && !quit && !paused && !presets_claimed && !sw_nonet && presets_update_due();
			if (presets) {
				presets_claimed = true;
				__atomic_add_fetch(&busy, 1, __ATOMIC_SEQ_CST);
			}
			LightLock_Unlock(&lock);
			if (presets) {
				LightLock_Lock(&net_lock);
				if (!quit && !paused && osGetTime() >= net_retry_at)
					presets_update_work(presets_fetch, presets_stop);
				LightLock_Unlock(&net_lock);
				covers_lock();
				presets_claimed = false;
				LightLock_Unlock(&lock);
				__atomic_sub_fetch(&busy, 1, __ATOMIC_SEQ_CST);
			}
			if (pick < 0) {
				if (logos_work())
					continue;
				if (thumbs_idle_work())
					continue;
				break;
			}
			cover_record scratch;
		const game_t *g = cover_game(game, &scratch);
			u64 t0 = osGetTime();
			HT("covers job enter");
			log_printf("covers: start #%d\n", game);
			bool retry = true;
			cover_t c = { 0 };
			bool fresh = false;
			bool ok = g && (upgrade ? upgrade_cover(g, &retry, &c) : load_cover(g, &retry, refresh, &c, &fresh));
			if (ok && thumbs_loaded)
				thumb_make(g->cover_key, &c, fresh || upgrade);
			log_printf("covers: #%d %s -> %s%s in %llums\n", game, g ? g->cover_key : "?",
				ok ? "ok" : "none", ok && c.lowres ? " (small)" : "", osGetTime() - t0);
			covers_lock();
			if (slots[pick].game == game && (upgrade ? slots[pick].upgrading : slots[pick].state == 5)) {
				if (ok) {
					free(slots[pick].pixels);
					slots[pick].pixels = c.data;
					slots[pick].w = c.w;
					slots[pick].h = c.h;
					slots[pick].lowres = c.lowres;
					slots[pick].refresh = false;
					c.data = NULL;
				}
				// interrupted by HOME/sleep: try again later, not "no cover";
				// without a network, not before the backoff is over
				slots[pick].retry_at = paused ? 0 : (net_retry_at > osGetTime() + 15000 ? net_retry_at : osGetTime() + 15000);
				if (upgrade) {
					slots[pick].upgrading = false;
					if (ok)
						slots[pick].state = 2;   // the shown picture is replaced
					else if (!retry)
						slots[pick].lowres = false;
				} else {
					slots[pick].state = ok ? 2 : (retry ? 1 : 4);
				}
			}
			free(c.data);
			LightLock_Unlock(&lock);
			HT("covers job leave");
			__atomic_sub_fetch(&busy, 1, __ATOMIC_SEQ_CST);
		}
	}
}

// ---------------------------------------------------------------- API

void covers_init(void)
{
	mkdir(COVER_DIR, 0777);
 mkdir(COVER_DIR "/gba",0777);
	paused = true; // library entry explicitly resumes after SOC/control init
	for (int i = 0; i < SLOTS; i++)
		slots[i].game = -1;
	for (int i = 0; i < TSLOTS; i++)
		tslots[i].game = -1;
	LightLock_Init(&lock);
	LightLock_Init(&state_lock);
	boxart_misses_load(COVER_STATE, BB_VERSION);
	LightEvent_Init(&wake, RESET_ONESHOT);   // each Signal wakes one worker
	// the network: 3dslink logging may already have started it
	// (3dslink logging may already have started the network)
	if (!dev_remote && !sw_nonet && !soc_buf && (soc_buf = memalign(0x1000, 0x100000)))
		soc_started = R_SUCCEEDED(socInit(soc_buf, 0x100000));
	curl_global_init(CURL_GLOBAL_DEFAULT);
	LightLock_Init(&net_lock);
	initialized = true;
	easy = curl_easy_init();
	if ((share = curl_share_init())) {
		curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
		curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
	}
	aptHook(&apt_cookie, on_apt, NULL);
	// Publish immutable conversion tables before either worker can use them.
	covers_gamma_init();
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	// The New 3DS's spare core 2 when we have it: on core 0 under the UI,
	// TLS got so little CPU that covers came in at 20KB/s. (The old HOME
	// freeze blamed on core 2 was the blocking socket calls, see net_lock.)
	// On core 0 the UI may consume the full refresh interval. A lower
	// priority worker never runs then (3DS does not age thread priorities).
	// Equal priority gives TLS/decode bounded time slices on Old 3DS.
	// (The Old 3DS has no core 2: straight to core 0 there, which is where the
	// failed core 2 call used to end up; Azahar's Old mode stops at that call.)
	for (int i = 0; i < WORKERS; i++) {
		int core = 2;
		workers[i] = g_new3ds ? threadCreate(worker_main, NULL, 64 * 1024, prio + 4, core, false) : NULL;
		if (!workers[i])
			workers[i] = threadCreate(worker_main, NULL, 64 * 1024, prio, core = 0, false);
		log_printf("covers: worker %d on core %d\n", i, workers[i] ? core : -1);
	}

}

/* After successful self-install, workers are already paused and drained.
 * Close sockets without state_flush(), texture work, or any asset/font reads.
 * The OS releases cached textures when this process exits, but not a paused
 * worker: it still wakes every 100 ms, and after main returns its stack (on the
 * heap) is gone. End them, bounded. false: one still runs, and everything it can
 * reach (curl, the sockets, the pixels) is left as it is for main's exit_terminal. */
bool covers_network_exit(u64 timeout)
{
 video_pre_draw=NULL;
 aptUnhook(&apt_cookie);
 quit=true;
 bool stopped=true;
 for(int i=0;i<WORKERS;i++){
  if(!workers[i])continue;
  LightEvent_Signal(&wake);
  if(threadJoin(workers[i],timeout/WORKERS)!=0){stopped=false;continue;}   /* a timeout is not R_FAILED */
  threadFree(workers[i]);workers[i]=NULL;
 }
 if(!stopped)return false;
 net_close();
 if(easy)curl_easy_cleanup(easy);easy=NULL;
 if(share)curl_share_cleanup(share);share=NULL;
 curl_global_cleanup();
 if(soc_started){socExit();soc_started=false;}
 return true;
}

bool covers_exit(void)
{
	video_pre_draw = NULL;
	aptUnhook(&apt_cookie);
	quit = true;
	bool stopped = true;
	for (int i = 0; i < WORKERS; i++) {
		if (!workers[i])
			continue;
		// workers notice quit within 100ms; give up after 3s rather than
		// hang the exit
		bool done = false;
		for (int t = 0; t < 30 && !done; t++) {
			LightEvent_Signal(&wake);
			done = threadJoin(workers[i], 100000000LL) == 0;   // a timeout is not R_FAILED
		}
		log_printf("covers: worker %d %s\n", i, done ? "stopped" : "did not stop");
		if (done) {
			threadFree(workers[i]);
			workers[i] = NULL;
		}
		stopped = stopped && done;
	}
	// One still runs: nothing it can reach (pixels, thumbnails, logos, curl, the
	// sockets) is freed or closed; main's exit_terminal ends it with the app.
	if (!stopped)
		return false;
	for (int i = 0; i < SLOTS; i++) {
		free(slots[i].pixels);
		if (slots[i].tex_ok)
			C3D_TexDelete(&slots[i].tex);
	}
	for (int i = 0; i < TSLOTS; i++)
		if (tslots[i].tex_ok)
			C3D_TexDelete(&tslots[i].tex);
	for (int i = 0; i < BB_MAX_SYSTEMS; i++) {
		free(logos[i].px);
		memset(&logos[i], 0, sizeof(logos[i]));
	}
	thumbs_free();
	free(own_pngs);
	own_pngs = NULL;
	free(thumb_tried);
	thumb_tried = NULL;
	net_close();
	state_flush();
	boxart_misses_free();
	if (easy)
		curl_easy_cleanup(easy);
	if (share)
		curl_share_cleanup(share);
	easy = NULL;
	share = NULL;
	curl_global_cleanup();
	initialized = false;
	// close the network ourselves: leaving sockets for the system to tear
	// down at exit crashed the 3DS's process manager
	if (soc_started) {
		socExit();
		soc_started = false;
	}
	return true;
}

// The small copy of a game's cover, while drawing (caller holds lock). Its
// subtexture has the full cover's size, so it shows exactly where and as large
// as the full one will. Textures are reused least recently drawn first; one
// drawn earlier in this frame is never rewritten (the frame began after the
// GPU finished the previous ones, see video_present_screens).
static C3D_Tex *thumb_get(int game, Tex3DS_SubTexture *sub)
{
	if (!thumbs_loaded || !nthumbs || covers_slow)
		return NULL;
	tslot_t *t = NULL, *lru = NULL;
	for (int i = 0; i < TSLOTS; i++) {
		if (tslots[i].game == game) {
			t = &tslots[i];
			break;
		}
		if (tslots[i].used != pump_frame && (!lru || tslots[i].used < lru->used))
			lru = &tslots[i];
	}
	if (!t) {
		cover_record scratch;
		const game_t *g = cover_game(game, &scratch);
		int k = g && lru && thumb_uploads_left > 0 ? thumb_find(name_hash(g->cover_key)) : -1;
		if (k < 0)
			return NULL;
		t = lru;
		if (!t->tex_ok) {
			t->tex_ok = C3D_TexInit(&t->tex, COVER_THUMB, COVER_THUMB, GPU_RGB565);
			if (!t->tex_ok)
				return NULL;
			C3D_TexSetFilter(&t->tex, GPU_LINEAR, GPU_LINEAR);
			C3D_TexSetWrap(&t->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		}
		const thumb_t *src = thumbs[k];
		memcpy(t->tex.data, src->px, COVER_THUMB_BYTES);
		GSPGPU_FlushDataCache(t->tex.data, COVER_THUMB_BYTES);
		t->game = game;
		t->w = src->w;
		t->h = src->h;
		t->tw = (src->w + 3) / 4;
		t->th = (src->h + 3) / 4;
		thumb_uploads_left--;
	}
	t->used = pump_frame;
	float u = (float)t->tw / COVER_THUMB, v = 1.0f - (float)t->th / COVER_THUMB;
	*sub = (Tex3DS_SubTexture){ t->w, t->h, 0.0f, 1.0f, u, v };
	return &t->tex;
}

// The small copy only (spine artwork): never queues the full-size cover.
C3D_Tex *covers_small(int game, Tex3DS_SubTexture *sub)
{
	if (sw_nocovers || library_changes)
		return NULL;
	covers_lock();
	C3D_Tex *tex = thumb_get(game, sub);
	LightLock_Unlock(&lock);
	return tex;
}

// Texture for game i if it's ready (or its small copy while drawing);
// otherwise queue it and return NULL.
C3D_Tex *covers_get(int game, Tex3DS_SubTexture *sub)
{
	if (sw_nocovers || library_changes)
		return NULL;
	slot_t *s = NULL;
	use_clock++;
	covers_lock();
	for (int i = 0; i < SLOTS; i++)
		if (slots[i].game == game) {
			s = &slots[i];
			break;
		}
	if (!s) {
		// reuse the least recently used slot (a queued one too: the worker
		// skips slots whose game changed under it)
		for (int i = 0; i < SLOTS; i++)
			if (!s || slots[i].last_used < s->last_used)
				s = &slots[i];
		if (s) {
			free(s->pixels);
			s->pixels = NULL;
			s->game = game;
			s->state = 1;
			s->retry_at = 0;
			s->refresh = false;
			s->shown = false;   // the texture still holds the previous game's cover
			s->lowres = false;
			s->upgrading = false;
			LightEvent_Signal(&wake);
		}
	}
	if (s) {
		// keep the best rank it got this frame (drawing asks again, later)
		u32 r = use_clock - frame_base;
		if (s->last_used < frame_base || r < s->rank)
			s->rank = r;
		s->last_used = use_clock;
	}
	bool shown = s && s->shown && s->tex_ok;
	if (shown && sub) {
		// the picture is the top left w x h of the texture; top = 1 is its first row
		float u = (float)s->w / COVER_TEX, v = 1.0f - (float)s->h / COVER_TEX;
		*sub = (Tex3DS_SubTexture){ s->w, s->h, 0.0f, 1.0f, u, v };
	}
	C3D_Tex *tex = shown ? &s->tex : sub ? thumb_get(game, sub) : NULL;
	LightLock_Unlock(&lock);
	return tex;
}

// true: game's full-size cover is on its texture, or none is coming (no cover); false while it
// loads (the shelf waits a moment for it before a box is taken out: never the small one in a hand)
bool covers_full(int game)
{
	if (sw_nocovers) return true;
	bool done = false;
	covers_lock();
	for (int i = 0; i < SLOTS; i++)
		if (slots[i].game == game) { done = (slots[i].shown && slots[i].tex_ok && !slots[i].lowres) || slots[i].state == 4; break; }
	LightLock_Unlock(&lock);
	return done;
}

// Upload decoded covers to textures. Called inside the frame (from
// video_present, after C3D_FrameBegin). The cache holds the texture exactly
// as the GPU wants it, so this is one copy and a cache flush: no GX transfer
// (an old blocking one raced citro3d's own GPU queue and could hang the app)
// and no staging buffer. A cover that replaces a shown one (small -> full
// size) is the same picture, so a frame still in flight showing a mix is
// invisible.
static void covers_pump_in_frame(void)
{
	u64 now = svcGetSystemTick();
	if (frame_last) {
		u32 us = (u32)((now - frame_last) * 1000000 / SYSCLOCK_ARM11);
		if (us < 1000000) {   // longer gaps are screen changes, not frames
			frames++;
			frame_total_us += us;
			frames_over_25ms += us > 25000;
			frames_over_40ms += us > 40000;
			if (us > frame_max_us) frame_max_us = us;
		}
	}
	frame_last = now;
	pump_frame++;
	thumb_uploads_left = 12;
	int done = 0;
	for (int i = 0; i < SLOTS && done < 2; i++) {
		slot_t *s = &slots[i];
		if (s->state != 2)
			continue;
		if (!s->tex_ok) {
			s->tex_ok = C3D_TexInit(&s->tex, COVER_TEX, COVER_TEX, GPU_RGB565);
			if (s->tex_ok) {
				C3D_TexSetFilter(&s->tex, GPU_LINEAR, GPU_LINEAR);
				C3D_TexSetWrap(&s->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
			}
		}
		if (!s->tex_ok)
			return;
		u64 t0 = svcGetSystemTick();
		covers_lock();
		bool still = s->state == 2 && s->pixels;
		if (still) {
			memcpy(s->tex.data, s->pixels, COVER_PACK_BYTES);
			free(s->pixels);
			s->pixels = NULL;
			s->shown = true;
			s->state = 3;
		}
		LightLock_Unlock(&lock);
		if (!still)
			continue;
		GSPGPU_FlushDataCache(s->tex.data, COVER_PACK_BYTES);
		u32 us = (u32)((svcGetSystemTick() - t0) * 1000000 / SYSCLOCK_ARM11);
		uploads++;
		upload_total_us += us;
		if (us > upload_max_us) upload_max_us = us;
		done++;
	}
}

void covers_pump(void)
{
	if (sw_nocovers)
		return;
	video_pre_draw = covers_pump_in_frame;   // runs at the next frame start
}

// Developer diagnostics: start the frame and upload counters over.
void covers_stats_reset(void)
{
	frame_last = 0;
	frames = frames_over_25ms = frames_over_40ms = frame_max_us = 0;
	frame_total_us = 0;
	uploads = upload_max_us = upload_total_us = 0;
}





// Developer-only refresh of a library cover, without deleting any SD files.
bool covers_refresh(int game)
{
	if (sw_nocovers || sw_nonet || game < 0 || game >= library_count()) return false;
	covers_get(game, NULL);
	bool ok = false;
	covers_lock();
	for (int i = 0; i < SLOTS; i++) if (slots[i].game == game && slots[i].state != 5 && !slots[i].upgrading) {
		free(slots[i].pixels);
		slots[i].pixels = NULL;
		slots[i].lowres = false;
		slots[i].refresh = true;
		slots[i].retry_at = 0;
		slots[i].state = 1;
		ok = true;
		break;
	}
	LightLock_Unlock(&lock);
	LightEvent_Signal(&wake);
	return ok;
}

// true while a cover is loading or a connection is open: HOME must wait
bool covers_busy(void)
{
 if(bb_update_net_busy())return true;
	if (sw_nocovers)
		return false;
	return busy > 0 || net_open;
}

// Pause the workers and close the network. Doesn't block: call it each
// frame until it returns true (the network loop checks pause every 10ms).
bool covers_stop(void)
{
 if(!breadbox_import_pause())return false;
 if(!bb_update_net_stop())return false;
	if (sw_nocovers)
		return true;
	// Serialize pause with worker claims: no worker may start between the
	// busy check and returning success to a game load or APT.
	covers_lock();
	paused = true;
	bool active = busy > 0;
	LightLock_Unlock(&lock);
	if (active || LightLock_TryLock(&net_lock) != 0)
		return false;
	net_close();
	LightLock_Unlock(&net_lock);
	return true;
}

// Wait for actual quiescence. Never enter gameplay/update with a worker alive.
void covers_stop_wait(void)
{
	u64 start = osGetTime();
	bool warned = false;
	while (!covers_stop()) {
		if (!warned && osGetTime() - start >= 5000) {
			log_printf("covers: quiescence waiting over 5s; importer/worker still active\n");
			warned = true;
		}
		svcSleepThread(10000000LL);
	}
	stop_ms = osGetTime() - start;
	if (stop_ms > max_stop_ms) max_stop_ms = stop_ms;
}

void covers_resume(void)
{
 breadbox_import_resume();
	if (sw_nocovers || apt_suspended || dev_covers_blocked())
		return;
	// A nonblocking drain may have started before the menu returned. Remember
	// its resume request just as we do for an already-open publication below;
	// otherwise try_begin's old pause snapshot stops the workers at its end.
	if (try_open) try_was_paused = false;
	if (library_changes) {library_was_paused = false;return;}
	paused = false;
	LightEvent_Signal(&wake);
	LightEvent_Signal(&wake);
}

// HOME during a cover download froze the app (11ec33b8). While the workers use the network HOME is held back; a
// press is caught, the downloads stop (polled, never waited for here), then the app goes to the HOME Menu itself.
// Every menu loop that runs the covers (Home, the Library) calls this once a frame; true when it went HOME.
bool covers_home_frame(void)
{
	static bool pending;
	aptSetHomeAllowed(!pending && !covers_busy());
	if (aptCheckHomePressRejected()) pending = true;
	if (!pending || !covers_stop()) return false;
	pending = false;
	aptSetHomeAllowed(true);
	aptJumpToHomeMenu();
	covers_resume();
	return true;
}

// a new UI frame: covers_get calls from now on rank from the selection out
void covers_focus(int game)
{
	if (sw_nocovers)
		return;
	focus_game = game;
	frame_base = use_clock + 1;
	LightEvent_Signal(&wake);
	LightEvent_Signal(&wake);
}

// A game needs the GPU memory: drop the cover textures (about 4 MB). Covers
// come back from their cache files on return to the library. The New 3DS
// keeps pixels that were decoded but not uploaded yet; the Old 3DS needs
// that heap for emulation too.
// Call with no GPU frame using them in flight.
void covers_release_gpu(void)
{
	if (sw_nocovers)
		return;
	covers_lock();
	gpu_generation++;
	for (int i = 0; i < SLOTS; i++) {
		slot_t *s = &slots[i];
		if (s->tex_ok)
			C3D_TexDelete(&s->tex);
		s->tex_ok = false;
		s->shown = false;
		s->upgrading = false;
		s->lowres = false;
		if (!g_new3ds) {
			free(s->pixels);
			s->pixels = NULL;
			s->retry_at = 0;
			s->state = s->game >= 0 ? 1 : 0;
		}
		if (s->state == 3)
			s->state = 1; // the reload finds that out again
	}
	for (int i = 0; i < TSLOTS; i++) {
		if (tslots[i].tex_ok)
			C3D_TexDelete(&tslots[i].tex);
		tslots[i].tex_ok = false;
		tslots[i].game = -1;
	}
	own_pngs_ok = false;  // and the player's PNGs looked for again
	LightLock_Unlock(&lock);
	// The small covers' memory (8 KiB a game, 1.6 MB for 200) goes to the game
	// too; they are read again in the library (one file). Unsaved new ones
	// are made again from the cache by the background fill.
	thumbs_free();
	covers_lock();
	free(thumb_tried);
	thumb_tried = NULL;
	thumb_tried_n = 0;
	LightLock_Unlock(&lock);
}

// Called only after the workers have drained. Numeric indexes belong to
// one published library; none may survive its replacement, even at equal size.
static void invalidate_library(void)
{
	covers_lock();
	for (int i = 0; i < SLOTS; i++) {
		slots[i].game = -1;
		slots[i].state = 0;
		slots[i].shown = false;
		slots[i].lowres = false;
		slots[i].upgrading = false;
		free(slots[i].pixels);
		slots[i].pixels = NULL;
	}
	for (int i = 0; i < TSLOTS; i++)
		tslots[i].game = -1;
	free(thumb_tried);
	thumb_tried = NULL;
	thumb_tried_n = 0;
	own_pngs_ok = false;   // the worker looks at the folder again
	LightLock_Unlock(&lock);
}

// A library mutation is a transaction on the UI thread. Workers may borrow
// game_t only while busy; drain them BEFORE freeing or sorting any entries.
// BreadBox keeps this transaction open across incremental import frames.
void covers_library_begin(void)
{
	if (sw_nocovers || !initialized) return;
	if (library_changes++) return;
	bool was_paused = paused;
	covers_stop_wait();
	library_was_paused = was_paused;
	invalidate_library();
}

// covers_library_begin without waiting: false while the worker is still in a job (a download or a decode:
// seconds on a cold card, the 2621 room froze 5.2 s on its first start). It is asked to stop and the caller
// asks again next frame; true is the same state covers_library_begin leaves (end it with covers_library_end).
bool covers_library_try_begin(void)
{
	if (sw_nocovers || !initialized) return true;
	if (library_changes) { library_changes++; return true; }
	if (!try_open) { try_open = true; try_was_paused = paused; }
	if (!covers_stop()) return false;
	try_open = false;
	library_changes++;
	library_was_paused = try_was_paused;
	invalidate_library();
	return true;
}

void covers_library_end(void)
{
	if (sw_nocovers || !initialized || !library_changes) return;
	if (--library_changes) return;
	if (!library_was_paused) covers_resume();
}

// A user rescan also forgets remembered database misses. It can be requested
// while covers are active, so use the same drain boundary as list replacement.
void covers_reset(void)
{
	if (sw_nocovers || !initialized) return;
	covers_library_begin();
	LightLock_Lock(&state_lock);
	boxart_misses_clear();
	LightLock_Unlock(&state_lock);
	state_flush();
	covers_library_end();
}

// Token-gated developer diagnostics. No arbitrary SD reads; report only this
// subsystem's cache files and bounded slot/counter snapshots.
void covers_debug(char *out, size_t len)
{
 room_pace_diagnostic();
	unsigned heap_used = 0, heap_arena = 0, linear_free = 0;
#ifdef __3DS__
	struct mallinfo mi = mallinfo();
	heap_used = mi.uordblks;
	heap_arena = mi.arena;
	linear_free = linearSpaceFree();
#endif
	int ready = 0, wanted = 0, missing = 0, cached = 0, invalid = 0, legacy = 0, textures = 0, pixels = 0;
	for (int i = 0; i < library_count(); i++) {
		char path[PATH_LEN];
		struct stat st;
		cache_path(library_get(i)->cover_key, ".cv2", path, sizeof(path));
		if (!stat(path, &st)) {
			if (st.st_size == CV2_BYTES) cached++;
			else invalid++;
			continue;
		}
		cache_path(library_get(i)->cover_key, ".bin", path, sizeof(path));
		if (stat(path, &st)) missing++;
		else if (st.st_size == COVER_LEGACY_SIZE * COVER_LEGACY_SIZE * 4) legacy++;
		else invalid++;
	}
	if (!sw_nocovers) {
		covers_lock();
		for (int i = 0; i < SLOTS; i++) {
			ready += slots[i].state == 3;
			wanted += slots[i].state == 1;
			textures += slots[i].tex_ok && slots[i].shown;
			pixels += slots[i].pixels != NULL;
		}
		LightLock_Unlock(&lock);
	}
	snprintf(out, len, "{\"ok\":true,\"gpu_generation\":%u,\"library_depth\":%u,\"textures\":%d,\"pixels\":%d,\"paused\":%s,\"busy\":%d,\"net_open\":%s,"
	         "\"attempts\":%u,\"downloaded\":%u,\"cache_hits\":%u,\"aborted\":%u,"
	         "\"failures\":%u,\"curl\":%d,\"http\":%ld,\"stop_ms\":%u,\"max_stop_ms\":%u,"
	         "\"ready\":%d,\"wanted\":%d,\"cached\":%d,\"missing\":%d,\"invalid_cache\":%d,"
	         "\"legacy\":%d,\"upgraded\":%u,\"miss_skips\":%u,\"other_names\":%u,\"uploads\":%u,\"upload_avg_us\":%u,\"upload_max_us\":%u,"
	         "\"frames\":%u,\"frame_avg_us\":%u,\"frame_max_us\":%u,\"frames_over_25ms\":%u,\"frames_over_40ms\":%u,"
	         "\"thumbs\":%d,\"thumbs_loaded\":%s,\"thumb_made\":%u,\"thumb_filled\":%u,\"thumb_load_ms\":%u,\"thumb_save_ms\":%u,\"own_pngs\":%d,\"fast\":%s,"
	         "\"heap_used\":%u,\"heap_arena\":%u,\"linear_free\":%u}",
	         gpu_generation, library_changes, textures, pixels, paused ? "true" : "false", busy, net_open ? "true" : "false", attempts,
	         downloaded, cache_hits, aborted_count, failures, last_curl, last_http,
	         stop_ms, max_stop_ms, ready, wanted, cached, missing, invalid,
	         legacy, upgraded, miss_skips, other_names, uploads, uploads ? upload_total_us / uploads : 0, upload_max_us,
	         frames, frames ? (unsigned)(frame_total_us / frames) : 0, frame_max_us, frames_over_25ms, frames_over_40ms,
	         nthumbs, thumbs_loaded ? "true" : "false", thumb_made, thumb_filled,
	         thumb_load_ms, thumb_save_ms, nown_pngs, covers_slow ? "false" : "true",
	         heap_used, heap_arena, linear_free);
}
