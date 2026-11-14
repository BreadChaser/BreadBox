// Host test of the game library scanner: runs the real systems/ps1/library.c
// (and safefile.c) on fixture folders built in a temp directory.
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
static u64 osGetTime(void) {static u64 tick;return ++tick;}
#define SYSCLOCK_ARM11 268111856ULL
static u64 svcGetSystemTick(void) {static u64 tick;tick+=SYSCLOCK_ARM11/1000;return tick;}   /* 1 ms a call, as osGetTime above */
void log_printf(const char *fmt, ...)
{
	if (!getenv("LIBRARY_TEST_LOG"))
		return;
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

#include <string.h>
#include "theme.h"
#include "app_paths.h"
#include "app_log.h"
#include "app_config.h"
#include "platform_video.h"
#include "app_input.h"
#include "ui_pause.h"
#include "ui.h"
#include "library.h"
#include "covers.h"
#include "bb_index.h"
#include "bb_save.h"
#include <string.h>
#include "devtools.h"
psx3d_config g_cfg;
static int path_reads,probe_reads;
static const char *paths[64];static int npaths;static unsigned gen=1;static bool busy;
int bb_index_folder_hint(const char *name) {return strstr(name,"PlayStation") || !strcmp(name,"ps1") ? 1:BB_SYSTEM_UNKNOWN;}
int bb_index_ps1_count(void) {return npaths;}
int bb_index_gba_count(void) {return 315;}
static int gba_total=315;
int bb_system_total(void) {return npaths+gba_total;}
void bb_system_counts(char *out,size_t cap) {snprintf(out,cap,"%d PS1 + 315 GBA",npaths); }
bool bb_index_ps1_get(int i,char *p,size_t n) {path_reads++;if(i<0||i>=npaths)return false;snprintf(p,n,"%s",paths[i]);return true;}
unsigned bb_index_generation(void) {return gen;}
bool bb_index_busy(void) {return busy;}
void bb_index_service(void) {}
static bool cancelling;
static bb_index_status_t terminal=BB_INDEX_COMPLETE;
static u32 keys;static int input_polls, cancel_at;

bool bb_index_cancelled(void){return cancelling;}
bb_index_status_t bb_index_status(void){return busy?BB_INDEX_SCANNING:terminal;}
bool bb_index_complete(void) {return !busy && terminal==BB_INDEX_COMPLETE;}
const char *bb_index_root(void) {return g_cfg.games_dir;}
void bb_index_start(const char *r,bool refresh) {(void)r;if(refresh)gen++;}
bool bb_disc_is_ps1_chd(const char *p) {(void)p;return false;}
bool aptMainLoop(void) {static int turns;assert(++turns<10000);return true;}
bool aptShouldClose(void) {return false;}
void dev_tick_keys(void) {}
void input_poll(void) {if(++input_polls==cancel_at)keys=KEY_B;}
u32 input_keys_down(void) {u32 k=keys;keys=0;return k;}
void ui_swap_enter(void) {}
u32 ui_swap_keys(u32 keys,u32 mask) {(void)mask;return keys;}
void ui_begin(void) {}
void ui_menu_header(const char *a,const char *b) {(void)a;(void)b;}
void ui_text_wrap(float x,float y,float scale,u32 c,float w,const char *s) {(void)x;(void)y;(void)scale;(void)c;(void)w;(void)s;}
void video_present_vsync(bool g,void (*t)(void *),void (*b)(void *),void *u) {(void)g;(void)t;(void)b;(void)u;}
const theme_t *theme(void) {static theme_t t;return &t;}
void C2D_DrawRectSolid(float x,float y,float z,float w,float h,u32 c) {(void)x;(void)y;(void)z;(void)w;(void)h;(void)c;}
static unsigned cover_changes;
void covers_library_begin(void) { assert(!cover_changes); cover_changes++; }
bool covers_library_try_begin(void) { covers_library_begin(); return true; }
void covers_library_end(void) { assert(cover_changes==1); cover_changes--; }

int bb_later_state(bb_later_file *r) {return r->state;}
static bool hold_cache;
static int grow_on_probe;
static void (*cache_job)(void *);static void *cache_arg;
static void check_prefetch(void (*fn)(void *));
bool bb_index_defer(void (*fn)(void *),void *arg) {
 check_prefetch(fn);
 if(hold_cache){assert(!cache_job);cache_job=fn;cache_arg=arg;}else fn(arg);
 return true;
}

static int staging_allocations;
static void *library_calloc(size_t n,size_t bytes)
{
 if(n==2048 && bytes==sizeof(game_t))staging_allocations++;
 return calloc(n,bytes);
}
#define calloc library_calloc
#include "../../systems/ps1/library.c"
#include "../common/test_tmpdir.h"
#undef calloc
static char *rows_text(void)
{
 char *text=NULL;size_t size=0;FILE *f=open_memstream(&text,&size);assert(f);
 for(int i=0;i<library_count();i++) {
  const game_t *g=library_get(i);fprintf(f,"%s\t%s",g->title,g->cover_key);
  for(int d=0;d<g->ndiscs;d++)fprintf(f,"\t%s",g->discs[d]);
  fputc('\n',f);
 }
 fclose(f);return text;
}
static char *cache_text(void)
{
 FILE *f=fopen(LIB_CACHE,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>=0);rewind(f);
 char *text=malloc(size+1);assert(text && fread(text,1,size,f)==(size_t)size);text[size]=0;fclose(f);return text;
}
static void check_prefetch(void (*fn)(void *))
{
 if(fn!=candidate_read)return;
 probe_reads+=ps1_io.count;
 for(int i=0;i<ps1_io.count;i++)if(ps1_io.entries[i].kind==2){
  char key[PATH_LEN],*k=key;norm_path("",ps1_io.entries[i].path,key,sizeof(key));
  assert(!nrefs || !bsearch(&k,refs,nrefs,sizeof(refs[0]),cmp_ref));
  if(grow_on_probe){npaths=grow_on_probe;grow_on_probe=0;busy=false;}
 }
}
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


#define CHECK(c) do {if(!(c)) {fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(void) {
 char tmp[4096];CHECK(test_mkdtemp(tmp, sizeof(tmp), "bb-library"));CHECK(!chdir(tmp));
 mkdirs(PSX3D_DIR);
 const char *root="sdmc:/roms";
 strcpy(g_cfg.games_dir,root);
 const char *dir="(1994) Sony PlayStation - V1.0";
 (void)dir;
 put(root,"(1994) Sony PlayStation - V1.0/Crash/Crash Bandicoot (USA).bin","x");
 put(root,"(1994) Sony PlayStation - V1.0/Crash/Crash Bandicoot (USA).cue","FILE \"Crash Bandicoot (USA).bin\" BINARY\n");
 put(root,"(1994) Sony PlayStation - V1.0/Crash/Crash Bandicoot 2 (USA).bin","x");
 put(root,"(1994) Sony PlayStation - V1.0/FF/Final Fantasy VII (USA) (Disc 1).bin","x");
 put(root,"(1994) Sony PlayStation - V1.0/FF/Final Fantasy VII (USA) (Disc 2).bin","x");
 put(root,"(2001) Nintendo Game Boy Advance - V1.2/BIOS/gba_bios.bin","junk");
 // Index paths only: an unrelated valid PS1 disc is deliberately NOT included.
 put(root,"not-in-index.bin","x");
 const char *snapshot[]={
 "sdmc:/roms/(1994) Sony PlayStation - V1.0/Crash/Crash Bandicoot (USA).bin",
 "sdmc:/roms/(1994) Sony PlayStation - V1.0/Crash/Crash Bandicoot (USA).cue",
 "sdmc:/roms/(1994) Sony PlayStation - V1.0/Crash/Crash Bandicoot 2 (USA).bin",
 "sdmc:/roms/(1994) Sony PlayStation - V1.0/FF/Final Fantasy VII (USA) (Disc 1).bin",
 "sdmc:/roms/(1994) Sony PlayStation - V1.0/FF/Final Fantasy VII (USA) (Disc 2).bin",
 "sdmc:/roms/(2001) Nintendo Game Boy Advance - V1.2/BIOS/gba_bios.bin"};
 memcpy(paths,snapshot,sizeof(snapshot));npaths=6;
 busy=true;CHECK(!library_poll_index());CHECK(!library_count());
 busy=false;CHECK(!library_poll_index());CHECK(library_index_busy());CHECK(cover_changes==1);
 // Replace an index generation mid-import; the workers stay drained through
 // cancellation, partial-list frees, restart, and final sorting.
 gen++;CHECK(!library_poll_index());CHECK(cover_changes==1);
 while(library_index_busy()) {library_poll_index();}
 CHECK(library_count()==3);CHECK(!cover_changes);
 CHECK(!strcmp(library_get(0)->title,"Crash Bandicoot"));
 CHECK(!strcmp(library_get(1)->title,"Crash Bandicoot 2"));
 CHECK(library_get(2)->ndiscs==2);
 CHECK(strstr(library_get(0)->cover_key,"Crash Bandicoot"));
 CHECK(!library_poll_index());
 // A replaced snapshot reuses the matching presentation cache.
 hold_cache=true;gen++;CHECK(!library_poll_index());
 CHECK(library_index_busy() && library_count()==3 && !cover_changes && cache_job);
 cache_job(cache_arg);cache_job=NULL;hold_cache=false;
 CHECK(library_poll_index());CHECK(library_count()==3);CHECK(!cover_changes);
 // Cache with changed candidates MUST be invalidated.
 npaths=2;gen++;hold_cache=true;CHECK(!library_poll_index());CHECK(cache_job);
 cache_job(cache_arg);cache_job=NULL;
 CHECK(!library_poll_index());CHECK(cache_job && library_index_busy() && library_count()==3);
 // A slow CUE read stays on the worker even when its generation is cancelled.
 gen++;CHECK(!library_poll_index());CHECK(cover_changes==1);
 cache_job(cache_arg);cache_job=NULL;hold_cache=false;
 while(library_index_busy()) {library_poll_index();}
 CHECK(library_count()==1);
 CHECK(library_load(root,true));CHECK(library_count()==1);
 // A cancelled batched disc job retains public rows and retries all candidates.
 npaths=6;gen++;hold_cache=true;CHECK(!library_poll_index());CHECK(cache_job);
 cache_job(cache_arg);cache_job=NULL;
 for(int turns=0;!cache_job && turns<20;turns++)library_poll_index();
 CHECK(cache_job && library_count()==1 && ps1_io.count>0 && ps1_io.count<=8);
 cancelling=true;cache_job(cache_arg);cache_job=NULL;cancelling=false;
 CHECK(ps1_io.state==3);hold_cache=false;
 while(library_index_busy())library_poll_index();
 CHECK(library_count()==3 && !cover_changes);
 // First-run import publishes while the raw directory walk is still busy,
 // then imports appended candidates even though its generation did not change.
 busy=true;npaths=2;gen++;ps1_force=true;ps1_loaded=false;
 bool published=false;
 for(int i=0;i<200 && !published;i++)published=library_poll_index();
 CHECK(published && library_count()==1 && !bb_index_complete() && ps1_noted==2);
 int previous_probes=probe_reads,allocations=staging_allocations;
 game_t *builder=games;const char *first_title=library_get(0)->title;
 npaths=6;published=false;
 for(int i=0;i<200 && !published;i++)published=library_poll_index();
 CHECK(published && library_count()==3 && !bb_index_complete());
 CHECK(games==builder && staging_allocations==allocations); // one builder for all publications
 bool same_title=false;for(int i=0;i<library_count();i++)same_title|=library_get(i)->title==first_title;
 CHECK(same_title); // existing strings/discs were retained, not re-added
 CHECK(probe_reads-previous_probes==4); // only newly appended files, no reread of CUE/prefix
 int settled_probes=probe_reads,settled_reads=path_reads;
 busy=false;
 CHECK(!library_poll_index()); // settlement saves the visible rows directly
 for(int i=0;i<200 && library_index_busy();i++)library_poll_index();
 CHECK(!library_index_busy() && library_count()==3);
 CHECK(probe_reads==settled_probes && path_reads<=settled_reads+npaths && ps1_noted==0); // settle cache without another pass
 // Settlement must persist the PUBLIC rows, and load_cache must round-trip
 // those exact titles/keys/discs without probes (not empty retired staging).
 char *settled_rows=rows_text();int saved_probes=probe_reads;
 gen++;ps1_loaded=false;published=false;
 for(int i=0;i<200 && !published;i++)published=library_poll_index();
 CHECK(published && probe_reads==saved_probes);
 char *loaded_rows=rows_text();CHECK(!strcmp(settled_rows,loaded_rows));free(settled_rows);free(loaded_rows);
 // LIMITED with visible candidates must never bless/overwrite that cache,
 // including the busy->terminal settlement path.
 char *before_limited=cache_text();terminal=BB_INDEX_LIMITED;busy=true;npaths=2;gen++;ps1_force=true;ps1_loaded=false;
 published=false;for(int i=0;i<200 && !published;i++)published=library_poll_index();
 CHECK(published && library_count()==1);busy=false;CHECK(!library_poll_index());
 char *after_limited=cache_text();CHECK(!strcmp(before_limited,after_limited));free(before_limited);free(after_limited);
 terminal=BB_INDEX_COMPLETE;
 // A reference arriving later hides its previously visible raw track safely.
 busy=true;npaths=1;gen++;ps1_force=true;ps1_loaded=false;published=false;
 for(int i=0;i<200 && !published;i++)published=library_poll_index();
 CHECK(published && library_count()==1 && strstr(library_get(0)->discs[0],".bin"));
 builder=games;allocations=staging_allocations;npaths=2;published=false;
 for(int i=0;i<200 && !published;i++)published=library_poll_index();
 CHECK(published && library_count()==1 && strstr(library_get(0)->discs[0],".cue"));
 CHECK(games==builder && staging_allocations==allocations);
 // A generation change can discard newly owned rows before publication,
 // while preserving the last public prefix and its strings.
 add_disc(1,paths[2]);CHECK(ngames>visible_count);
 library_free();CHECK(library_count()==1 && strstr(library_get(0)->discs[0],".cue"));
 busy=false;library_poll_index();
 // The raw walk can finish with extra candidates during the last probe.
 // Do not bless/cache the smaller import with the larger signature.
 busy=true;npaths=2;gen++;ps1_force=true;ps1_loaded=false;grow_on_probe=6;
 published=false;
 for(int i=0;i<200 && !published;i++)published=library_poll_index();
 CHECK(published && !grow_on_probe && !busy && library_count()==1 && library_index_busy());
 for(int i=0;i<200 && library_index_busy();i++)library_poll_index();
 CHECK(!library_index_busy() && library_count()==3);
 npaths=0;gen++;ps1_force=true;
 for(int i=0;i<200 && library_index_busy();i++)library_poll_index();
 CHECK(!library_index_busy() && library_count()==0 && !visible_games);
 // A GBA-only folder is loaded through both the enum and compatibility APIs.
 CHECK(library_load_result(root,false)==LIBRARY_LOADED);CHECK(library_load(root,false));
 gba_total=0;
 // LIMITED and ERROR zero-game scans are terminal even without a usable raw snapshot.
 terminal=BB_INDEX_LIMITED;npaths=0;gen++;ps1_loaded=false;ps1_force=true;
 CHECK(library_load_result(root,false)==LIBRARY_EMPTY);CHECK(!library_index_busy());
 terminal=BB_INDEX_ERROR;gen++;ps1_loaded=false;ps1_force=true;
 CHECK(library_load_result(root,false)==LIBRARY_EMPTY);CHECK(!library_index_busy());
 // B cancels the raw-index wait and does not fall into a second wait.
 busy=true;keys=KEY_B;int polls=input_polls;
 CHECK(library_load_result(root,false)==LIBRARY_CANCELLED);CHECK(input_polls==polls+1);
 // B also cancels presentation progress after a usable raw cache arrives.
 busy=false;terminal=BB_INDEX_COMPLETE;gen++;ps1_force=true;cancel_at=input_polls+1;
 CHECK(!library_load(root,false));CHECK(input_polls==cancel_at);
 cancel_at=0;
 // A long wait remains cancellable and explains what B does after two seconds.
 busy=true;cancel_at=input_polls+2100;
 CHECK(library_load_result(root,false)==LIBRARY_CANCELLED && index_wait_slow);
 busy=false;cancel_at=0;
 library_free();scan_free();
 puts("Shared-index PS1 import, owner root, CUE track hiding, clean titles, discs, series, covers, cache invalidation: PASS");
 return 0;
}

void ui_progress_draw(const char *a,const char *b,int n){(void)a;(void)b;(void)n;}
void ui_bar_draw(const ui_button_t *b,int n){(void)b;(void)n;}
