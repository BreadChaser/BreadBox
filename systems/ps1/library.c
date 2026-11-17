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
#include "ps1_library.h"

#define LIB_CACHE PSX3D_DIR "/library.txt"

game_t *ps1_library_games;
static game_t *visible_games;
static int visible_count,visible_capacity;
static bool visible_borrowed;
int ps1_library_ngames;
static bool library_building;
static char lib_root[PATH_LEN];

ps1_library_omissions ps1_library_omit;

int library_count(void) {
 return visible_count;
}
const game_t *library_get(int i) {
 return i>=0 && i<visible_count ? &visible_games[i]:NULL;
}

// frees everything a game owns
void ps1_library_game_free(game_t *g)
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
  for(int i=0;i<ps1_library_ngames;i++) {
   if(i>=visible_count)ps1_library_game_free(&ps1_library_games[i]);
   else for(int d=visible_games[i].ndiscs;d<ps1_library_games[i].ndiscs;d++)free(ps1_library_games[i].discs[d]);
  }
  free(ps1_library_games);ps1_library_games=NULL;ps1_library_ngames=0;visible_borrowed=false;return;
 }
	for (int i = 0; i < ps1_library_ngames; i++)
		ps1_library_game_free(&ps1_library_games[i]);
 free(ps1_library_games);ps1_library_games=NULL;
	ps1_library_ngames = 0;
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
	for (int i = 0; i < ps1_library_ngames; i++) {
		fprintf(f, "%s\t%s", ps1_library_games[i].title, ps1_library_games[i].cover_key);
		for (int d = 0; d < ps1_library_games[i].ndiscs; d++)
			fprintf(f, "\t%s", ps1_library_games[i].discs[d]);
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
	while (ok && fgets(line, sizeof(line), f) && ps1_library_ngames < MAX_GAMES) {
		line[strcspn(line, "\r\n")] = 0;
		char *tok = strtok(line, "\t");
		if (!tok)
			continue;
		game_t *g = &ps1_library_games[ps1_library_ngames];
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
			ps1_library_ngames++;
		else
			ps1_library_game_free(g);   // no disc, or out of memory
	}
	CACHE_CLOSE(f);
#undef CACHE_CLOSE
	return ok && ps1_library_ngames > 0;
}

/* UI-owned staging arrays never replace the published rows until fully sorted. */
static bool reserve_visible(void)
{
 if(ps1_library_ngames<=visible_capacity)return true;
 int cap=visible_capacity?visible_capacity:8;
 while(cap<ps1_library_ngames)cap*=2;
 game_t *rows=realloc(visible_games,cap*sizeof(*rows));if(!rows)return false;
 visible_games=rows;visible_capacity=cap;return true;
}
static void publish_library(void)
{
 if(!visible_borrowed)for(int i=0;i<visible_count;i++)ps1_library_game_free(&visible_games[i]);
 if(ps1_library_ngames)memcpy(visible_games,ps1_library_games,ps1_library_ngames*sizeof(*ps1_library_games));
 visible_count=ps1_library_ngames;visible_borrowed=true;
}
// A later CUE/M3U can hide a file already appended to an earlier publication.
// Drop it only at the publication boundary, with covers held and no yield.
static void remove_referenced_discs(void)
{
 for(int i=0;i<ps1_library_ngames;) {
  game_t *g=&ps1_library_games[i];
  for(int d=0;d<g->ndiscs;) {
   char key[PATH_LEN],*k=key;ps1_library_norm_path("",g->discs[d],key,sizeof(key));
   if(!ps1_library_nrefs || !bsearch(&k,ps1_library_refs,ps1_library_nrefs,sizeof(ps1_library_refs[0]),ps1_library_cmp_ref)){d++;continue;}
   free(g->discs[d]);
   for(int e=d+1;e<g->ndiscs;e++){g->discs[e-1]=g->discs[e];g->disc_no[e-1]=g->disc_no[e];}
   g->ndiscs--;
  }
  if(g->ndiscs){i++;continue;}
  ps1_library_game_free(g);memmove(g,g+1,(ps1_library_ngames-i-1)*sizeof(*g));ps1_library_ngames--;
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
  } else r->valid=ps1_library_image_at(r->path,0);
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
 for(int i=ps1_cursor,limit=kind==1?ps1_candidates:ps1_library_ncands;i<limit && ps1_io.count<8;i++) {
  char next[PATH_LEN];
  if(kind==1){if(!bb_index_ps1_get(i,next,sizeof(next)))continue;const char *e=ps1_library_ext_of(next);if(strcasecmp(e,".cue") && strcasecmp(e,".m3u"))continue;}
  else {
   if(ps1_library_cands[i].probed)continue;
   snprintf(next,sizeof(next),"%s",ps1_library_cands[i].path);
   char key[PATH_LEN],*k=key;ps1_library_norm_path("",next,key,sizeof(key));
   if(ps1_library_nrefs && bsearch(&k,ps1_library_refs,ps1_library_nrefs,sizeof(ps1_library_refs[0]),ps1_library_cmp_ref))continue;
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
  game_t *old=ps1_library_games;int count=ps1_library_ngames;ps1_library_games=visible_games;ps1_library_ngames=visible_count;
  save_cache(lib_root);ps1_library_games=old;ps1_library_ngames=count;
 }
 ps1_needs_settle=false;retire_published_builder();ps1_library_scan_free();ps1_noted=0;
}
bool library_poll_index(void)
{
    bb_index_service();
    int io_state=__atomic_load_n(&ps1_io.state,__ATOMIC_ACQUIRE);
    if(io_state==1)return false;
    if(io_state==3){candidate_done();ps1_library_scan_free();library_free();ps1_phase=0;ps1_force=true;}
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
    if (!ps1_covers_held) {if (!covers_library_try_begin()) return false; ps1_covers_held=true;}   // never waits for the covers' worker
    if (ps1_phase && ps1_build_generation != generation) {
        candidate_done();
        ps1_library_scan_free(); library_free(); ps1_phase=0;
    }
    if (!ps1_phase) {
        bool append=!ps1_force && ps1_library_games && ps1_scan_generation==generation && !strcmp(lib_root,root);
        if(!append) {
            library_free();ps1_library_scan_free();ps1_noted=0;memset(&ps1_library_omit,0,sizeof(ps1_library_omit));
            ps1_scan_generation=generation;
            ps1_library_games=calloc(MAX_GAMES,sizeof(*ps1_library_games));
            if(!ps1_library_games){ps1_covers_held=false;covers_library_end();return false;}
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
            for(int i=0;i<ps1_library_ngames;i++)ps1_library_game_free(&ps1_library_games[i]);
            ps1_library_ngames=0;
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
                    const char *ext=ps1_library_ext_of(path);bool reference=!strcasecmp(ext,".cue") || !strcasecmp(ext,".m3u");
                    if(reference && !candidate_ready(path,1))return false;
                    ps1_library_refs_prepared=reference;ps1_library_refs_data=candidate_current?candidate_current->data:NULL;ps1_library_refs_size=candidate_current?candidate_current->size:0;
                    char *slash=strrchr(path,'/');
                    if (slash) {
                        *slash=0;u32 id=2166136261u;
                        for (const unsigned char *p=(const unsigned char *)path;*p;p++) id=(id^*p)*16777619u;
                        ps1_library_note_file(id,path,slash+1);
                    }
                    ps1_library_refs_prepared=false;ps1_library_refs_data=NULL;ps1_library_refs_size=0;
                    if(reference)candidate_consume();
                }
                ps1_noted=++ps1_cursor;
            } else {
                if(ps1_library_nrefs) qsort(ps1_library_refs,ps1_library_nrefs,sizeof(ps1_library_refs[0]),ps1_library_cmp_ref);
                ps1_phase=2;ps1_cursor=0;
            }
        } else if (ps1_cursor < ps1_library_ncands) {
            cand_t *c=&ps1_library_cands[ps1_cursor];char key[PATH_LEN],*k=key;
            ps1_library_norm_path("",c->path,key,sizeof(key));
            if (!ps1_library_nrefs || !bsearch(&k,ps1_library_refs,ps1_library_nrefs,sizeof(ps1_library_refs[0]),ps1_library_cmp_ref)) {
                if(!c->probed) {
                    if(!candidate_ready(c->path,2))return false;
                    c->valid=candidate_current->valid;c->probed=true;candidate_consume();
                }
                if(c->valid && !c->added){ps1_library_add_disc(c->dir_id,c->path);c->added=true;}
            }
            ps1_cursor++;
        } else {
            candidate_done();
            if(!reserve_visible())return false;
            remove_referenced_discs();
            for(int i=0;i<ps1_library_ngames;i++) {
                ps1_library_sort_discs(&ps1_library_games[i]);ps1_library_fix_article(&ps1_library_games[i]);
            }
            ps1_library_disambiguate();qsort(ps1_library_games,ps1_library_ngames,sizeof(ps1_library_games[0]),ps1_library_cmp_games);
            bool settled=!bb_index_busy() && generation==bb_index_generation() &&
                ps1_candidates==bb_index_ps1_count();
            if(settled && bb_index_status()==BB_INDEX_COMPLETE) save_cache(root);
            publish_library();ps1_snapshot=generation;ps1_loaded=true;ps1_needs_settle=!settled;
            if(settled){retire_published_builder();ps1_library_scan_free();ps1_noted=0;}
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
