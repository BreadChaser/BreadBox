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
#include "covers_internal.h"
#include "cold_code.h"




slot_t covers_slots[SLOTS];

thumb_t **covers_thumbs;          // sorted by key (under lock)
int covers_nthumbs;
static int covers_thumbs_cap;
atomic_bool covers_thumbs_loaded, covers_thumbs_loading;
bool covers_thumbs_dirty;
u64 covers_thumbs_dirty_at;
u8 *covers_thumb_tried;           // per game: the background fill has looked at it
int covers_thumb_tried_n;
u64 *covers_own_pngs;             // the player's PNGs in the covers folder (name hashes, sorted)
int covers_nown_pngs;
atomic_bool covers_own_pngs_ok;          // the folder was listed
int covers_focus_game = -1;
tslot_t covers_tslots[TSLOTS];
static u32 pump_frame;            // frames the covers were drawn in
static int thumb_uploads_left;    // this frame's budget
unsigned covers_thumb_made, covers_thumb_filled, covers_thumb_load_ms, covers_thumb_save_ms;
static Thread workers[WORKERS];
LightLock covers_list_lock;

LightEvent covers_wake;
atomic_bool covers_quit;
u64 covers_net_retry_at;   // after a network failure, wait before trying again
u64 covers_net_backoff;    // that wait: 15 s, doubling to 10 min, 0 after a success
LightLock covers_state_lock;   // boxart.c's memory of misses and own pictures
static bool apt_was_paused;
static unsigned apt_suspended;
unsigned covers_attempts, covers_downloaded, covers_cache_hits, covers_aborted_count, covers_failures, covers_upgraded;
unsigned covers_miss_skips, covers_other_names;   // not asked for (remembered miss); fetched under another name
static unsigned uploads, upload_max_us, upload_total_us;   // main thread: texture copies
// library frame intervals (a developer diagnostic: is the carousel smooth?)
static u64 frame_last;
static unsigned frames, frames_over_25ms, frames_over_40ms, frame_max_us;
static u64 frame_total_us;
// how fast covers appear while scrolling (developer diagnostics, "covers scroll=")
bool covers_slow;                  // "covers fast=off": as before small covers (an A/B in one build)
unsigned covers_sd_ms;             // "covers sdms=N": cache reads take N ms per 128 KB longer
                                   // (the console's SD card, in an emulator)

int covers_last_curl;
long covers_last_http;
extern unsigned covers_stop_ms, covers_max_stop_ms;
atomic_bool covers_paused; // gameplay / HOME / sleep: do not start work
atomic_int covers_busy_workers;    // workers currently fetching/decoding a cover or presets
static bool initialized, library_was_paused;
static unsigned library_changes; // UI-owned; blocks resume until publication is complete
static bool try_open, try_was_paused;
static unsigned gpu_generation;
bool covers_presets_claimed; // a worker runs the preset check (under lock)
static aptHookCookie apt_cookie;


// The system tells us about HOME, sleep and closing right away (from inside
// aptMainLoop). Stop network work immediately instead of whenever the main
// loop next looks.
static void on_apt(APT_HookType hook, void *param)
{
	log_printf("covers: apt hook %d, busy %d\n", hook, covers_busy_workers);
	(void)param;

	switch (hook) {
	case APTHOOK_ONSUSPEND:
	case APTHOOK_ONSLEEP:
		if (!apt_suspended) apt_was_paused = library_changes ? library_was_paused : covers_paused;
		apt_suspended |= hook == APTHOOK_ONSUSPEND ? 1 : 2;
		covers_stop_wait_apt();
		break;
	case APTHOOK_ONRESTORE:
	case APTHOOK_ONWAKEUP:
		apt_suspended &= ~(hook == APTHOOK_ONRESTORE ? 1u : 2u);
		if (apt_suspended) break;
        if(bb_update_installing() || bb_update_committed()){covers_paused=true;break;}
		covers_paused = apt_was_paused || library_changes;
		covers_net_retry_at = osGetTime() + 1000;   // the network needs a moment back
		LightEvent_Signal(&covers_wake);
		LightEvent_Signal(&covers_wake);
		break;
	case APTHOOK_ONEXIT:
		covers_quit = true;                         // aborts transfers in progress
		LightEvent_Signal(&covers_wake);
		LightEvent_Signal(&covers_wake);
		break;
	default:
		break;
	}
}
u32 covers_use_clock;
static volatile u32 frame_base;   // use_clock when the UI's current frame began (covers_focus)
static u32 *soc_buf;
static bool soc_started;

// The network. Only one thread at a time may use it (net_lock): all our
// socket calls share one session with the 3DS socket service, and a thread
// blocked in poll() there holds up every other socket call, ours and, while
// suspended, the HOME Menu's. That froze the 3DS on HOME. So curl is driven
// by hand with nonblocking polls and 10ms sleeps, and every socket
// is closed (net_close) before HOME or sleep.
LightLock covers_net_lock;
CURL *covers_curl_easy;
CURLM *covers_curl_multi;
CURLSH *covers_curl_share;            // DNS and TLS sessions, kept across net_close
volatile bool covers_net_open;   // multi holds connections
covers_logo_state covers_logos[BB_MAX_SYSTEMS];

// ---------------------------------------------------------------- small covers

// caller holds lock: the index of key, or where it would go (-1 - index)

// caller holds lock; t is then owned by the list
bool covers_thumb_insert(thumb_t *t)
{
	int i = covers_thumb_find(t->key);
	if (i >= 0) {
		free(covers_thumbs[i]);
		covers_thumbs[i] = t;
		return true;
	}
	i = -1 - i;
	if (covers_nthumbs == covers_thumbs_cap) {
		int cap = covers_thumbs_cap ? covers_thumbs_cap * 2 : 256;
		thumb_t **n = realloc(covers_thumbs, cap * sizeof(*n));
		if (!n) return false;
		covers_thumbs = n;
		covers_thumbs_cap = cap;
	}
	memmove(covers_thumbs + i + 1, covers_thumbs + i, (covers_nthumbs - i) * sizeof(*covers_thumbs));
	covers_thumbs[i] = t;
	covers_nthumbs++;
	return true;
}
void covers_thumbs_free(void)
{
	covers_lock();
	for (int i = 0; i < covers_nthumbs; i++)
		free(covers_thumbs[i]);
	free(covers_thumbs);
	covers_thumbs = NULL;
	covers_nthumbs = covers_thumbs_cap = 0;
	covers_thumbs_loaded = false;
	covers_thumbs_dirty = false;
	LightLock_Unlock(&covers_list_lock);
}

static int logo_index(const bb_system_t *s)
{
	for (unsigned i = 0; i < bb_system_count && i < BB_MAX_SYSTEMS; i++)
		if (bb_systems[i] == s) return (int)i;
	return -1;
}
const u32 *covers_logo(const bb_system_t *s, int *w, int *h)
{
	int i = s && s->logo_url ? logo_index(s) : -1;
	if (i < 0) return NULL;
	if (!covers_logos[i].px) {
		char path[128];
		logo_path(s, ".png", path, sizeof(path));
		if (!covers_logos[i].checked) {
			struct stat st;
			covers_logos[i].checked = true;
			if (!stat(path, &st)) covers_logos[i].have = true;
		}
		if (covers_logos[i].have && !covers_logos[i].decode) {   // decoded by the worker: a PNG decode is frames of the UI
			covers_logos[i].decode = true;
			LightEvent_Signal(&covers_wake);
		}
	}
	if (covers_logos[i].px) {
		*w = covers_logos[i].w;
		*h = covers_logos[i].h;
		return covers_logos[i].px;
	}
	if (!covers_logos[i].have && !covers_logos[i].wanted) {
		covers_logos[i].wanted = true;
		LightEvent_Signal(&covers_wake);
	}
	return NULL;
}

// ---------------------------------------------------------------- API

void covers_init(void)
{
	mkdir(COVER_DIR, 0777);
 mkdir(COVER_DIR "/gba",0777);
	covers_paused = true; // library entry explicitly resumes after SOC/control init
	for (int i = 0; i < SLOTS; i++)
		covers_slots[i].game = -1;
	for (int i = 0; i < TSLOTS; i++)
		covers_tslots[i].game = -1;
	LightLock_Init(&covers_list_lock);
	LightLock_Init(&covers_state_lock);
	boxart_misses_load(COVER_STATE, BB_VERSION);
	LightEvent_Init(&covers_wake, RESET_ONESHOT);   // each Signal wakes one worker
	// the network: 3dslink logging may already have started it
	// (3dslink logging may already have started the network)
	if (!dev_remote && !sw_nonet && !soc_buf && (soc_buf = memalign(0x1000, 0x100000)))
		soc_started = R_SUCCEEDED(socInit(soc_buf, 0x100000));
	curl_global_init(CURL_GLOBAL_DEFAULT);
	LightLock_Init(&covers_net_lock);
	initialized = true;
	covers_curl_easy = curl_easy_init();
	if ((covers_curl_share = curl_share_init())) {
		curl_share_setopt(covers_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
		curl_share_setopt(covers_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
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
		workers[i] = g_new3ds ? threadCreate(covers_worker_main, NULL, 64 * 1024, prio + 4, core, false) : NULL;
		if (!workers[i])
			workers[i] = threadCreate(covers_worker_main, NULL, 64 * 1024, prio, core = 0, false);
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
 covers_quit=true;
 bool stopped=true;
 for(int i=0;i<WORKERS;i++){
  if(!workers[i])continue;
  LightEvent_Signal(&covers_wake);
  if(threadJoin(workers[i],timeout/WORKERS)!=0){stopped=false;continue;}   /* a timeout is not R_FAILED */
  threadFree(workers[i]);workers[i]=NULL;
 }
 if(!stopped)return false;
 covers_net_close();
 if(covers_curl_easy)curl_easy_cleanup(covers_curl_easy);covers_curl_easy=NULL;
 if(covers_curl_share)curl_share_cleanup(covers_curl_share);covers_curl_share=NULL;
 curl_global_cleanup();
 if(soc_started){socExit();soc_started=false;}
 return true;
}

bool covers_exit(void)
{
	video_pre_draw = NULL;
	aptUnhook(&apt_cookie);
	covers_quit = true;
	bool stopped = true;
	for (int i = 0; i < WORKERS; i++) {
		if (!workers[i])
			continue;
		// workers notice quit within 100ms; give up after 3s rather than
		// hang the exit
		bool done = false;
		for (int t = 0; t < 30 && !done; t++) {
			LightEvent_Signal(&covers_wake);
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
		free(covers_slots[i].pixels);
		if (covers_slots[i].tex_ok)
			C3D_TexDelete(&covers_slots[i].tex);
	}
	for (int i = 0; i < TSLOTS; i++)
		if (covers_tslots[i].tex_ok)
			C3D_TexDelete(&covers_tslots[i].tex);
	for (int i = 0; i < BB_MAX_SYSTEMS; i++) {
		free(covers_logos[i].px);
		memset(&covers_logos[i], 0, sizeof(covers_logos[i]));
	}
	covers_thumbs_free();
	free(covers_own_pngs);
	covers_own_pngs = NULL;
	free(covers_thumb_tried);
	covers_thumb_tried = NULL;
	covers_net_close();
	covers_state_flush();
	boxart_misses_free();
	if (covers_curl_easy)
		curl_easy_cleanup(covers_curl_easy);
	if (covers_curl_share)
		curl_share_cleanup(covers_curl_share);
	covers_curl_easy = NULL;
	covers_curl_share = NULL;
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
	if (!covers_thumbs_loaded || !covers_nthumbs || covers_slow)
		return NULL;
	tslot_t *t = NULL, *lru = NULL;
	for (int i = 0; i < TSLOTS; i++) {
		if (covers_tslots[i].game == game) {
			t = &covers_tslots[i];
			break;
		}
		if (covers_tslots[i].used != pump_frame && (!lru || covers_tslots[i].used < lru->used))
			lru = &covers_tslots[i];
	}
	if (!t) {
		cover_record scratch;
		const game_t *g = cover_game(game, &scratch);
		int k = g && lru && thumb_uploads_left > 0 ? covers_thumb_find(covers_name_hash(g->cover_key)) : -1;
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
		const thumb_t *src = covers_thumbs[k];
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
	LightLock_Unlock(&covers_list_lock);
	return tex;
}

// Texture for game i if it's ready (or its small copy while drawing);
// otherwise queue it and return NULL.
C3D_Tex *covers_get(int game, Tex3DS_SubTexture *sub)
{
	if (sw_nocovers || library_changes)
		return NULL;
	slot_t *s = NULL;
	covers_use_clock++;
	covers_lock();
	for (int i = 0; i < SLOTS; i++)
		if (covers_slots[i].game == game) {
			s = &covers_slots[i];
			break;
		}
	if (!s) {
		// reuse the least recently used slot (a queued one too: the worker
		// skips slots whose game changed under it)
		for (int i = 0; i < SLOTS; i++)
			if (!s || covers_slots[i].last_used < s->last_used)
				s = &covers_slots[i];
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
			LightEvent_Signal(&covers_wake);
		}
	}
	if (s) {
		// keep the best rank it got this frame (drawing asks again, later)
		u32 r = covers_use_clock - frame_base;
		if (s->last_used < frame_base || r < s->rank)
			s->rank = r;
		s->last_used = covers_use_clock;
	}
	bool shown = s && s->shown && s->tex_ok;
	if (shown && sub) {
		// the picture is the top left w x h of the texture; top = 1 is its first row
		float u = (float)s->w / COVER_TEX, v = 1.0f - (float)s->h / COVER_TEX;
		*sub = (Tex3DS_SubTexture){ s->w, s->h, 0.0f, 1.0f, u, v };
	}
	C3D_Tex *tex = shown ? &s->tex : sub ? thumb_get(game, sub) : NULL;
	LightLock_Unlock(&covers_list_lock);
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
		if (covers_slots[i].game == game) { done = (covers_slots[i].shown && covers_slots[i].tex_ok && !covers_slots[i].lowres) || covers_slots[i].state == 4; break; }
	LightLock_Unlock(&covers_list_lock);
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
		slot_t *s = &covers_slots[i];
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
		LightLock_Unlock(&covers_list_lock);
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
	for (int i = 0; i < SLOTS; i++) if (covers_slots[i].game == game && covers_slots[i].state != 5 && !covers_slots[i].upgrading) {
		free(covers_slots[i].pixels);
		covers_slots[i].pixels = NULL;
		covers_slots[i].lowres = false;
		covers_slots[i].refresh = true;
		covers_slots[i].retry_at = 0;
		covers_slots[i].state = 1;
		ok = true;
		break;
	}
	LightLock_Unlock(&covers_list_lock);
	LightEvent_Signal(&covers_wake);
	return ok;
}

// true while a cover is loading or a connection is open: HOME must wait
bool covers_busy(void)
{
 if(bb_update_net_busy())return true;
	if (sw_nocovers)
		return false;
	return covers_busy_workers > 0 || covers_net_open;
}

// Pause the workers and close the network. Doesn't block: call it each
// frame until it returns true (the network loop checks pause every 10ms).
PSX3D_COLD const char *covers_stop_pending(void)
{
 /* Request all stops together, even if the importer has not settled yet. */
 bool importing = !breadbox_import_pause();
 bool updating = !bb_update_net_stop();
 const char *pending = importing ? "importer" : updating ? "update network" : NULL;
 if (sw_nocovers) return pending;
 /* A stuck filesystem/decode operation may hold either lock. Never block here. */
 if (LightLock_TryLock(&covers_list_lock) != 0) return pending ? pending : "cover list";
 covers_paused = true;
 bool active = covers_busy_workers > 0;
 LightLock_Unlock(&covers_list_lock);
 if (active) return pending ? pending : "cover loader";
 if (LightLock_TryLock(&covers_net_lock) != 0) return pending ? pending : "cover network";
 covers_net_close();
 LightLock_Unlock(&covers_net_lock);
 return pending;
}

bool covers_stop(void) { return covers_stop_pending() == NULL; }

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
	covers_paused = false;
	LightEvent_Signal(&covers_wake);
	LightEvent_Signal(&covers_wake);
}

// HOME during a cover download froze the app (11ec33b8). While the workers use the network HOME is held back; a
// press is caught, the downloads stop (polled, never waited for here), then the app goes to the HOME Menu itself.
// Every menu loop that runs the covers (Home, the Library) calls this once a frame; true when it went HOME.
static bool home_pending;
PSX3D_COLD bool covers_wait_home(void)
{
 if (aptCheckHomePressRejected()) home_pending = true;
 return home_pending;
}
bool covers_home_frame(void)
{
	aptSetHomeAllowed(!home_pending && !covers_busy());
	if (aptCheckHomePressRejected()) home_pending = true;
	if (!home_pending || !covers_stop()) return false;
	home_pending = false;
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
	covers_focus_game = game;
	frame_base = covers_use_clock + 1;
	LightEvent_Signal(&covers_wake);
	LightEvent_Signal(&covers_wake);
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
		slot_t *s = &covers_slots[i];
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
		if (covers_tslots[i].tex_ok)
			C3D_TexDelete(&covers_tslots[i].tex);
		covers_tslots[i].tex_ok = false;
		covers_tslots[i].game = -1;
	}
	covers_own_pngs_ok = false;  // and the player's PNGs looked for again
	LightLock_Unlock(&covers_list_lock);
	// The small covers' memory (8 KiB a game, 1.6 MB for 200) goes to the game
	// too; they are read again in the library (one file). Unsaved new ones
	// are made again from the cache by the background fill.
	covers_thumbs_free();
	covers_lock();
	free(covers_thumb_tried);
	covers_thumb_tried = NULL;
	covers_thumb_tried_n = 0;
	LightLock_Unlock(&covers_list_lock);
}

// Called only after the workers have drained. Numeric indexes belong to
// one published library; none may survive its replacement, even at equal size.
static void invalidate_library(void)
{
	covers_lock();
	for (int i = 0; i < SLOTS; i++) {
		covers_slots[i].game = -1;
		covers_slots[i].state = 0;
		covers_slots[i].shown = false;
		covers_slots[i].lowres = false;
		covers_slots[i].upgrading = false;
		free(covers_slots[i].pixels);
		covers_slots[i].pixels = NULL;
	}
	for (int i = 0; i < TSLOTS; i++)
		covers_tslots[i].game = -1;
	free(covers_thumb_tried);
	covers_thumb_tried = NULL;
	covers_thumb_tried_n = 0;
	covers_own_pngs_ok = false;   // the worker looks at the folder again
	LightLock_Unlock(&covers_list_lock);
}

// A library mutation is a transaction on the UI thread. Workers may borrow
// game_t only while busy; drain them BEFORE freeing or sorting any entries.
// BreadBox keeps this transaction open across incremental import frames.
bool covers_library_begin(void)
{
	if (sw_nocovers || !initialized) return true;
	if (library_changes) { library_changes++; return true; }
	bool was_paused = covers_paused;
	if (!covers_stop_wait()) return false;
	library_changes++;
	library_was_paused = was_paused;
	invalidate_library();
	return true;
}

// covers_library_begin without waiting: false while the worker is still in a job (a download or a decode:
// seconds on a cold card, the 2621 room froze 5.2 s on its first start). It is asked to stop and the caller
// asks again next frame; true is the same state covers_library_begin leaves (end it with covers_library_end).
bool covers_library_try_begin(void)
{
	if (sw_nocovers || !initialized) return true;
	if (library_changes) { library_changes++; return true; }
	if (!try_open) { try_open = true; try_was_paused = covers_paused; }
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
	if (!covers_library_begin()) return;
	LightLock_Lock(&covers_state_lock);
	boxart_misses_clear();
	LightLock_Unlock(&covers_state_lock);
	covers_state_flush();
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
		covers_cache_path(library_get(i)->cover_key, ".cv2", path, sizeof(path));
		if (!stat(path, &st)) {
			if (st.st_size == CV2_BYTES) cached++;
			else invalid++;
			continue;
		}
		covers_cache_path(library_get(i)->cover_key, ".bin", path, sizeof(path));
		if (stat(path, &st)) missing++;
		else if (st.st_size == COVER_LEGACY_SIZE * COVER_LEGACY_SIZE * 4) legacy++;
		else invalid++;
	}
	if (!sw_nocovers) {
		covers_lock();
		for (int i = 0; i < SLOTS; i++) {
			ready += covers_slots[i].state == 3;
			wanted += covers_slots[i].state == 1;
			textures += covers_slots[i].tex_ok && covers_slots[i].shown;
			pixels += covers_slots[i].pixels != NULL;
		}
		LightLock_Unlock(&covers_list_lock);
	}
	snprintf(out, len, "{\"ok\":true,\"gpu_generation\":%u,\"library_depth\":%u,\"textures\":%d,\"pixels\":%d,\"paused\":%s,\"busy\":%d,\"net_open\":%s,"
	         "\"attempts\":%u,\"downloaded\":%u,\"cache_hits\":%u,\"aborted\":%u,"
	         "\"failures\":%u,\"curl\":%d,\"http\":%ld,\"stop_ms\":%u,\"max_stop_ms\":%u,"
	         "\"ready\":%d,\"wanted\":%d,\"cached\":%d,\"missing\":%d,\"invalid_cache\":%d,"
	         "\"legacy\":%d,\"upgraded\":%u,\"miss_skips\":%u,\"other_names\":%u,\"uploads\":%u,\"upload_avg_us\":%u,\"upload_max_us\":%u,"
	         "\"frames\":%u,\"frame_avg_us\":%u,\"frame_max_us\":%u,\"frames_over_25ms\":%u,\"frames_over_40ms\":%u,"
	         "\"thumbs\":%d,\"thumbs_loaded\":%s,\"thumb_made\":%u,\"thumb_filled\":%u,\"thumb_load_ms\":%u,\"thumb_save_ms\":%u,\"own_pngs\":%d,\"fast\":%s,"
	         "\"heap_used\":%u,\"heap_arena\":%u,\"linear_free\":%u}",
	         gpu_generation, library_changes, textures, pixels, covers_paused ? "true" : "false", covers_busy_workers, covers_net_open ? "true" : "false", covers_attempts,
	         covers_downloaded, covers_cache_hits, covers_aborted_count, covers_failures, covers_last_curl, covers_last_http,
	         covers_stop_ms, covers_max_stop_ms, ready, wanted, cached, missing, invalid,
	         legacy, covers_upgraded, covers_miss_skips, covers_other_names, uploads, uploads ? upload_total_us / uploads : 0, upload_max_us,
	         frames, frames ? (unsigned)(frame_total_us / frames) : 0, frame_max_us, frames_over_25ms, frames_over_40ms,
	         covers_nthumbs, covers_thumbs_loaded ? "true" : "false", covers_thumb_made, covers_thumb_filled,
	         covers_thumb_load_ms, covers_thumb_save_ms, covers_nown_pngs, covers_slow ? "false" : "true",
	         heap_used, heap_arena, linear_free);
}
