#include "covers_internal.h"

static void sd_model(size_t bytes)
{
	if (covers_sd_ms)
		svcSleepThread((long long)covers_sd_ms * 1000000LL * bytes / COVER_PACK_BYTES);
}

// A cover's small copy, made from the finished cover. replace: the cover
// changed (a download, the player's PNG); else only if there is none yet.
static void thumb_make(const char *key, const cover_t *c, bool replace)
{
	u64 k = covers_name_hash(key);
	covers_lock();
	bool have = covers_thumb_find(k) >= 0;
	LightLock_Unlock(&covers_list_lock);
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
	if (covers_thumb_insert(t)) {
		if (!covers_thumbs_dirty) covers_thumbs_dirty_at = osGetTime();
		covers_thumbs_dirty = true;
		for (int i = 0; i < TSLOTS; i++) {
   cover_record scratch; const game_t *g=cover_game(covers_tslots[i].game,&scratch);
   if (g && covers_name_hash(g->cover_key)==k) covers_tslots[i].game=-1;
  }
	} else free(t);
	LightLock_Unlock(&covers_list_lock);
	__atomic_add_fetch(&covers_thumb_made, 1, __ATOMIC_RELAXED);
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
		if (covers_paused || covers_quit) {
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
		list[n++] = covers_name_hash(name);
	}
	if (d) closedir(d);
	if (n)
		qsort(list, n, sizeof(*list), cmp_u64);
	covers_lock();
	free(covers_own_pngs);
	covers_own_pngs = list;
	covers_nown_pngs = n;
	covers_own_pngs_ok = d != NULL;
	LightLock_Unlock(&covers_list_lock);
}

// false: the player has no PNG for this game (true: there may be one)
static bool own_png_maybe(const char *key)
{
	char name[256];
	covers_thumb_name(key, name, sizeof(name));
	u64 h = covers_name_hash(name);
	covers_lock();
	bool maybe = !covers_own_pngs_ok || covers_slow || (covers_own_pngs && bsearch(&h, covers_own_pngs, covers_nown_pngs, sizeof(*covers_own_pngs), cmp_u64));
	LightLock_Unlock(&covers_list_lock);
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
		if (covers_quit || covers_paused) {
			fclose(f);
			free(vbuf);
			covers_thumbs_free();
			return false;
		}
		thumb_t *t = malloc(sizeof(*t));
		if (!t || fread(t, sizeof(*t), 1, f) != 1 || !t->w || t->w > COVER_TEX || !t->h || t->h > COVER_TEX) {
			free(t);
			log_printf("covers: %s damaged at %d of %d, made again\n", THUMB_FILE, i, count);
			break;
		}
		covers_lock();
		if (!covers_thumb_insert(t)) free(t);
		LightLock_Unlock(&covers_list_lock);
	}
	if (f) fclose(f);
	free(vbuf);
	sd_model((size_t)covers_nthumbs * sizeof(thumb_t));
	covers_lock();
	covers_thumbs_loaded = true;
	covers_thumbs_dirty = covers_nthumbs != count;   // damaged or missing: write it again
	covers_thumbs_dirty_at = 0;
	LightLock_Unlock(&covers_list_lock);
	covers_thumb_load_ms = (unsigned)(osGetTime() - t0);
	log_printf("covers: %d small covers in %ums\n", covers_nthumbs, covers_thumb_load_ms);
	return true;
}

// Write covers/thumbs.pk next to the old one, then put it in place (worker).
// A pause throws the new file away; it is written again later.
static void thumbs_save(void)
{
	u64 t0 = osGetTime();
	covers_lock();
	int count = covers_nthumbs;
	covers_thumbs_dirty = false;
	LightLock_Unlock(&covers_list_lock);
	FILE *f = safe_begin(THUMB_FILE);
	thumb_head_t head = { THUMB_MAGIC, count, COVER_THUMB_BYTES, 0 };
	bool ok = f && fwrite(&head, sizeof(head), 1, f) == 1;
	for (int i = 0; ok && i < count; i++) {
		if (covers_quit || covers_paused) {
			ok = false;
			break;
		}
		// records only ever get added or replaced, never removed, while the
		// library is open: index i is still a whole record
		covers_lock();
		thumb_t rec = *covers_thumbs[i < covers_nthumbs ? i : covers_nthumbs - 1];
		LightLock_Unlock(&covers_list_lock);
		ok = fwrite(&rec, sizeof(rec), 1, f) == 1;
	}
	if (f) safe_finish(f, THUMB_FILE, ok);
	if (!ok) {
		covers_lock();
		covers_thumbs_dirty = true;
		LightLock_Unlock(&covers_list_lock);
	}
	covers_thumb_save_ms = (unsigned)(osGetTime() - t0);
	log_printf("covers: %d small covers %s in %ums\n", count, ok ? "saved" : "not saved", covers_thumb_save_ms);
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
void covers_net_close(void)
{
	if (covers_curl_multi)
		curl_multi_cleanup(covers_curl_multi);
	covers_curl_multi = NULL;
	covers_net_open = false;
}

// caller holds net_lock
static int transfer_progress(void *unused, curl_off_t total, curl_off_t now,
                             curl_off_t up_total, curl_off_t up_now)
{
	(void)unused; (void)total; (void)now; (void)up_total; (void)up_now;
	return covers_quit || covers_paused;
}

// One transfer into a file on the SD card, at most limit bytes (caller holds
// net_lock). *aborted: HOME, sleep, exit or a game start stopped it.
static CURLcode fetch_file(const char *url, const char *path, size_t limit, long *code, bool *aborted)
{
	CURL *c = covers_curl_easy;
	*code = 0;
	*aborted = false;
	if (!c || (!covers_curl_multi && !(covers_curl_multi = curl_multi_init())))
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
	curl_easy_setopt(c, CURLOPT_SHARE, covers_curl_share);
	curl_easy_setopt(c, CURLOPT_DNS_CACHE_TIMEOUT, -1L);   // lookups can't be aborted: do them once

	CURLcode rc = CURLE_ABORTED_BY_CALLBACK;
	int running = 1;
	curl_multi_add_handle(covers_curl_multi, c);
	covers_net_open = true;
	__atomic_add_fetch(&covers_attempts, 1, __ATOMIC_RELAXED);
	for (;;) {
		if (covers_quit || covers_paused) {   // HOME, sleep or exit: retried later
			*aborted = true;
			break;
		}
		if (curl_multi_perform(covers_curl_multi, &running) != CURLM_OK || !running)
			break;
		// Never wait inside SOC: a blocking poll monopolizes its shared IPC
		// session, including the developer server and HOME socket calls.
		curl_multi_poll(covers_curl_multi, NULL, 0, 0, NULL);
		svcSleepThread(10000000LL);
	}
	CURLMsg *msg;
	int left;
	while (!*aborted && (msg = curl_multi_info_read(covers_curl_multi, &left)))
		if (msg->msg == CURLMSG_DONE)
			rc = msg->data.result;
	if (rc == CURLE_ABORTED_BY_CALLBACK && (covers_quit || covers_paused)) *aborted = true;
	curl_multi_remove_handle(covers_curl_multi, c);
	if (*aborted)
		covers_net_close();
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
	covers_last_curl = rc;
	covers_last_http = *code;
	if (*aborted) __atomic_add_fetch(&covers_aborted_count, 1, __ATOMIC_RELAXED);
	return rc;
}

static bool network_error(CURLcode rc)
{
	return rc == CURLE_COULDNT_CONNECT || rc == CURLE_COULDNT_RESOLVE_HOST || rc == CURLE_OPERATION_TIMEDOUT;
}

// no Wi-Fi, no server: every try waits twice as long as the one before
static void net_failed(void)
{
	covers_net_backoff = covers_net_backoff ? covers_net_backoff * 2 : 15000;
	if (covers_net_backoff > 600000) covers_net_backoff = 600000;
	covers_net_retry_at = osGetTime() + covers_net_backoff;
}

static u32 *download(const char *key, bool *retry, int *pw, int *ph)
{
	char name[256], esc[768], url[1024];
	covers_thumb_name(key, name, sizeof(name));
	covers_url_escape(name, esc, sizeof(esc));
	snprintf(url, sizeof(url), "%s%s.png", THUMB_URL, esc);
 const char *slash=strchr(key,'/');
 char id[16];
 if(slash && (size_t)(slash-key)<sizeof(id)) {
  snprintf(id,sizeof(id),"%.*s",(int)(slash-key),key);
  const bb_system_t *system=bb_system_by_id(id);
  if(!system || !system->cover_db)return NULL;
  char database[256];covers_url_escape(system->cover_db,database,sizeof(database));
  covers_thumb_name(slash+1,name,sizeof(name));covers_url_escape(name,esc,sizeof(esc));
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
		log_printf("covers: network error %d, retrying in %llus\n", rc, covers_net_backoff / 1000);
	} else {
		log_printf("covers: %s -> %d / HTTP %ld\n", name, rc, code);
		if (!aborted && code != 404)
			net_failed();   // server trouble or a broken picture: not again at once
	}
	if (px) {
		covers_net_backoff = 0;
		__atomic_add_fetch(&covers_downloaded, 1, __ATOMIC_RELAXED);
	} else if (!aborted) __atomic_add_fetch(&covers_failures, 1, __ATOMIC_RELAXED);
	remove(COVER_DIR "/download.tmp");
	return px;
}
// Worker thread, nothing else to do: fetch one wanted logo. true if it tried.
static bool logos_work(void)
{
	for (int i = 0; i < BB_MAX_SYSTEMS && i < (int)bb_system_count; i++) {   // a saved logo the shelf wants
		if (!covers_logos[i].decode || covers_logos[i].px || !covers_logos[i].have) continue;
		char path[128];
		int w, h;
		logo_path(bb_systems[i], ".png", path, sizeof(path));
		u32 *px = covers_mark_decode(path, LOGO_MAX, &w, &h);
		if (!px) {   // damaged: forget it, fetched again next session
			remove(path);
			covers_logos[i].have = false;
			covers_logos[i].decode = false;
			return true;
		}
		covers_logos[i].w = w;
		covers_logos[i].h = h;
		__atomic_store_n(&covers_logos[i].px, px, __ATOMIC_RELEASE);
		return true;
	}
	for (int i = 0; i < BB_MAX_SYSTEMS && i < (int)bb_system_count; i++) {
		const bb_system_t *s = bb_systems[i];
		if (!covers_logos[i].wanted || covers_logos[i].have || covers_logos[i].tried || !s->logo_url) continue;
		if (covers_quit || covers_paused || sw_nonet || osGetTime() < covers_net_retry_at) return false;
		char path[128], tmp[128];
		mkdir(LOGO_DIR, 0777);
		logo_path(s, ".png", path, sizeof(path));
		logo_path(s, ".tmp", tmp, sizeof(tmp));
		long code;
		bool aborted;
		LightLock_Lock(&covers_net_lock);
		CURLcode rc = fetch_file(s->logo_url, tmp, 256 * 1024, &code, &aborted);
		if (!aborted && rc != CURLE_OK && network_error(rc)) net_failed();
		LightLock_Unlock(&covers_net_lock);
		bool ok = false;
		if (!aborted && rc == CURLE_OK && code == 200) {
			int w, h;
			u32 *px = covers_mark_decode(tmp, LOGO_MAX, &w, &h);   // only a picture is kept
			if (px) {
				remove(path);
				ok = !rename(tmp, path);
				if (ok) {   // and kept decoded for the shelf
					covers_logos[i].w = w;
					covers_logos[i].h = h;
					__atomic_store_n(&covers_logos[i].px, px, __ATOMIC_RELEASE);
				} else
					free(px);
			}
		}
		remove(tmp);
		log_printf("covers: logo %s -> %s (curl %d, HTTP %ld)\n", s->id, ok ? "saved" : "none", rc, code);
		if (ok) {
			covers_logos[i].have = true;
		} else if (!aborted && !network_error(rc)) {
			covers_logos[i].tried = true;   // the server has no such picture: not again this session
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
static bool presets_stop(void) { return covers_quit || covers_paused; }

// ---------------------------------------------------------------- worker

void covers_state_flush(void)
{
	LightLock_Lock(&covers_state_lock);
	boxart_misses_save(COVER_STATE, BB_VERSION);
	LightLock_Unlock(&covers_state_lock);
}

// A cover that has to come from the network (nothing cached): a finished
// cover, or false. *retry false: the database has no such picture. A picture
// it did not have lately is not asked for again (boxart_miss_*).
static bool fetch_cover(const game_t *g, bool *retry, cover_t *c)
{
	if (covers_quit || sw_nonet)
		return false;
	LightLock_Lock(&covers_state_lock);
	bool missed = boxart_miss_known(g->cover_key, osGetTime());
	LightLock_Unlock(&covers_state_lock);
	if (missed) {
		__atomic_add_fetch(&covers_miss_skips, 1, __ATOMIC_RELAXED);
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
	LightLock_Lock(&covers_net_lock);
	for (int i = 0; i < n && all_404 && !ok; i++) {
		if (covers_quit || covers_paused || osGetTime() < covers_net_retry_at) {
			all_404 = false;
			break;
		}
		int w = 0, h = 0;
		bool r = true;
		u32 *px = download(cand[i], &r, &w, &h);
		ok = px && pack_cover(px, w, h, c);
		free(px);
		if (ok && strcmp(cand[i], own))
			__atomic_add_fetch(&covers_other_names, 1, __ATOMIC_RELAXED);
		if (ok || r)
			all_404 = false;   // got it, or no word from the database: not "it has none"
	}
	LightLock_Unlock(&covers_net_lock);
	if (!ok && all_404) {
		*retry = false;
		LightLock_Lock(&covers_state_lock);
		boxart_miss_add(g->cover_key, osGetTime());
		LightLock_Unlock(&covers_state_lock);
		covers_state_flush();
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
			LightLock_Lock(&covers_state_lock);
			boxart_user_clear(key);
			LightLock_Unlock(&covers_state_lock);
			covers_state_flush();
		}
		return false;
	}
	if (marked && size == (long)ps.st_size && mtime == (long)ps.st_mtime && !stat(path, &cs) && read_cv2(path, c)) {
		__atomic_add_fetch(&covers_cache_hits, 1, __ATOMIC_RELAXED);
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
		LightLock_Lock(&covers_state_lock);
		boxart_user_set(key, (long)ps.st_size, (long)ps.st_mtime);
		LightLock_Unlock(&covers_state_lock);
		covers_state_flush();
	}
	return ok;
}

// *fresh: the cover is new (not the cached one): its small copy is made again
static bool load_cover(const game_t *g, bool *retry, bool refresh, cover_t *c, bool *fresh)
{
	char path[PATH_LEN], legacy[PATH_LEN], own[PATH_LEN];
	covers_cache_path(g->cover_key, ".cv2", path, sizeof(path));
	covers_cache_path(g->cover_key, ".png", own, sizeof(own));

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
		__atomic_add_fetch(&covers_cache_hits, 1, __ATOMIC_RELAXED);
		return true;
	}
	// 2. the old small cache: show it now, the full size one comes later
	covers_cache_path(g->cover_key, ".bin", legacy, sizeof(legacy));
	if (!refresh && read_legacy(legacy, c)) {
		c->lowres = !sw_nonet;
		__atomic_add_fetch(&covers_cache_hits, 1, __ATOMIC_RELAXED);
		return true;
	}
	// 3. download
	if (!fetch_cover(g, retry, c))
		return false;
	*fresh = true;
	write_cv2(path, c);
	remove(legacy);
	LightLock_Lock(&covers_state_lock);
	boxart_user_clear(g->cover_key);
	LightLock_Unlock(&covers_state_lock);
	covers_state_flush();
	return true;
}

// Replace an old small cover with the full size one. A picture the database
// does not have (404) makes the small one final.
static bool upgrade_cover(const game_t *g, bool *retry, cover_t *c)
{
	char path[PATH_LEN], legacy[PATH_LEN];
	covers_cache_path(g->cover_key, ".cv2", path, sizeof(path));
	covers_cache_path(g->cover_key, ".bin", legacy, sizeof(legacy));
	*retry = true;
	bool ok = fetch_cover(g, retry, c);
	if (!ok && !*retry)
		ok = read_legacy(legacy, c);
	if (ok) {
		write_cv2(path, c);
		remove(legacy);
		__atomic_add_fetch(&covers_upgraded, 1, __ATOMIC_RELAXED);
	}
	return ok;
}


// Nothing to load for the screen: make small copies of cached covers that
// have none yet (nearest to the selection first), and save the small cover
// file a few seconds after the last change. true: did something.
static bool thumbs_idle_work(void)
{
	if (!covers_thumbs_loaded || covers_quit || covers_paused)
		return false;
	int n, game = -1;
	bool save = false;
	covers_lock();
	// Claim before even reading the library count: stop_wait must cover every
	// worker access to the list, including idle thumbnail selection.
	if (covers_paused) {
		LightLock_Unlock(&covers_list_lock);
		return false;
	}
	n = library_count();
	if (covers_thumb_tried_n != n) {
		free(covers_thumb_tried);
		covers_thumb_tried = calloc(n ? n : 1, 1);
		covers_thumb_tried_n = covers_thumb_tried ? n : 0;
	}
	for (int d = 0; covers_thumb_tried && d < n && game < 0; d++)
		for (int side = 0; side < 2 && game < 0; side++) {
			int i = (covers_focus_game < 0 ? 0 : covers_focus_game) + (side ? -d : d);
			if (i < 0 || i >= n || covers_thumb_tried[i])
				continue;
			covers_thumb_tried[i] = 1;
			const game_t *g = library_get(i);
			if (g && covers_thumb_find(covers_name_hash(g->cover_key)) < 0)
				game = i;
		}
	if (game < 0 && covers_thumbs_dirty && osGetTime() - covers_thumbs_dirty_at > 3000)
		save = true;
	if (game >= 0 || save)
		__atomic_add_fetch(&covers_busy_workers, 1, __ATOMIC_SEQ_CST);
	LightLock_Unlock(&covers_list_lock);
	if (save)
		thumbs_save();
	else if (game >= 0) {
		cover_record scratch;
		const game_t *g = cover_game(game, &scratch);
		char path[PATH_LEN];
		cover_t c = { 0 };
		if (g) {
			covers_cache_path(g->cover_key, ".cv2", path, sizeof(path));
			bool ok = read_cv2(path, &c);
			if (!ok) {
				covers_cache_path(g->cover_key, ".bin", path, sizeof(path));
				ok = read_legacy(path, &c);
			}
			if (ok) {
				thumb_make(g->cover_key, &c, false);
				__atomic_add_fetch(&covers_thumb_filled, 1, __ATOMIC_RELAXED);
			}
			free(c.data);
		}
		// leave the UI core some room on Old 3DS (the worker shares core 0 there)
		svcSleepThread(g_new3ds ? 1000000LL : 8000000LL);
	}
	if (game >= 0 || save)
		__atomic_sub_fetch(&covers_busy_workers, 1, __ATOMIC_SEQ_CST);
	return game >= 0 || save;
}

void covers_worker_main(void *arg)
{
	(void)arg;
	while (!covers_quit) {
		// timed: a one-shot signal can be lost when two arrive together
		// (LightEvent_Signal does nothing on an already-signalled event),
		// so never rely on one to see quit
		LightEvent_WaitTimeout(&covers_wake, 100000000LL);
		for (;;) {
			while (covers_paused && !covers_quit)
				svcSleepThread(100000000LL);
			if (covers_quit)
				break;
			// the small covers first: they show while the rest loads
			if ((!covers_thumbs_loaded || !covers_own_pngs_ok) && !covers_quit) {
				covers_lock();
				bool mine = (!covers_thumbs_loaded || !covers_own_pngs_ok) && !covers_thumbs_loading && !covers_paused;
				if (mine) {
					covers_thumbs_loading = true;
					__atomic_add_fetch(&covers_busy_workers, 1, __ATOMIC_SEQ_CST);
				}
				LightLock_Unlock(&covers_list_lock);
				if (mine) {
					if (!covers_own_pngs_ok) own_pngs_scan();
					if (!covers_thumbs_loaded) {HT("thumbs enter");thumbs_load();HT("thumbs leave");}
					covers_thumbs_loading = false;
					__atomic_sub_fetch(&covers_busy_workers, 1, __ATOMIC_SEQ_CST);
					continue;
				}
			}
			int pick = -1, game = -1, best = 1 << 30;
			bool refresh = false, upgrade = false;
			covers_lock();
			for (int i = 0; i < SLOTS; i++) {
				if (covers_slots[i].state != 1 || osGetTime() < covers_slots[i].retry_at)
					continue;
				// not asked for in the last few frames: scrolled away
				if (covers_slots[i].last_used + 40 < covers_use_clock) {
					covers_slots[i].state = 0;
					covers_slots[i].game = -1;
					continue;
				}
				int d = (int)covers_slots[i].rank;   // the UI asks for the selection first, then outward
				if (d < best) {
					best = d;
					pick = i;
					game = covers_slots[i].game;
				}
			}
			// Nothing new to load: swap a shown old small cover for the full size one.
			if (pick < 0 && !covers_quit && !covers_paused && !sw_nonet) {
				best = 1 << 30;
				for (int i = 0; i < SLOTS; i++) {
					if (covers_slots[i].state != 3 || !covers_slots[i].lowres || covers_slots[i].upgrading ||
					    osGetTime() < covers_slots[i].retry_at || covers_slots[i].last_used + 40 < covers_use_clock)
						continue;
					if ((int)covers_slots[i].rank < best) {
						best = covers_slots[i].rank;
						pick = i;
						game = covers_slots[i].game;
						upgrade = true;
					}
				}
			}
			if (pick >= 0 && !covers_quit && !covers_paused) {
				__atomic_add_fetch(&covers_busy_workers, 1, __ATOMIC_SEQ_CST);
				refresh = covers_slots[pick].refresh;
				if (upgrade)
					covers_slots[pick].upgrading = true;   // stays shown meanwhile
				else
					covers_slots[pick].state = 5;   // ours: the other worker skips it
			} else pick = -1;
			// Nothing to fetch for the library: the daily preset check.
			bool presets = pick < 0 && !covers_quit && !covers_paused && !covers_presets_claimed && !sw_nonet && presets_update_due();
			if (presets) {
				covers_presets_claimed = true;
				__atomic_add_fetch(&covers_busy_workers, 1, __ATOMIC_SEQ_CST);
			}
			LightLock_Unlock(&covers_list_lock);
			if (presets) {
				LightLock_Lock(&covers_net_lock);
				if (!covers_quit && !covers_paused && osGetTime() >= covers_net_retry_at)
					presets_update_work(presets_fetch, presets_stop);
				LightLock_Unlock(&covers_net_lock);
				covers_lock();
				covers_presets_claimed = false;
				LightLock_Unlock(&covers_list_lock);
				__atomic_sub_fetch(&covers_busy_workers, 1, __ATOMIC_SEQ_CST);
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
			if (ok && covers_thumbs_loaded)
				thumb_make(g->cover_key, &c, fresh || upgrade);
			log_printf("covers: #%d %s -> %s%s in %llums\n", game, g ? g->cover_key : "?",
				ok ? "ok" : "none", ok && c.lowres ? " (small)" : "", osGetTime() - t0);
			covers_lock();
			if (covers_slots[pick].game == game && (upgrade ? covers_slots[pick].upgrading : covers_slots[pick].state == 5)) {
				if (ok) {
					free(covers_slots[pick].pixels);
					covers_slots[pick].pixels = c.data;
					covers_slots[pick].w = c.w;
					covers_slots[pick].h = c.h;
					covers_slots[pick].lowres = c.lowres;
					covers_slots[pick].refresh = false;
					c.data = NULL;
				}
				// interrupted by HOME/sleep: try again later, not "no cover";
				// without a network, not before the backoff is over
				covers_slots[pick].retry_at = covers_paused ? 0 : (covers_net_retry_at > osGetTime() + 15000 ? covers_net_retry_at : osGetTime() + 15000);
				if (upgrade) {
					covers_slots[pick].upgrading = false;
					if (ok)
						covers_slots[pick].state = 2;   // the shown picture is replaced
					else if (!retry)
						covers_slots[pick].lowres = false;
				} else {
					covers_slots[pick].state = ok ? 2 : (retry ? 1 : 4);
				}
			}
			free(c.data);
			LightLock_Unlock(&covers_list_lock);
			HT("covers job leave");
			__atomic_sub_fetch(&covers_busy_workers, 1, __ATOMIC_SEQ_CST);
		}
	}
}
