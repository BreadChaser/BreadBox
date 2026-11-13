#include "gba_saveid.h"
#include "app_policy.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "theme.h"
#include "ui_format.h"
#include "app_paths.h"
#include "app_log.h"
#include "app_config.h"
#include "platform_video.h"
#include "app_input.h"
#include "ui_list.h"
#include "ui_pause.h"
#include "ui.h"
#include "covers.h"
#include "uisound.h"
#include "libui.h"
#include "playlog.h"
#define LIST_HEADER ui_list_context
#include "devtools.h"
#include "bb_index.h"
#include "bb_system.h"
#include "ui_format.h"
#if defined(__has_include)
#if __has_include("gba.h")
#include "gba.h"
#define HAVE_GBA_CORE_HEADER 1
#else
int gba_run(const char *path) __attribute__((weak));
int gba_run(const char *path) { (void)path; return -1; }
const char *gba_error(void) __attribute__((weak));
#endif

#else
int gba_run(const char *path) __attribute__((weak));
int gba_run(const char *path) { (void)path; return -1; }
const char *gba_error(void) __attribute__((weak));
#endif

#define GBA_MAX 1024
// Small (the strings on the heap): the list stays in memory through a game, so the shelf
// has it at once after one (it was 830 B a game, given back at every game start).
typedef struct { char *path, *title, *cover_key; char id[GBA_ID_LEN+8], code[5]; } gba_entry_t;
static gba_entry_t *entries;
static int count, capacity;
// Workers borrow entries only while claimed busy; every mutation drains first.
bool gba_cover_item(int index, bb_cover_item *out)
{
 if(index<0 || index>=count)return false;
 gba_entry_t *g=&entries[index];
 // Output borrows stable strings; publication drains workers before mutation.
 *out=(bb_cover_item){.title=g->title,.key=g->cover_key,.code=g->code,.path=g->path};
 return true;
}

// A to Z once the list has settled (the shelf orders its boxes itself; gba_catalog's rows follow this)
static int title_cmp(const void *a, const void *b)
{
	const gba_entry_t *x = a, *y = b;
	return strcasecmp(x->title, y->title);
}
/* The worker owns the shared directory walk; this list only copies completed
 * records. No ROM, directory, network or cache I/O is needed to give the shelf its games. */
static unsigned index_generation;
static int copied;
static bool was_busy;
static u64 opened_at;

static void entry_free(gba_entry_t *g)
{
 free(g->path);free(g->title);free(g->cover_key);
 g->path=g->title=g->cover_key=NULL;
}
// A game starts: the list is kept (64 B a game and its strings; the same games come back from
// the index after it, see bb_index content()). Nothing to do, and no covers' drain.
void gba_library_release(void)
{
}
static bool room_for_one(void)
{
 if(count<capacity)return true;
 int want=capacity?capacity*2:64;if(want>GBA_MAX)want=GBA_MAX;
 if(want<=count)return false;
 gba_entry_t *more=realloc(entries,want*sizeof(*entries));
 if(!more)return false;
 entries=more;capacity=want;return true;
}

static bool sync_index(void)
{
 bool metadata = playlog_poll_adoptions();
 unsigned generation = bb_index_generation();
 int available = bb_index_gba_count();
 bool more = (generation != index_generation && (bb_index_complete() || available)) || (copied < available && count < GBA_MAX);
 bool settles = was_busy && bb_index_complete() && !more;
 bool changed = metadata;
 if(generation==index_generation && !more && !settles) {
  return metadata;   // nothing to copy: no covers' drain (it waited for their worker, seconds on the Old 3DS)
 }
 if (!covers_library_try_begin()) return false;
 if (generation != index_generation) {
  for(int i=0;i<count;i++)entry_free(&entries[i]);
  index_generation = generation; copied = count = 0;
  was_busy = true; changed = true;
 }
 for (int budget = 0; copied < available && count < GBA_MAX && budget < 32; budget++) {
  bb_entry entry;
  char path[PATH_LEN], title[256];
  if (!bb_index_get(bb_system_by_id("gba"),copied++,path,sizeof(path),&entry) || !room_for_one())continue;
  gba_entry_t *g = &entries[count];
  memset(g,0,sizeof(*g));
  char code[5];snprintf(code,sizeof(code),"%.4s",entry.cover);strcpy(g->code,code);
  const char *stem=strrchr(path,'/');stem=stem?stem+1:path;
  char key[256];snprintf(key,sizeof(key),"gba/%.250s",stem);
  char *ext=strrchr(key,'.');if(ext)*ext=0;
  ui_clean_title(stem, title, sizeof(title));
  if (!title[0]) snprintf(title, sizeof(title), "Game Boy Advance");
  g->path=strdup(path);g->title=strdup(title);g->cover_key=strdup(key);
  if(!g->path || !g->title || !g->cover_key){entry_free(g);continue;}
  snprintf(g->id,sizeof(g->id),"gba:%s",entry.key);
  playlog_adopt_key_later(entry.key,g->id);
  playlog_adopt_key_later(code[0]?code:g->title,g->id);
  count++; changed = true;
 }
 bool busy = !bb_index_complete() || (copied < available && count < GBA_MAX);
 if (was_busy && !busy) {
  covers_library_begin();
  qsort(entries, count, sizeof(entries[0]), title_cmp);
  covers_library_end();
  log_printf("gba-library: phase=list-ready count=%d of %d%s ms=%llu\n", count, available,
   bb_index_status()==BB_INDEX_LIMITED || bb_index_status()==BB_INDEX_ERROR ? " (scan incomplete)" : "", (unsigned long long)(osGetTime()-opened_at));
  changed = true;
 }
 was_busy = busy;
 covers_library_end();
 return changed;
}

/* Developer lookup uses the same cleaned library names, never a second scan. */
bool gba_remote_resolve(const char *title, char *path, size_t cap)
{
 sync_index(); /* never spin on a covers worker still finishing its I/O */
 int found=-1, matches=0;
 for(int i=0;i<count;i++) {
  if(!strcasecmp(entries[i].title,title)) {found=i;matches=1;break;}
  for(const char *p=entries[i].title;*p;p++)if(!strncasecmp(p,title,strlen(title))) {found=i;matches++;break;}
 }
 if(matches!=1)return false;
 snprintf(path,cap,"%s",entries[found].path);return true;
}

bool gba_library_poll(void)
{
 if(!opened_at)opened_at=osGetTime();
 u64 start=svcGetSystemTick();bool changed=sync_index();
 if(changed)log_printf("gba-library: phase=publish count=%d us=%lu scanning=%d\n",count,
  (unsigned long)((svcGetSystemTick()-start)*1000000/SYSCLOCK_ARM11),bb_index_busy());
 return changed;
}

bool gba_catalog(int row,bb_catalog_item *out)
{
 // Catalog reads never publish, sort, drain covers or touch the SD card.
 if(row<0 || row>=count)return false;
 gba_entry_t *g=&entries[row];
 snprintf(out->title,sizeof(out->title),"%s",g->title);
 snprintf(out->key,sizeof(out->key),"%s",g->id);
 snprintf(out->path,sizeof(out->path),"%s",g->path);
 out->cover_id=bb_system_cover_id(bb_system_by_id("gba"),row);return true;
}

