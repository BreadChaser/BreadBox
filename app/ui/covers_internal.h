/* Private to the cover cache: app/ui/covers.c (the UI side, the API and the shared
 * state) and app/services/covers_fetch.c (the worker thread, the network, the cache files). */
#ifndef BB_COVERS_INTERNAL_H
#define BB_COVERS_INTERNAL_H
#include "fs_path.h"
#include "bb_update.h"
#include "room.h"
#include "breadbox_import.h"
#include "ht_trace.h"
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
#include "covers_match.h"

// External IDs never overlap PS1 indexes. Borrowed records obey the same
// covers_library_begin/end publication boundary as the PS1 library.
typedef struct {game_t game;
 char title[BB_TITLE_LEN],key[256],path[PATH_LEN];
} cover_record;
static __attribute__((unused)) const game_t *cover_game(int id, cover_record *scratch)
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

#define THUMB_FILE COVER_DIR "/thumbs.pk"
#define THUMB_MAGIC 0x31485450u   // "PTH1"
#define TSLOTS 40                 // small cover textures (8 KiB each)
typedef struct { u32 magic, count, bytes, zero; } thumb_head_t;
typedef struct { u64 key; u16 w, h; u32 zero; u8 px[COVER_THUMB_BYTES]; } thumb_t;   // also the file record
typedef struct { int game; u32 used; u16 w, h, tw, th; C3D_Tex tex; bool tex_ok; } tslot_t;
#define WORKERS 1   // one shared PS1/GBA queue and downloader

// ---------------------------------------------------------------- system logos
// A system's mark for the shelf spines (bb_system_t.logo_url) is not part of
// the app: the worker fetches it once, over the covers' connection and lock,
// into sdmc:/3ds/BreadBox/logos/<id>.png, and it is read from there from then
// on. Until then (and offline) the shelf draws its generic mark.
#define LOGO_DIR PSX3D_DIR "/logos"
#define LOGO_MAX 256   // px, either way
typedef struct {
	volatile bool wanted, have, tried;   // asked for / on the SD card / given up this session
	bool checked;                        // main thread: looked for the file
	volatile bool decode;                // main thread: wants the saved picture (the worker decodes it)
	u32 *volatile px;
	int w, h;
} covers_logo_state;
extern covers_logo_state covers_logos[BB_MAX_SYSTEMS];

// shared state, defined in covers.c
extern slot_t covers_slots[SLOTS];
extern thumb_t **covers_thumbs;
extern int covers_nthumbs;
extern atomic_bool covers_thumbs_loaded, covers_thumbs_loading;
extern bool covers_thumbs_dirty;
extern u64 covers_thumbs_dirty_at;
extern u8 *covers_thumb_tried;
extern int covers_thumb_tried_n;
extern u64 *covers_own_pngs;
extern int covers_nown_pngs;
extern atomic_bool covers_own_pngs_ok;
extern int covers_focus_game;
extern tslot_t covers_tslots[TSLOTS];
extern unsigned covers_thumb_made, covers_thumb_filled, covers_thumb_load_ms, covers_thumb_save_ms;
extern LightLock covers_list_lock;
extern LightEvent covers_wake;
extern atomic_bool covers_quit;
extern u64 covers_net_retry_at;
extern u64 covers_net_backoff;
extern LightLock covers_state_lock;
extern unsigned covers_attempts, covers_downloaded, covers_cache_hits, covers_aborted_count, covers_failures, covers_upgraded;
extern unsigned covers_miss_skips, covers_other_names;
extern int covers_last_curl;
extern long covers_last_http;
extern atomic_bool covers_paused;
extern atomic_int covers_busy_workers;
extern bool covers_presets_claimed;
extern u32 covers_use_clock;
extern LightLock covers_net_lock;
extern CURL *covers_curl_easy;
extern CURLM *covers_curl_multi;
extern CURLSH *covers_curl_share;
extern volatile bool covers_net_open;

// shared helpers, inlined into every caller as before the split
static inline void covers_lock(void) {
#ifdef HT_TRACE
 uint64_t t=ht_begin();
 LightLock_Lock(&covers_list_lock);
 if(t && svcGetSystemTick()-t>SYSCLOCK_ARM11*3/10000)ht_mark("covers lock ticks",(u32)(svcGetSystemTick()-t));
#else
 LightLock_Lock(&covers_list_lock);
#endif
}
static inline int covers_thumb_find(u64 key)
{
	int lo = 0, hi = covers_nthumbs;
	while (lo < hi) {
		int m = (lo + hi) / 2;
		if (covers_thumbs[m]->key == key) return m;
		if (covers_thumbs[m]->key < key) lo = m + 1;
		else hi = m;
	}
	return -1 - lo;
}
bool covers_thumb_insert(thumb_t *t);
void covers_thumbs_free(void);
// app/services/covers_fetch.c
void covers_worker_main(void *arg);
void covers_net_close(void);
void covers_state_flush(void);

static inline void logo_path(const bb_system_t *s, const char *ext, char *out, size_t len)
{
	snprintf(out, len, LOGO_DIR "/%s%s", s->id, ext);
}

#endif
