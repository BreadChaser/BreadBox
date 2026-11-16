#!/usr/bin/env python3
"""Execute the production main menu loop with nested picker/scan close returns.
The actual loop is extracted so missing outer guards cannot be hidden by a fake driver.
GPU setup is represented by Home/library entry counters, not actual GPU submission.
"""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'app/ui/main.c').read_text()
assert s.count('if (MAIN_STOP()) break;') == 13
assert s.count('#define MAIN_STOP() (!aptMainLoop() || !gspHasGpuRight())') == 1
a=s.index('#define MAIN_STOP()'); b=s.index('\n    if(bb_update_committed())',a)
loop=s[a:b]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define PATH_LEN 512
#define APP_NAME "BreadBox"
enum { HOME_QUIT, HOME_AGAIN, HOME_FOLDERS, HOME_LIBRARY };
typedef struct { char title[64],cover_key[64]; } game_t;
typedef struct { bool resume; char save[512],title[512]; } home_launch_t;
static struct {char games_dir[512],last_title[64];} g_cfg;
static char sw_game[64],launch_error[64];
static int scenario, entries, reloads, browsers, scans;
static bool closed, gpu;
static int startup_mode;
static bool aptMainLoop(void){return !closed;}
static bool gspHasGpuRight(void){return gpu;}
static bool aptShouldClose(void){return closed;}
static void dev_tick(bool b){(void)b;}
static bool dev_exiting(void){return false;}
static bool dev_next_game(char *p,char *s,char *t){(void)p;(void)s;(void)t;return false;}
static void dev_local_game(void){}
static bool dev_remote_game(void){return false;}
static bool dev_transition(void){return false;}
static bool start_game(const char *p,const char *s,const char *t){(void)p;(void)s;(void)t;assert(!closed&&gpu);assert(0);return false;}
static void show_error(const char *s){(void)s;assert(!closed&&gpu);}
static int library_count(void){return 0;}
static const game_t *library_get(int i){(void)i;return NULL;}
static bool library_launch_path(const game_t *g,char *p,size_t n){(void)g;(void)p;(void)n;return false;}
#define log_printf(...) ((void)0)
#define strcasestr(a,b) strstr(a,b)
static void bb_system_resume(const char *p,bool r){(void)p;(void)r;assert(!closed&&gpu);}
static void bb_index_poll(bool r){(void)r;assert(!closed&&gpu);}
static int bb_system_total(void){return 0;}
static bool bb_games_dir_gone;
static void end_nested(void){if(scenario>=8)gpu=false;else closed=true;}
static bool library_load(const char *r,bool b){(void)r;(void)b;assert(!closed&&gpu);reloads++;end_nested();return scenario%2==0;}
static int home_run(bool l,char *p,size_t n,home_launch_t *h){(void)l;(void)p;(void)n;(void)h;assert(!closed&&gpu);assert(++entries<3);return scenario==0||scenario==1||scenario==8?HOME_FOLDERS:HOME_LIBRARY;}
static bool browser_run(char *p,size_t n){(void)p;(void)n;assert(!closed&&gpu);browsers++;end_nested();return scenario%2==0;}
static int breadbox_systems_run(char *p,size_t n){(void)p;(void)n;assert(!closed&&gpu);scans++;if(scenario==2||scenario==3||scenario==9)return 2;end_nested();return 4;}
static int home_empty_library_run(void){assert(!closed&&gpu);scans++;end_nested();return scenario%2==0?3:2;}
static bool bb_update_committed(void){return false;}
static void run(void){char path[PATH_LEN]; bool use_library=scenario<4||scenario==8||scenario==9;
'''
suffix=r'''
}
int main(int argc,char **argv){assert(argc==2);startup_mode=atoi(argv[1]);for(scenario=0;scenario<10;scenario++){
 closed=false;gpu=true;entries=reloads=browsers=scans=0;
 // Enter either Home, the populated library, or first-run empty UI directly.
 strcpy(g_cfg.games_dir,scenario==4||scenario==5||scenario==6||scenario==7?"":"sdmc:/games");
 run();assert(closed||!gpu);
 }puts("main picker/scan close chain: PASS");}
'''
# Starting at Home is production's default. For remaining cases enter the library/empty branch.
loop=loop.replace('bool at_home = true;', 'bool at_home = scenario==0||scenario==1||scenario==8;')
# Automatic migration lifecycle and worker ownership execute in tests/psx3d_import/ui.c.
startup=(root/'app/ui/main.c').read_text()
assert startup.count('breadbox_import_init();')==1
assert startup.index('breadbox_import_init();')<startup.index('playlog_init();')
assert startup.index('breadbox_import_init();')<startup.index('covers_init();')
with tempfile.TemporaryDirectory(prefix='main-lifecycle-') as d:
 p=Path(d);(p/'check.c').write_text(prefix+loop+suffix)
 subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(p/'check.c'),'-o',str(p/'check')],check=True)
 for mode in ('0','1','2'):subprocess.run([str(p/'check'),mode],check=True)

# Execute the actual successful-update exit branch, with an ordered allowlist of
# services. An added asset/font/config call has no fake and fails compilation.
# Each step is bounded and journaled; at the first one that times out no further step runs
# (nothing the leftover thread can reach is freed: a live reader keeps ROMFS mounted), the
# handoff is unclean and the process ends through exit_terminal.
a=s.index('    if(bb_update_committed()) {')
# Find the block's matching brace; every following statement belongs to normal exit.
b=s.index('{',a)+1;depth=1
while depth:
 if s[b]=='{':depth+=1
 elif s[b]=='}':depth-=1
 b+=1
committed_end=b
branch=s[a:b]
import itertools
# Every step can time out (0) or finish (1); (installer, covers, network, reads, audio, gpu).
cases=[(1,1,1,1,1,1)]+[tuple(int(i!=k) for i in range(6)) for k in range(6)]
with tempfile.TemporaryDirectory(prefix='update-shutdown-') as d:
 p=Path(d)
 (p/'check.c').write_text(r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
typedef uint64_t u64;
#define BB_UPDATE_JOIN_NS 5000000000ULL
enum {BB_UP_INSTALLER_JOIN=10,BB_UP_COVERS_EXIT,BB_UP_DEVNET_EXIT,BB_UP_ROMFS_EXIT,BB_UP_CHAINLOAD,BB_UP_HOME_RETURN,BB_UP_AUDIO_EXIT=18,BB_UP_GPU_EXIT};
static int ok[6],may_relaunch;static char calls[512];
static void call(const char *s){strcat(calls,s);strcat(calls," ");}
static void exit_terminal(void){call("terminal");}
static bool bb_update_committed(void){return true;}
static bool bb_update_shutdown_timeout(uint64_t t){assert(t==BB_UPDATE_JOIN_NS);call("installer");return ok[0];}
static bool covers_network_exit(u64 t){assert(t==BB_UPDATE_JOIN_NS);call("covers");return ok[1];}
static bool dev_shutdown_timeout(uint64_t t){assert(t==BB_UPDATE_JOIN_NS);call("devnet");return ok[2];}
static bool uisound_stop_reads(u64 t){assert(t==BB_UPDATE_JOIN_NS);call("reads");return ok[3];}
static void uisound_exit(void){}
static void gfxExit(void){}
typedef struct { void (*fn)(void); } exit_step_t;
static const exit_step_t exit_audio={uisound_exit},exit_gpu={gfxExit};
static bool exit_step(const exit_step_t *step,u64 t){assert(t==BB_UPDATE_JOIN_NS);
 if(step==&exit_audio){call("sound");return ok[4];}
 assert(step==&exit_gpu);call("gfx");return ok[5];}
static void romfsExit(void){call("romfs");}
static void aptClearChainloader(void){call("clear");}
static void bb_update_record(unsigned p,uint32_t r){char s[32];sprintf(s,"%u=%u",p,(unsigned)r);call(s);}
static bool bb_update_handoff(bool clean){call(clean?"handoff-clean":"handoff-unclean");return clean && may_relaunch;}
static int shutdown_path(void){
"""+branch+r"""
return 1;
}
int main(int argc,char **argv){
 assert(argc==9);for(int i=0;i<6;i++)ok[i]=atoi(argv[i+1]);may_relaunch=atoi(argv[7]);
 assert(shutdown_path()==0);
 if(strcmp(calls,argv[8])){fprintf(stderr,"got  %s\nwant %s\n",calls,argv[8]);return 1;}
 return 0;}
""")
 subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(p/'check.c'),'-o',str(p/'check')],check=True)
 for (inst,cov,net,reads,audio,gpu),may in itertools.product(cases,(1,0)):
  want=f"installer 10={inst} ";clean=inst
  if clean:want+=f"covers 11={cov} ";clean=cov
  if clean:want+=f"devnet 12={net} ";clean=net
  if clean:
   want+="reads "+("sound " if reads else "");clean=int(bool(reads and audio));want+=f"18={clean} "
  if clean:want+="romfs 13=1 "
  want+=f"handoff-{'clean' if clean else 'unclean'} "
  relaunch=clean and may
  if clean:
   want+=f"gfx 19={gpu} "
   if relaunch and not gpu:want+="clear 15=0 "
   clean=gpu
  if not clean:want+="terminal "
  subprocess.run([str(p/'check'),*map(str,(inst,cov,net,reads,audio,gpu,may)),want],check=True)
print('update exit: first timed-out step stops the teardown, HOME, exit_terminal (14 combinations) PASS')

def extract(path,start,end):
    t=(root/path).read_text();a=t.index(start);return t[a:t.index(end,a)]

# S2: the real exit_step / exit_step_main under a scheduling fake. The audio helper's entry is
# delayed past its join timeout, the GPU helper then runs, then the first one is let in. Each
# helper must run the step it was given (round 1's shared pointer gave audio=0, gfx=2).
step_src=extract('app/ui/main.c','typedef struct { void (*fn)(void); } exit_step_t;','\nint main(int argc')
assert 'exit_step_fn' not in s
with tempfile.TemporaryDirectory(prefix='exit-step-') as d:
 p=Path(d)
 (p/'step.c').write_text(r'''
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
typedef uint64_t u64;typedef int32_t s32;
#define PSX3D_COLD
#define CUR_THREAD_HANDLE 0
struct Thread_tag{pthread_t t;void (*fn)(void *);void *arg;bool delayed;volatile int done;};
typedef struct Thread_tag *Thread;
static volatile int audio,gfx,delay_next,gate;static Thread created[4];static int created_n;
static void uisound_exit(void){__atomic_add_fetch(&audio,1,__ATOMIC_SEQ_CST);}
static void gfxExit(void){__atomic_add_fetch(&gfx,1,__ATOMIC_SEQ_CST);}
static void svcGetThreadPriority(s32 *p,int h){(void)h;*p=0x30;}
static void *entry(void *v){Thread t=v;if(t->delayed)while(!__atomic_load_n(&gate,__ATOMIC_ACQUIRE))usleep(1000);
 t->fn(t->arg);__atomic_store_n(&t->done,1,__ATOMIC_RELEASE);return NULL;}
static Thread threadCreate(void (*fn)(void *),void *arg,size_t stack,int prio,int core,bool detached)
{(void)stack;(void)prio;(void)core;assert(!detached);Thread t=calloc(1,sizeof(*t));t->fn=fn;t->arg=arg;t->delayed=delay_next;delay_next=0;created[created_n++]=t;
 assert(!pthread_create(&t->t,NULL,entry,t));return t;}
static int threadJoin(Thread t,u64 timeout){assert(timeout==5000000000ULL);
 for(int i=0;i<200;i++){if(__atomic_load_n(&t->done,__ATOMIC_ACQUIRE))return 0;usleep(1000);}return 0x09401BFE;} /* timeout: not R_FAILED */
static void threadFree(Thread t){pthread_join(t->t,NULL);free(t);}
'''+step_src+r'''
int main(void){
 delay_next=1;
 assert(!exit_step(&exit_audio,5000000000ULL));   /* the helper has not even started */
 assert(exit_step(&exit_gpu,5000000000ULL));
 assert(audio==0 && gfx==1);
 __atomic_store_n(&gate,1,__ATOMIC_RELEASE);
 for(int i=0;i<2000 && !audio;i++)usleep(1000);
 printf("audio=%d gfx=%d\n",audio,gfx);
 assert(audio==1 && gfx==1);
 threadFree(created[0]);   /* production keeps a timed-out helper for the kernel to end */
 return 0;}
''')
 subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-g',str(p/'step.c'),'-o',str(p/'step'),'-pthread'],check=True)
 subprocess.run([str(p/'step')],check=True)
print('S2: each exit helper runs its own step after a delayed entry (audio=1, gfx=1) PASS')

# The real exit_terminal: CIA ends the process the way libctru 2.7.0's exit ends last
# (APT_PrepareToCloseApplication, APT_CloseApplication, svcExitProcess) with nothing in
# between and the socket service closed first; it never returns. A 3dsx returns (documented).
term_src=extract('app/ui/main.c','PSX3D_COLD static void exit_terminal(void)','// A teardown that libctru cannot bound')
body=term_src.replace('socExit()','').replace('svcExitProcess()','')
for banned in ('free(','Exit()','romfs','gfx','ndsp','threadJoin','__ctru_heap','aptExit','aptClearChainloader','aptSetChainloader'):
 assert banned not in body,banned
with tempfile.TemporaryDirectory(prefix='exit-terminal-') as d:
 p=Path(d)
 (p/'term.c').write_text(r'''
#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define PSX3D_COLD
typedef uint32_t Handle;
static char calls[256];static jmp_buf ended;static bool homebrew;
static void call(const char *s){strcat(calls,s);strcat(calls," ");}
#define log_printf(...) call("log")
static void log_soon(void){call("soon");}
static void svcSleepThread(long long ns){assert(ns>0 && ns<=100000000LL);call("sleep");}
static bool envIsHomebrew(void){return homebrew;}
static int socExit(void){call("soc");return 0;}
static int APT_PrepareToCloseApplication(bool cancel){assert(cancel);call("prepare-close");return 0;}
static int APT_CloseApplication(const void *p,size_t n,Handle h){assert(!p && !n && !h);call("close");return 0;}
static void svcExitProcess(void){call("exit-process");longjmp(ended,1);}
'''+term_src+r'''
int main(void){
 if(!setjmp(ended)){exit_terminal();assert(!"returned on a CIA");}
 assert(!strcmp(calls,"log soon sleep soc prepare-close close exit-process "));
 calls[0]=0;homebrew=true;
 if(!setjmp(ended)){exit_terminal();assert(!strcmp(calls,"log soon sleep "));}
 else assert(!"3dsx must return to its loader");
 return 0;}
''')
 subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra','-Werror','-g',str(p/'term.c'),'-o',str(p/'term')],check=True)
 subprocess.run([str(p/'term')],check=True)
print('exit_terminal: CIA closes SOC, APT close, svcExitProcess, never returns; 3dsx returns PASS')

# The real normal quit / HOME Close teardown, run against fakes that model what a thread
# left running still uses. Scenarios: clean; the index worker never stops; a PS1 game closed
# from the HOME Menu (its core still runs); a cover worker does not stop; an importer cleanup
# stranded on a stopped index worker (retry_cleanup restarts the worker: it must be drained
# before the final index shutdown, and no worker may start after it).
normal=s[committed_end:s.index('\n}\n\n#include "components/main.inc"')]
assert normal.count('exit_terminal();')==2 and 'exit_keep_heap' not in s and 'exit_heap_decide' not in s
with tempfile.TemporaryDirectory(prefix='normal-exit-') as d:
 p=Path(d)
 (p/'normal.c').write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
typedef uint64_t u64;
enum {BB_SAVES_EXIT=2};
static char calls[1024];static int scenario;static bool gpu=true,closing,abandoned,index_closed,index_worker,index_started_late;
static bool retained;static int index_shutdowns,stranded;static u64 clock_ms;static bool sw_nocovers,sw_noirrst;
static void call(const char *s){strcat(calls,s);strcat(calls," ");}
/* What a thread left running still uses: freeing it under that thread is the bug. */
static void touch_check(const char *what){if(abandoned){fprintf(stderr,"%s freed under a live thread\n",what);abort();}}
#define log_printf(...) ((void)0)
static void bb_update_shutdown(void){assert(scenario!=5);call("update");}
static bool gspHasGpuRight(void){return gpu;}
static bool aptShouldClose(void){return closing;}
static void log_resume(void){call("log-resume");}
static void breadbox_import_shutdown(void){call("import");}
static bool bb_index_background_start(void){if(index_closed)return false;index_worker=true;if(index_shutdowns)index_started_late=true;return true;}
static void covers_stop_wait(void){call("covers-stop");if(stranded){stranded=0;assert(bb_index_background_start());}}
static void bb_index_shutdown(void){call("index-stop");index_shutdowns++;if(scenario!=1)index_worker=false;}
static void bb_index_close(void){call("index-close");index_closed=true;}
static bool bb_index_shutdown_done(void){return !index_worker;}
static u64 osGetTime(void){return clock_ms;}
static void svcSleepThread(long long ns){clock_ms+=(u64)(ns/1000000);}
static void bb_exit_abandon(void){call("abandon");abandoned=true;}
static bool bb_exit_abandoned(void){return abandoned;}
static int bb_platform_saves(int m){assert(m==BB_SAVES_EXIT);call("saves");if(retained){call("retry");retained=false;}return 0;}
static void config_save(void){assert(!retained);call("config");}
static void exit_terminal(void){call("terminal");}
static void dev_shutdown(void){touch_check("devnet");call("devnet");}
static void uisound_exit(void){touch_check("sound");call("sound");}
static bool covers_exit(void){call("covers-exit");if(scenario==3){abandoned=true;return false;}return true;}
static void libui_exit(void){touch_check("library");call("libui");}
static void romfsExit(void){touch_check("romfs");call("romfs");}
static void bb_system_deinit(void){touch_check("systems");call("systems");}
static void ui_exit(void){touch_check("ui");call("ui");}
static void video_exit(void){touch_check("video");call("video");}
static void irrstExit(void){call("irrst");}
static void cfguExit(void){call("cfgu");}
static void log_close(void){touch_check("log");call("log-close");}
static int normal_exit(void){
'''+normal+r'''
}
int main(int argc,char **argv){
 assert(argc==3);scenario=atoi(argv[1]);
 index_worker=true;
 if(scenario==2){gpu=false;closing=true;abandoned=true;}   /* PS1 HOME > Close: sys_ps1's run() abandoned the core */
 if(scenario==4){index_worker=false;stranded=1;}          /* a cleanup waits for a stopped worker */
 if(scenario==5){abandoned=true;retained=true;} /* updater deadline with a failed final save */
 assert(normal_exit()==0);
 if(strcmp(calls,argv[2])){fprintf(stderr,"got  %s\nwant %s\n",calls,argv[2]);return 1;}
 assert((index_closed || abandoned) && !index_started_late);
 if(scenario==1)assert(clock_ms>3000);
 return 0;}
''')
 subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-g',str(p/'normal.c'),'-o',str(p/'normal')],check=True)
 clean="update import covers-stop index-stop index-close saves devnet config sound covers-exit libui romfs systems ui video irrst cfgu log-close "
 for sc,want in ((0,clean),
                 (1,"update import covers-stop index-stop index-close abandon saves config terminal "),
                 (2,"log-resume saves config terminal "),
                 (3,"update import covers-stop index-stop index-close saves devnet config sound covers-exit terminal "),
                 (4,clean),
                 (5,"saves retry config terminal ")):
  subprocess.run([str(p/'normal'),str(sc),want],check=True)
print('normal quit / HOME Close: index/cover/PS1-core leftovers free nothing and end in exit_terminal; no index worker after the final shutdown PASS')
startup_fail=s[s.index('\tif (!video_init()) {'):s.index('\tui_init();')]
assert 'exit_terminal();' in startup_fail and 'return 1;' in startup_fail

# Covers: the real covers_exit / covers_network_exit. A worker that does not stop keeps
# everything it can reach (pixels, thumbnails, logos, curl, the sockets); a later call that
# joins it frees all of it once.
cx=extract('app/ui/covers.c','bool covers_network_exit(u64 timeout)','// The small copy of a game')
with tempfile.TemporaryDirectory(prefix='covers-exit-') as d:
 p=Path(d)
 (p/'covers.c').write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define WORKERS 2
#define SLOTS 3
#define TSLOTS 2
#define BB_MAX_SYSTEMS 2
typedef uint64_t u64;typedef struct Thread_tag *Thread;typedef struct {int id;} C3D_Tex;
static struct Thread_tag {int id;} w[WORKERS];
static Thread workers[WORKERS];static void *video_pre_draw=(void*)1;static int apt_cookie,covers_wake;
static bool covers_quit,initialized=true,soc_started=true;static int join_ok[WORKERS];
static struct {void *pixels;bool tex_ok;C3D_Tex tex;} covers_slots[SLOTS];
static struct {bool tex_ok;C3D_Tex tex;} covers_tslots[TSLOTS];
static struct {void *px;int n;} covers_logos[BB_MAX_SYSTEMS];
static void *covers_own_pngs,*covers_thumb_tried,*covers_curl_easy,*covers_curl_share;static int dummy;
static int released,texes,covers_thumbs,nets,flushes,misses,curls,socs,unhooks,thread_frees;
#define free(p) do{if(p)released++;}while(0)
#define log_printf(...) ((void)0)
static void aptUnhook(int *c){(void)c;unhooks++;}
static void LightEvent_Signal(int *e){(void)e;}
static int threadJoin(Thread t,u64 ns){(void)ns;return join_ok[t->id]?0:0x09401BFE;}
static void threadFree(Thread t){(void)t;thread_frees++;}
static void C3D_TexDelete(C3D_Tex *t){(void)t;texes++;}
static void covers_thumbs_free(void){covers_thumbs++;}
static void covers_net_close(void){nets++;}
static void covers_state_flush(void){flushes++;}
static void boxart_misses_free(void){misses++;}
static void curl_easy_cleanup(void *c){(void)c;curls++;}
static void curl_share_cleanup(void *c){(void)c;curls++;}
static void curl_global_cleanup(void){curls++;}
static void socExit(void){socs++;}
static void arm(void){
 for(int i=0;i<WORKERS;i++){w[i].id=i;workers[i]=&w[i];join_ok[i]=1;}
 for(int i=0;i<SLOTS;i++){covers_slots[i].pixels=&dummy;covers_slots[i].tex_ok=true;}
 for(int i=0;i<TSLOTS;i++)covers_tslots[i].tex_ok=true;
 for(int i=0;i<BB_MAX_SYSTEMS;i++)covers_logos[i].px=&dummy;
 covers_own_pngs=covers_thumb_tried=covers_curl_easy=covers_curl_share=&dummy;soc_started=true;
 released=texes=covers_thumbs=nets=flushes=misses=curls=socs=thread_frees=0;}
#define UNTOUCHED (!released && !texes && !covers_thumbs && !nets && !flushes && !misses && !curls && !socs && covers_curl_easy && covers_curl_share && soc_started && covers_own_pngs)
'''+cx+r'''
int main(void){
 arm();join_ok[1]=0;
 assert(!covers_exit() && covers_quit && UNTOUCHED && workers[1] && !workers[0] && thread_frees==1);
 join_ok[1]=1;
 assert(covers_exit() && !workers[1] && thread_frees==2);
 assert(released==SLOTS+BB_MAX_SYSTEMS+2 && texes==SLOTS+TSLOTS && covers_thumbs==1 && nets==1 && flushes==1 && curls==3 && socs==1 && !soc_started);
 arm();join_ok[0]=0;covers_quit=false;
 assert(!covers_network_exit(5000000000ULL) && covers_quit && UNTOUCHED && workers[0]);
 join_ok[0]=1;
 assert(covers_network_exit(5000000000ULL) && !workers[0] && nets==1 && curls==3 && socs==1 && !released);
 return 0;}
''')
 subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-misleading-indentation','-fsanitize=address,undefined','-g',str(p/'covers.c'),'-o',str(p/'covers')],check=True)
 subprocess.run([str(p/'covers')],check=True)
worker=(root/'app/services/covers_fetch.c').read_text()
worker=worker[worker.index('void covers_worker_main(void *arg)'):]
assert 'while (covers_paused && !covers_quit)\n\t\t\t\tsvcSleepThread(100000000LL);\n\t\t\tif (covers_quit)\n\t\t\t\tbreak;' in worker
print('covers exits: a timed-out worker keeps pixels, curl and sockets; the join that succeeds frees them once PASS')

# PS1 HOME > Close: the real adapter run() and the real session end. run_game returns true
# without the GPU right and with its core, CD/dynarec/GPU/SPU and rewind threads still running;
# nothing they use (the cards' copies, the system's memory loan) may be released after it.
ps1_run=extract('systems/ps1/sys_ps1.c','static ADAPTER bb_run_result run(','static ADAPTER bool state(')
sess=extract('app/ui/bb_session.c','static bool abandoned;','void bb_session_set_stereo')
with tempfile.TemporaryDirectory(prefix='ps1-home-close-') as d:
 p=Path(d)
 (p/'ps1.c').write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define ADAPTER
typedef enum { BB_RUN_QUIT, BB_RUN_FAILED } bb_run_result;
typedef struct {const char *path,*save_name,*title;int slot;} bb_launch;
typedef struct {int id;} bb_system_t;
static const bb_system_t ps1={1};static const bb_system_t *active=&ps1;static bool bb_session_stereo=true;
static bool gpu,core_threads,ended_ok=true;static int sw_state,cards_ends,drops,restores;
static bool bb_exec_held(void){return true;}
static const bb_system_t *bb_session_system(void){return active;}
static const bb_system_t *bb_system_by_id(const char *id){assert(!strcmp(id,"ps1"));return &ps1;}
static bool gspHasGpuRight(void){return gpu;}
static const char *emu_load_error(void){return "error";}
/* emu.c's run_game: without the GPU right it skips rewind_stop/retro_deinit (core threads live). */
static bool emu_run_game(const char *p,const char *s,const char *t){(void)p;(void)s;(void)t;core_threads=!gpu;return ended_ok;}
static void cards_end(void){assert(!core_threads);cards_ends++;}
static void bb_platform_drop(void){assert(!core_threads);drops++;}
static void bb_platform_restore_menu(void){assert(!core_threads);restores++;}
'''+sess+ps1_run+r'''
int main(int argc,char **argv){
 assert(argc==2);gpu=atoi(argv[1]);
 char error[64];bb_launch l={"sdmc:/roms/game.cue",NULL,"Game",-1};
 assert(run(&l,error,sizeof(error))==BB_RUN_QUIT);
 bb_session_end();
 assert(!active && !bb_session_stereo);
 if(gpu)assert(!bb_exit_abandoned() && cards_ends==1 && drops==1 && restores==1);
 else assert(bb_exit_abandoned() && !cards_ends && !drops && !restores);
 return 0;}
''')
 subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-g',str(p/'ps1.c'),'-o',str(p/'ps1')],check=True)
 for g in ('1','0'):subprocess.run([str(p/'ps1'),g],check=True)
emu=(root/'systems/ps1/emu.c').read_text()
close=emu[emu.index('\tif (!gspHasGpuRight()) {\n\t\tlog_resume();\n\t\taudio_exit();'):emu.index('\thome_save_drop();\n\tautosave_wait();\n\tlog_printf("game: leaving\\n");')]
assert 'core_initialized = false;' in close and 'return true;' in close
for torn in ('rewind_stop','retro_deinit','retro_unload_game','video_game_end'):assert torn not in close   # no renderer/core teardown without the GPU
print('PS1 HOME Close: core left running -> abandoned, no card/loan release, exit_terminal at exit PASS')
home=(root/'app/ui/home.c').read_text()
assert 'if(bb_update_tick())return HOME_QUIT;' in home
assert 'if(bb_update_committed())return HOME_QUIT;' in home
assert home.count('if(bb_update_installing() || bb_update_committed()){bb_update_install_frame();continue;}')==2
update=(root/'app/services/update.c').read_text()
tick=update[update.index('bool bb_update_tick(void)'):update.index('void bb_update_draw_notice')]
assert tick.index('if(bb_update_committed())return true;',tick.index('if(bb_update_installing())return false;'))<tick.index('if(update_thread)')
print('update minimal shutdown: bounded journaled teardown (covers, devnet, sound), the handoff decision, then the GPU; a timeout ends in exit_terminal (HOME), no title assets PASS')

# Menu release must retire importer handles while its worker can still run.
platform=(root/'platform/3ds/bb_platform.c').read_text()
release=platform[platform.index('void bb_platform_release_menu(void)'):platform.index('void bb_platform_restore_menu(void)')]
assert release.index('covers_stop_wait();')<release.index('bb_index_shutdown();')
print('menu release drains importer before index shutdown PASS')

# Bounded joins on the install/teardown path: run the real functions against fakes. A join
# that times out must leave the thread, the log file, the socket and every buffer alone
# (nothing is freed or closed under a running thread), and a later call must finish the job.
log_fn=extract('app/ui/log.c','bool log_close_timeout(uint64_t timeout)','void log_close(void)')
net_fn=extract('platform/3ds/devnet.c','bool devnet_exit_timeout(uint64_t timeout)','void devnet_exit(void)')
with tempfile.TemporaryDirectory(prefix='bounded-join-') as d:
 p=Path(d)
 (p/'log.c').write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef int Thread;
static Thread writer_thread=7,*writer=&writer_thread;
static bool writer_stop;static int ring_wake,signals,joins,frees,logs_now,closes,socks;
static unsigned log_quiesced;static FILE *log_fp;static int link_fd=-1;
static unsigned join_result=1;
static int threadJoin(Thread *t,uint64_t timeout){(void)t;assert(timeout==1234);joins++;return join_result;}
static void threadFree(Thread *t){(void)t;frees++;}
static void LightEvent_Signal(int *e){(void)e;signals++;}
static void log_now(void){logs_now++;}
static int closesocket(int fd){(void)fd;closes++;return 0;}
static void socExit(void){socks++;}
'''+log_fn+r'''
int main(void){
 log_fp=tmpfile();assert(log_fp);link_fd=5;
 assert(!log_close_timeout(1234));
 assert(writer && !writer_stop && !log_quiesced && log_fp && !frees && !closes && !socks && link_fd==5); /* untouched, running */
 join_result=0;
 assert(log_close_timeout(1234));
 assert(!writer && frees==1 && logs_now==1 && !log_fp && link_fd==-1 && closes==1 && socks==1 && log_quiesced==1 && joins==2);
 assert(log_close_timeout(1234) && joins==2); /* nothing left to join: idempotent */
 return 0;}
''')
 subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra','-Werror',str(p/'log.c'),'-o',str(p/'log')],check=True)
 subprocess.run([str(p/'log')],check=True)
 (p/'net.c').write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
typedef int Thread;
typedef int LightLock;
static Thread worker_thread=3,*worker=&worker_thread;
static int server=9,mailbox=0,lock,joins,frees,closes,socks,sleeps;static bool stopping,soc_owned=true;
static void *soc_buffer,*reply,*shot_pixels,*history;
static unsigned join_result=1;
static void LightLock_Lock(LightLock *l){(void)l;}static void LightLock_Unlock(LightLock *l){(void)l;}
static void svcSleepThread(long long n){(void)n;sleeps++;}
static int threadJoin(Thread *t,uint64_t timeout){(void)t;assert(timeout==99);joins++;return join_result;}
static void threadFree(Thread *t){(void)t;frees++;}
static int closesocket(int fd){(void)fd;closes++;return 0;}
static void socExit(void){socks++;}
static int released;
#define free(p) do{if(p)released++;}while(0)
'''+net_fn+r'''
int main(void){
 soc_buffer=&lock;reply=&lock;shot_pixels=&lock;history=&lock;
 assert(!devnet_exit_timeout(99));
 assert(stopping && worker && !closes && !socks && !released && server==9 && soc_owned && soc_buffer && history); /* nothing freed under a live worker */
 join_result=0;
 assert(devnet_exit_timeout(99));
 assert(!worker && frees==1 && closes==1 && socks==1 && released==4 && server==-1 && !soc_owned && !soc_buffer && !reply && !shot_pixels && !history);
 assert(devnet_exit_timeout(99) && closes==1 && socks==1 && released==4); /* no double close or free */
 return 0;}
''')
 subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function',str(p/'net.c'),'-o',str(p/'net')],check=True)
 subprocess.run([str(p/'net')],check=True)
print('bounded log and developer-network joins: timeout leaves everything intact, retry finishes PASS')

# RC5c F3: the real ui_begin must not publish the library index while the updater installs or
# has committed (the index worker is shut down there), and must keep publishing otherwise.
ui_begin_fn=extract('app/ui/ui.c','void ui_begin(void)','void ui_text(')
with tempfile.TemporaryDirectory(prefix='ui-begin-') as d:
 p=Path(d)
 (p/'ui.c').write_text(r'''
#include <assert.h>
#include <stdbool.h>
static int cleared,imports,publishes;static void *textbuf;static bool session,installing,committed;
static void C2D_TextBufClear(void *b){(void)b;cleared++;}
static void breadbox_import_poll(void){imports++;}
static bool bb_session_system(void){return session;}
static bool bb_update_installing(void){return installing;}
static bool bb_update_committed(void){return committed;}
static void bb_index_publish(void){publishes++;}
'''+ui_begin_fn+r'''
int main(void){
 ui_begin();assert(cleared==1 && imports==1 && publishes==1);
 session=true;ui_begin();assert(publishes==1 && imports==2);session=false;
 installing=true;ui_begin();assert(publishes==1 && imports==3);installing=false;
 committed=true;ui_begin();assert(publishes==1 && imports==4);committed=false;
 ui_begin();assert(publishes==2);
 return 0;}
''')
 subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra','-Werror',str(p/'ui.c'),'-o',str(p/'ui')],check=True)
 subprocess.run([str(p/'ui')],check=True)
print('RC5c F3: ui_begin publishes only outside install/commit and sessions PASS')
