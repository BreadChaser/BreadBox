#!/usr/bin/env python3
"""Real Continue handoff: entry/reveal never perform filesystem/GPU work."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
home=(root/'app/ui/home.c').read_text()
entry=home[home.index('int home_run('):home.index('layout();',home.index('int home_run('))]
assert 'cont_request_refresh();' in entry
assert 'Continue after reveal' not in home
assert 'continue_pending &&' not in home
assert home.count('cont_probe();')==0 # BreadBox never probes on the menu thread
assert 'bb_last_picture_load(' not in home
assert 'bb_last_picture_read(' not in home
assert 'cont_poll()' in home
source=(root/'app/ui/home_continue.inc').read_text()
# UI capture/publication has no hidden read or inline fallback.
ui=source[source.index('static void cont_request_refresh'):]
for name in ['library_missing_file','library_launch_path','states_auto_read','bb_last_picture_load','bb_save_continue','identify(']:
 assert name not in ui,name
assert 'bb_index_defer_low(cont_probe,j)' in ui
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;typedef uint16_t u16;typedef uint64_t u64;
#define PATH_LEN 512
#define BB_PICTURE_W 256
#define BB_PICTURE_H 192
#define PSX3D_STATE_DIR "states"
#define SYSCLOCK_ARM11 1000000
static struct {char last_game_path[512],last_game_state[512],last_game[512],last_game_save[512];} g_cfg;
typedef struct {char *title,*cover_key;int ndiscs;char *discs[8];int disc_no[8];} game_t;
typedef struct {int unused;} bb_content_id;
typedef struct {void *data;bool (*resolve)(void *,char *,size_t);void (*release)(void *);} bb_path_snapshot;
typedef struct {bool legacy_continue;bool (*continue_capture)(const char *,bb_path_snapshot *);} shell_t;
typedef struct {shell_t *shell;void *save;bool (*identify)(const char *,void *,bb_content_id *,void *,int);bool (*continue_available)(bb_content_id *,u32 *);} bb_system_t;
static bool worker,cancelled,unavailable,missing,stop_missing;static int reads,uploads,deletes,launches;
static char continue_save[512],continue_title[512];
static bool cont_ok,cont_tex_ok;static u32 cont_saved;static char cont_path[512];static int cont_tex;
static void (*queued)(void *);static void *queued_arg;
typedef struct deferred_job {struct deferred_job *next;void (*fn)(void *);void *arg;} deferred_job;
static deferred_job *jobs,*low_jobs,*visible_jobs,**visible_tail=&visible_jobs;
static unsigned njobs,visible_runs,forbidden_runs;
#define LOCK() assert(worker)
#define UNLOCK() assert(worker)
#define ht_mark(...) ((void)0)
static bool bb_index_visible_checkpoint(void);
static void nested_visible(void *arg){(void)arg;visible_runs++;assert(bb_index_visible_checkpoint());}
static void visible_read(void *arg){(void)arg;visible_runs++;}
static void forbidden_job(void *arg){(void)arg;forbidden_runs++;}

static bool bb_index_cancelled(void){return cancelled;}
static u64 svcGetSystemTick(void){return 0;}
static void log_printf(const char *fmt,...){(void)fmt;}
static bool card_tex_ok,ui_wants_3d;static int card_tex,room_tv_cover;
static char room_tv_key[300];
static void aptSetHomeAllowed(bool on){(void)on;}
static void music_play(bool on){(void)on;}
static void shelf_preload_end(bool menu){assert(!menu);}
static void room_pace_priority(bool on){(void)on;}
static bool welcome_live;   /* home_welcome.inc: the room is up for bb_welcome_open */
enum {DEV_UI_NONE};static bool notes_present=true;
static void bb_whatsnew_room(int screen,bool ready,void (*present)(void (*)(void *)))
{assert(screen==DEV_UI_NONE && !ready && !present);notes_present=false;}
typedef struct {int w,h;float a,b,c,d;} Tex3DS_SubTexture;
static int cover_of(const char *p){(void)p;return -1;}
static void room_scene_tv_upload(const u16 *p,bool snow){assert(p && !snow);}
static void room_scene_tv_tex(const int *t,const Tex3DS_SubTexture *s){(void)t;(void)s;assert(0);}
static void room_scene_tv(const u16 *p){(void)p;assert(0);}
static void video_gpu_idle(void){assert(!worker);}
static void C3D_TexDelete(int *t){(void)t;assert(!worker);deletes++;}
static bool bb_index_defer_low(void (*fn)(void *),void *arg){assert(!worker);if(unavailable)return false;assert(!queued);queued=fn;queued_arg=arg;return true;}
static bool library_missing_file_cancel(const char *p,char *m,size_t n,bool (*cancel)(void *),void *arg){(void)p;(void)m;(void)n;assert(worker);reads++;if(stop_missing)cancelled=true;return cancel(arg)||missing;}
static bool resolve(void *g,char *p,size_t n){(void)g;assert(worker);launches++;snprintf(p,n,"new.cue");missing=false;return true;}
static bool capture(const char *title,bb_path_snapshot *out){assert(!worker && !strcmp(title,"Moved"));*out=(bb_path_snapshot){.resolve=resolve};return true;}
static shell_t shell={.legacy_continue=true,.continue_capture=capture};static bb_system_t adapter={.shell=&shell};
static const bb_system_t *bb_system_for_path(const char *p){(void)p;return &adapter;}
static bool states_auto_read(const char *p,u32 *saved,u16 *px,bool *thumb,bool (*cancel)(void *),void *arg){assert(worker && !cancel(arg));assert(!strcmp(p,"states/key"));reads++;*saved=42;*thumb=px!=NULL;if(px)*px=123;return true;}
static bool room_scene_picture_upload(int *t,const u16 *px){(void)t;assert(!worker && *px==123);uploads++;return true;}
static bool bb_save_continue_read(const bb_system_t *s,const bb_content_id *id,u32 *saved,bool (*cancel)(void *),void *arg){(void)cancel;(void)arg;(void)s;(void)id;(void)saved;assert(worker);return false;}
static bool bb_last_picture_read(const char *p,u16 *px,bool (*cancel)(void *),void *arg){assert(!cancel(arg));(void)p;assert(worker);reads++;if(px)*px=123;return true;}
static bool room_scene_tv_bake(const u16 *p,u16 *t,u16 *c){assert(worker && p && t && c);t[0]=321;c[0]=123;return true;}
static void run(void){assert(queued);void (*fn)(void *)=queued;queued=NULL;worker=true;fn(queued_arg);worker=false;}
'''
main=r'''
int main(void){
 strcpy(g_cfg.last_game_path,"old.cue");strcpy(g_cfg.last_game_state,"key");strcpy(g_cfg.last_game,"Moved");
 unavailable=true;cont_request_refresh();for(int i=0;i<120;i++)assert(!cont_poll());assert(!reads && !uploads && !queued);
 unavailable=false;assert(!cont_poll() && queued);for(int i=0;i<120;i++)assert(!cont_poll());assert(!reads); // entry and whole reveal
 // cont_probe's real cancellation checkpoint may nest a visible TV/kept read,
 // but queued regular and low metadata work must remain untouched.
 jobs=calloc(1,sizeof(*jobs));low_jobs=calloc(1,sizeof(*low_jobs));
 jobs->fn=low_jobs->fn=forbidden_job;
 visible_jobs=calloc(1,sizeof(*visible_jobs));visible_jobs->next=calloc(1,sizeof(*visible_jobs));
 visible_jobs->fn=nested_visible;visible_jobs->next->fn=visible_read;
 visible_tail=&visible_jobs->next->next;njobs=4;
 missing=true;run();assert(visible_runs==2 && !forbidden_runs && njobs==2 && jobs && low_jobs && !visible_jobs);
assert(!cont_ok && !uploads && launches==1);assert(cont_poll());assert(cont_ok && cont_tex_ok && cont_saved==42 && !strcmp(cont_path,"new.cue"));assert(uploads==1);
 u16 *cached_card=continue_card,*cached_tv=continue_tv;int old_reads=reads;
 leave();assert(continue_card==cached_card && continue_tv==cached_tv && !cont_tex_ok && !notes_present);
 room_tv();assert(card_tex_ok && reads==old_reads); // first reentry picture, no worker result
 cont_request_refresh();cont_poll();cont_request_refresh();run();assert(!cont_poll() && queued && cont_ok && uploads==2); // obsolete snapshot discarded
 run();assert(cont_poll() && uploads==3 && deletes==1);
 cont_request_refresh();cont_poll();int before=reads;cancelled=true;run();assert(!cont_poll() && reads==before && cont_ok); // shutdown, no SD
 cancelled=false;cont_request_refresh();cont_poll();stop_missing=true;before=launches;run();assert(!cont_poll() && launches==before && cont_ok); // cancellation between operations
 u16 *last_picture=continue_pixels;
 assert(home_release_cache() && continue_pixels==last_picture && continue_picture && !continue_tv && !continue_card && !continue_work);
 assert(continue_pixels[0]==123);free(continue_pixels);continue_pixels=NULL;free(jobs);free(low_jobs);
 puts("Continue: async entry/reveal, last snapshot retained through gameplay, moved path, stale result, unavailable worker and cancellation PASS");
}
'''
with tempfile.TemporaryDirectory() as td:
 index=(root/'app/ui/bb_index.c').read_text()
 a=index.index('bool bb_index_visible_checkpoint(void)');b=index.index('/* At most one job',a)
 checkpoint=index[a:b]
 helper=home[home.index('static void cont_capture_prefix('):home.index('// The game the main menu would continue:')]
 p=Path(td);(p/'test.c').write_text(prefix+checkpoint+helper+source+home[home.index("void leave(void)"):home.index("// as the library does before a game")]+home[home.index("static void room_tv(void)"):home.index("static void room_tv_poll(")]+main)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-format-truncation','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
