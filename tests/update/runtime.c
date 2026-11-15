/* Production staging/AM code with fake console services, real curl/SHA. */
#include <string.h>
#include <sys/types.h>
#include "app_log.h"
#include "app_config.h"
#include "platform_video.h"
#include "dev_switches.h"
#include "app_input.h"
#include "ui_list.h"
#include "ui_pause.h"
#include "ui.h"
#include "covers.h"
#include "uisound.h"
#include "playlog.h"
#include "app_main.h"
#include "bb_update.h"
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include "bb_writers.h"
typedef struct {pthread_t thread;void *stack;size_t size;} *Thread;
static size_t host_stack_high_water;
#define U64_MAX UINT64_MAX
typedef int aptHookCookie;
typedef int APT_HookType;
#define APTHOOK_ONWAKEUP 1
#define APTHOOK_ONRESTORE 2
#define APTHOOK_ONSUSPEND 3
#define APTHOOK_ONSLEEP 4
#define APTHOOK_ONEXIT 5
static void aptHook(aptHookCookie *c,void (*fn)(APT_HookType,void *),void *a){(void)c;(void)fn;(void)a;}
static unsigned join_timeouts,join_calls;
static int threadJoin(Thread t,u64 timeout){join_calls++;if(join_timeouts){join_timeouts--;return 1;} /* a timeout is a nonzero, non-failure code */
 (void)timeout;pthread_join(t->thread,NULL);unsigned char *p=t->stack;size_t n=0;while(n<t->size && p[n]==0xa5)n++;if(t->size-n>host_stack_high_water)host_stack_high_water=t->size-n;return 0;}
static void threadFree(Thread t){free(t->stack);free(t);}
static void (*thread_fn)(void *);
static void *thread_arg;
static void *thread_entry(void *arg){(void)arg;thread_fn(thread_arg);return NULL;}
static Thread threadCreate(void (*fn)(void *),void *arg,size_t stack,int prio,int core,bool detached)
{
 (void)prio;(void)core;(void)detached;Thread t=calloc(1,sizeof(*t));assert(t);
 t->size=stack;assert(!posix_memalign(&t->stack,4096,stack));memset(t->stack,0xa5,stack);
 pthread_attr_t attr;assert(!pthread_attr_init(&attr));assert(!pthread_attr_setstack(&attr,t->stack,stack));
 thread_fn=fn;thread_arg=arg;int rc=pthread_create(&t->thread,&attr,thread_entry,NULL);pthread_attr_destroy(&attr);
 if(rc){free(t->stack);free(t);return NULL;}return t;
}

static void abort_install(void);
static unsigned cancel_stop,cancel_index,cancel_save,cancel_log;
static bool importer_drained,index_pending,retire_after_cancel;static unsigned retire_polls;
static bool abandoned;void bb_exit_abandon(void){abandoned=true;}
bool bb_exit_abandoned(void){return abandoned;}
void covers_stop_wait(void){importer_drained=true;}
static void bb_index_shutdown(void){assert(importer_drained);importer_drained=false;}
bool bb_index_shutdown_done(void){if(cancel_index && !--cancel_index)abort_install();if(retire_after_cancel && !cancel_index && ++retire_polls>=3)index_pending=false;return !index_pending;}
static unsigned restart_scans;
const char *bb_index_root(void){return "sdmc:/roms";}
void bb_index_start(const char *root,bool refresh){assert(!strcmp(root,"sdmc:/roms"));(void)refresh;restart_scans++;}
static void dev_shutdown(void){}
bool sw_nosound,g_new3ds;
void uisound_exit(void){}
void uisound_init(void){}
void covers_init(void){}
bool covers_exit(void){return true;}
static unsigned log_close_calls;static bool log_stops=true;
bool log_close_timeout(uint64_t t){(void)t;log_close_calls++;if(cancel_log && !--cancel_log)abort_install();return log_stops;}
void log_close(void){}
void log_resume(void){}
void log_writer_start(int core){(void)core;}
static unsigned progress_frames,gpu_drains;
static bool gpu_busy;static unsigned gpu_polls;
bool video_gpu_poll_idle(void){assert(!bb_update_committed());gpu_polls++;if(gpu_busy)return false;gpu_drains++;return true;}
void music_play(bool on){(void)on;}
void ui_progress_card_draw(const char *t,const char *m,int percent){assert(!bb_update_committed());progress_frames++;(void)t;(void)m;(void)percent;}
typedef int Result;
typedef unsigned Handle;
typedef struct {u64 titleID;u16 version;} AM_TitleInfo;
typedef struct {u32 sectorSize,clusterSize,totalClusters,freeClusters;} FS_ArchiveResource;
typedef const char *FS_Path;
#define R_FAILED(x) ((x)<0)
#define R_SUCCEEDED(x) ((x)>=0)
#define MEDIATYPE_SD 1
#define ARCHIVE_SDMC 1
#define PATH_EMPTY 0
#define PATH_ASCII 1
#define FS_OPEN_READ 1
static bool game,closed,no_gpu,homebrew,stop_ok=true,save_ok=true,config_ok=true,playlog_ok=true;
static unsigned free_clusters=100000,installed=16400,candidate=16401;
static u64 self=BB_UPDATE_TITLE,cia_title=BB_UPDATE_TITLE;
static unsigned fail_at,starts,cancels,finishes,am_count,read_count,chainloads;
static FILE *source_file;
static int operation;
static bool fail(void){return ++operation==(int)fail_at;}
bool aptMainLoop(void){return !closed;}
bool aptShouldClose(void){return closed;}
bool gspHasGpuRight(void){return !no_gpu;}
bool envIsHomebrew(void){return homebrew;}
Result APT_GetProgramID(u64 *out){*out=self;return fail()?-1:0;}
Result amInit(void){am_count++;return fail()?-1:0;}
void amExit(void){}
Result AM_GetTitleInfo(int media,unsigned n,u64 *id,AM_TitleInfo *info){(void)media;(void)n;assert(*id==BB_UPDATE_TITLE);info->version=installed;return fail()?-1:0;}
static bool adapter=true;static u8 battery=100;static bool telemetry_ok=true;
Result ptmuInit(void){return telemetry_ok?0:-1;}void ptmuExit(void){}
Result PTMU_GetAdapterState(bool *out){*out=adapter;return 0;}
Result mcuHwcInit(void){return telemetry_ok?0:-1;}void mcuHwcExit(void){}
Result MCUHWC_GetBatteryLevel(u8 *out){*out=battery;return 0;}
static bool pending_ours,pending_other,pending_fail,pending_delete_fail;static unsigned pending_deleted;
Result AM_GetNumPendingTitles(u32 *out,int media){(void)media;*out=pending_ours+pending_other;return pending_fail?-1:0;}
Result AM_GetPendingTitleList(u32 *out,u64 *ids,u32 count,int media){(void)media;unsigned n=0;if(pending_other)ids[n++]=BB_UPDATE_TITLE+1;if(pending_ours)ids[n++]=BB_UPDATE_TITLE;assert(n<=count);*out=n;return 0;}
Result AM_DeletePendingTitle(int media,u64 id){(void)media;assert(id==BB_UPDATE_TITLE);pending_deleted++;if(!pending_delete_fail)pending_ours=false;return pending_delete_fail?-1:0;}
Result FSUSER_GetSdmcArchiveResource(FS_ArchiveResource *r){*r=(FS_ArchiveResource){.clusterSize=4096,.freeClusters=free_clusters};return 0;}
FS_Path fsMakePath(int type,const char *path){(void)type;return path;}
Result FSUSER_OpenFileDirectly(Handle *h,int archive,FS_Path root,FS_Path path,int mode,int flags){(void)archive;(void)root;(void)mode;(void)flags;if(fail())return -1;char p[512];snprintf(p,sizeof(p),"sdmc:%s",path);source_file=fopen(p,"rb");*h=source_file?1:0;return source_file?0:-1;}
Result AM_GetCiaFileInfo(int media,AM_TitleInfo *info,Handle h){(void)media;assert(h==1);*info=(AM_TitleInfo){cia_title,candidate};return fail()?-1:0;}
Result AM_StartCiaInstall(int media,Handle *h){(void)media;starts++;if(fail())return -1;*h=2;return 0;}
Result FSFILE_Read(Handle h,u32 *n,u64 off,void *buf,u32 count){assert(h==1);read_count++;if(fail())return -1;fseek(source_file,off,SEEK_SET);*n=fread(buf,1,count,source_file);return 0;}
Result FSFILE_Write(Handle h,u32 *n,u64 off,const void *buf,u32 count,int flags){(void)off;(void)buf;(void)flags;assert(h==2);if(fail())return -1;*n=count;return 0;}
Result AM_FinishCiaInstall(Handle h){assert(h==2);finishes++;return fail()?-1:0;}
Result AM_CancelCIAInstall(Handle h){assert(h==2);cancels++;return 0;}
void FSFILE_Close(Handle h){assert(h==1);fclose(source_file);source_file=NULL;}
void aptSetChainloaderToSelf(void){chainloads++;}
static unsigned chainload_title,chainload_clears;void aptSetChainloader(u64 id,u8 media){assert(id==BB_UPDATE_TITLE && media==MEDIATYPE_SD);chainload_title++;}
void aptClearChainloader(void){chainload_clears++;}
bool sw_nonet,sw_nocovers;
/* A one-shot hook run at the first call inside update_poll (forced in-poll interleaving, re-check 2026-11-05). */
static void (*session_hook)(void);
bool bb_session_active(void){if(session_hook){void (*h)(void)=session_hook;session_hook=NULL;h();}return game;}
bool bb_index_cancelled(void){return false;}
static bool index_busy,index_ready;
bool bb_index_busy(void){return index_busy;}
bool bb_index_complete(void){return index_ready;}
bool bb_index_visible_checkpoint(void){return false;}
static u64 fake_time=100;static bool sleep_advances;
/* sleep_advances: the fake clock jumps, but real time passes too so the other thread is scheduled */
void svcSleepThread(long long n){if(sleep_advances){fake_time+=(u64)(n/1000000);usleep(100);}else usleep(1000);}
u64 osGetTime(void){return fake_time;}
#define SYSCLOCK_ARM11 268123480
u64 svcGetSystemTick(void){return fake_time*(SYSCLOCK_ARM11/1000);}
static unsigned stop_calls;
bool covers_stop(void){stop_calls++;if(cancel_stop && !--cancel_stop)abort_install();if(retire_after_cancel && !cancel_stop && ++retire_polls>=3)stop_ok=true;if(stop_ok)importer_drained=true;return stop_ok;}
void covers_resume(void){}
void playlog_flush(void){}
bool playlog_pending(void){return !playlog_ok;}
static bool save_settles=true;
bool bb_save_settle_timeout(uint64_t t){(void)t;if(cancel_save && !--cancel_save)abort_install();return save_settles;}
void bb_save_settle(void){}
bool bb_save_flush(void){return save_ok;}
bool bb_save_final_failed(void){return !save_ok;}
void config_save(void){}
bool config_last_save_ok(void){return config_ok;}
bool dev_covers_blocked(void){return false;}
static bool diagnostic;
bool dev_bench;
bool dev_enabled(void){return diagnostic;}
/* The confirm dialog: answer, count, and what the UI would show while it waits (never "Downloading"). */
static bool confirm_answer=true;static unsigned confirms;static bool confirm_saw_progress;
static bool update_progress_text(char *text,size_t cap);
bool ui_confirm_choice(const char *a,const char *b,const char *c,const char *d,bool e){(void)a;(void)b;(void)c;(void)d;(void)e;
 confirms++;char t[48];if(update_progress_text(t,sizeof(t)))confirm_saw_progress=true;return confirm_answer;}
static char notice_seen[64];void ui_notice(const char *s,int frames){(void)frames;snprintf(notice_seen,sizeof(notice_seen),"%s",s);}
/* footer taps: the shared bar input maps a tap on tab i to its key (tap_tab -1: no tap) */
static int tap_tab=-1;
u32 ui_bar_input(const ui_button_t *b,int n,u32 k){if((k&KEY_TOUCH) && tap_tab>=0 && tap_tab<n)return (k&~KEY_TOUCH)|b[tap_tab].key;return k;}
bool bb_index_background_start(void){return true;}
static bool accept_queue;static unsigned queued;
static void (*queued_fn)(void *);
bool bb_index_defer_low(void (*fn)(void *),void *arg){(void)arg;if(accept_queue){queued++;queued_fn=fn;}return accept_queue;}
#include <stdarg.h>
static unsigned journal_logs;
void log_printf(const char *fmt,...){if(strstr(fmt,"previous install journal"))journal_logs++;}
static u32 keys[8];static unsigned key_index;
bool dev_tick(bool playing){(void)playing;return false;}
int dev_ui_pending(void){return 0;}
static u32 held;u32 input_keys_held(void){return held;}
void input_poll(void){}
u32 input_keys_down(void){assert(key_index<8);return keys[key_index++];}
void ui_swap_enter(void){}
static int sfx_errors;
void sfx_play(int e){if(e==SFX_ERROR)sfx_errors++;}
void sfx_cancel(void){}
u32 ui_swap_keys(u32 k,u32 mask){(void)mask;return k;}
u32 ui_card_input(const flist_t *f,int n,int *sel,u32 k){(void)f;if(k&KEY_DOWN)*sel=(*sel+1)%n;return k;}
void ui_card_update(flist_t *f,int sel,int n){(void)f;(void)sel;(void)n;}
void music_update(void){}
void ui_begin(void){}
void ui_menu_top(void *ud){(void)ud;}
void ui_card_notice_calm(void){}
void ui_card_draw(const flist_t *f,int n,int sel,void (*row)(int,void *,ui_card_row_t *),void *ud,const ui_card_t *card){(void)f;(void)n;(void)sel;(void)row;(void)ud;(void)card;}
void ui_bar_draw(const ui_button_t *b,int n){(void)b;(void)n;}
void video_present_vsync(bool game,void (*top)(void *),void (*bottom)(void *),void *ud){(void)game;top(ud);bottom(ud);assert(!bb_update_committed());}
/* Route the fixed dev discovery origin to the ephemeral local fixture port. */
#include <curl/curl.h>
static struct curl_slist *discovery_connect;
static CURL *test_curl_init(void)
{CURL *c=curl_easy_init();if(c && discovery_connect){curl_easy_setopt(c,CURLOPT_CONNECT_TO,discovery_connect);curl_easy_setopt(c,CURLOPT_PROXY,"");}return c;}
#ifdef BB_UPDATE_RELEASE_TEST
/* Observe the options passed by production, preserving their values for real TLS. */
static unsigned transport_options;
static char transport_request[2048];
static void test_curl_option(CURLoption option,uintptr_t value)
{
 switch(option){
 case CURLOPT_URL:snprintf(transport_request,sizeof(transport_request),"%s",(const char *)value);transport_options|=1;break;
 case CURLOPT_SSL_VERIFYPEER:assert(value==1);transport_options|=2;break;
 case CURLOPT_SSL_VERIFYHOST:assert(value==2);transport_options|=4;break;
 case CURLOPT_CAINFO:assert(!strcmp((const char *)value,"romfs:/certs.pem"));transport_options|=8;break;
 case CURLOPT_PROTOCOLS_STR:assert(!strcmp((const char *)value,"https"));transport_options|=16;break;
 case CURLOPT_REDIR_PROTOCOLS_STR:assert(!strcmp((const char *)value,"https"));transport_options|=32;break;
 case CURLOPT_FOLLOWLOCATION:assert(value==1);transport_options|=64;break;
 case CURLOPT_MAXREDIRS:assert(value==5);transport_options|=128;break;
 default:break;
 }
}
#undef curl_easy_setopt
#define curl_easy_setopt(c,o,v) (test_curl_option((o),(uintptr_t)(v)),curl_easy_setopt((c),(o),(v)))
static CURLMcode test_curl_add(CURLM *m,CURL *c)
{assert(transport_options==255);transport_options=0;return curl_multi_add_handle(m,c);}
#define curl_multi_add_handle test_curl_add
#endif
#define curl_easy_init test_curl_init
#include "../../app/services/update.c"
static void abort_install(void){__atomic_store_n(&update_install_abort,1,__ATOMIC_RELEASE);}
#undef curl_easy_init
#ifdef BB_UPDATE_RELEASE_TEST
#undef curl_easy_setopt
#undef curl_multi_add_handle
#include "release.inc"
#endif
FILE *__real_fopen(const char *,const char *);
FILE *__wrap_fopen(const char *path,const char *mode)
{assert(!bb_update_committed() || strncmp(path,"romfs:",6));return __real_fopen(path,mode);}
static void stage(void)
{
 FILE *f=fopen(STAGE,"wb");assert(f);for(int i=0;i<9000;i++)fputc('Z',f);fclose(f);
 update_manifest=(bb_update_manifest){.version=16401,.size=9000};strcpy(update_manifest.display,"1.0.1");
 strcpy(update_manifest.sha256,"d8d28122647767c99b3d1fcbbc76c20caadec40e56f1cc4ad6eff89305258153");
 update_install_abort=0;operation=0;starts=cancels=finishes=am_count=read_count=0;update_commit=0;update_finish_allowed=1;update_finish_ready=0;
}
static void journal_is(const unsigned *want,unsigned n)
{
 bb_update_phase_record j[BB_UPDATE_PHASE_MAX];unsigned got=bb_update_phase_load(BB_UPDATE_PHASE_PATH,j,BB_UPDATE_PHASE_MAX);
 assert(got==n);for(unsigned i=0;i<n;i++)assert(j[i].phase==want[i]);
}
static unsigned journal_last(unsigned *result)
{
 bb_update_phase_record j[BB_UPDATE_PHASE_MAX];unsigned got=bb_update_phase_load(BB_UPDATE_PHASE_PATH,j,BB_UPDATE_PHASE_MAX);
 assert(got);if(result)*result=j[got-1].result;return j[got-1].phase;
}
static void *journal_hammer(void *unused)
{(void)unused;for(unsigned i=0;i<20;i++)bb_update_record(BB_UP_NEXT_BOOT,i);return NULL;}
static const unsigned journal_success[]={BB_UP_FLUSH,BB_UP_LOG_CLOSE,BB_UP_SECOND_SHA,BB_UP_RECEIPT,BB_UP_AM_START,BB_UP_WRITES_DONE,BB_UP_GPU_ACK,BB_UP_FINISH,BB_UP_HANDLES_CLOSED};
/* Ready, approved, idle room: the next tick starts the installer thread. */
static void arm_install(void)
{
 stage();update_checked=true;update_check_now=false;update_preference=0;update_crypto_disabled=false;update_job_busy=0;update_retry_after=0;
 update_postponed=false;update_install_attempted=false;update_machine.state=BB_UPDATE_READY;update_initialized=true;update_approved=true;
 update_install_abort=0;update_commit=0;update_power_checked=0;
}
static void complete_in_poll(void)
{
 update_machine.state=BB_UPDATE_READY;__atomic_store_n(&update_job_busy,0,__ATOMIC_RELEASE);
}
int main(int argc,char **argv)
{
#ifdef BB_UPDATE_RELEASE_TEST
 return release_transport_test(argc,argv);
#endif
 accept_queue=true;diagnostic=true;
#ifdef BB_UPDATE_TEST_REMOTE
 assert(!bb_update_tick() && queued==1);update_job_busy=0;
 dev_bench=true;assert(!bb_update_tick() && queued==1);dev_bench=false;
#else
 assert(!bb_update_tick() && !queued);
#endif
 diagnostic=false;accept_queue=false;queued=0;update_initialized=false;update_prefs_loaded=1;   /* the updater has started and read its preference */
 sink header={0};char h2[]="HTTP/2 206\r\n",h3[]="HTTP/3 503\r\n";
 assert(update_header(h2,1,sizeof(h2)-1,&header)==sizeof(h2)-1 && header.response==206);
 assert(update_header(h3,1,sizeof(h3)-1,&header)==sizeof(h3)-1 && header.response==503);
 assert(argc==2);mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir(APP_SD_ROOT,0700);mkdir(UPDATE_ROOT,0700);
 /* Every precommit drain is bounded/cancellable; no AM or worker release. */
 if(getenv("BB_DRAIN_REPRO")){
  if(!strcmp(getenv("BB_DRAIN_REPRO"),"gate")){
   arm_install();update_explicit_check=true;save_ok=false;unsigned before=stop_calls,asked=confirms;
   assert(!bb_update_tick() && !update_thread && !starts && stop_calls==before);
   assert(!strcmp(update_card_notice(),"Waiting: save not written"));
   /* Automatic READY must disclose the same blocker, even without a visit's explicit check. */
   update_explicit_check=false;assert(!strcmp(update_card_notice(),"Waiting: save not written"));
   update_explicit_check=true;save_ok=true;
   assert(update_poll(false)==UPDATE_POLL_WAIT && !update_thread && !starts);
   assert(!strcmp(update_card_notice(),"Leave Settings to install"));
   /* Resolving retained files by retry/discard leaves consent intact. Only the room may install. */
   assert(!bb_update_tick() && update_thread && confirms==asked);
   for(unsigned turn=0;bb_update_installing();turn++){assert(turn<10000);bb_update_install_frame();usleep(1000);}
   assert(bb_update_committed() && starts==1 && finishes==1 && confirms==asked);
   bb_update_shutdown();bb_writers_release_exclusive();
   puts("PASS: retained final save explains READY, Settings never installs, resolution resumes the approved update in the room without asking again");return 0;
  }
  sleep_advances=true;
  for(unsigned scenario=0;scenario<6;scenario++){
   stage();index_pending=scenario==1 || scenario==3;stop_ok=scenario!=0 && scenario!=2;
   save_settles=scenario!=4;log_stops=scenario!=5;abandoned=false;
   cancel_stop=scenario==2?2:0;cancel_index=scenario==3?2:0;
   retire_polls=0;retire_after_cancel=scenario==2 || scenario==3;
   cancel_save=scenario==4?2:0;cancel_log=scenario==5?2:0;
   update_install_busy=1;u64 start=fake_time;alarm(5);update_install_job(NULL);alarm(0);
   assert(fake_time-start<=6000 && !starts && !bb_update_installing() && !bb_writers_busy());
   if(scenario<2)assert(abandoned);
   else assert(!abandoned);
   if(scenario==2 || scenario==3)assert(!bb_update_tick());
  }
  update_install_busy=1;update_commit=0;abandoned=false;u64 start=fake_time;
  alarm(10);update_apt(APTHOOK_ONEXIT,NULL);alarm(0);
  assert(abandoned && update_install_busy && fake_time-start<=31000);
  puts("PASS: permanently pending cover/index, cancel at cover/index/save/log, APT deadline retain ownership and never enter AM");return 0;
 }
 /* Discovery needs only update.json; malformed title/URL/hash never reaches staging. */
 char connect[128];const char *port=strrchr(argv[1],':');assert(port);
 snprintf(connect,sizeof(connect),"127.0.0.1:80:127.0.0.1:%s",port+1);
 discovery_connect=curl_slist_append(NULL,connect);assert(discovery_connect);
 update_discover_job(NULL);assert(update_status==2 && update_machine.state==BB_UPDATE_DOWNLOAD && update_manifest.version==16401);
 for(unsigned i=0;i<3;i++){
  update_machine.state=BB_UPDATE_IDLE;update_discover_job(NULL);
  assert(update_status==4 && update_machine.state==BB_UPDATE_IDLE && !starts);
 }
 update_discover_job(NULL);assert(update_status==7 && update_machine.state==BB_UPDATE_IDLE);
 /* merge review: no update file (404) or a server error (503) is not Wi-Fi; no answer at all is */
 for(unsigned i=0;i<2;i++){
  update_discover_job(NULL);
  assert(update_status==16 && !strcmp(update_settings_status(),"No update info right now") && update_machine.state==BB_UPDATE_IDLE && !starts);
 }
 update_discover_job(NULL);assert(update_status==3 && !strcmp(update_settings_status(),"No connection. Check Wi-Fi") && update_machine.state==BB_UPDATE_IDLE);
 assert(update_check_failures==3);update_check_failures=0;   /* failed checks still back off the automatic ones */
 curl_slist_free_all(discovery_connect);discovery_connect=NULL;update_network_close();
 stage();game=true;assert(!update_install(NULL) && !starts);game=false;
 stage();homebrew=true;assert(!update_install(NULL) && !starts);homebrew=false;
 stage();self++;assert(!update_install(NULL) && !starts);self--;
 stage();installed=16401;assert(!update_install(NULL) && !starts);installed=16400;
 stage();free_clusters=0;assert(!update_install(NULL) && !starts);free_clusters=100000;
 stage();cia_title++;assert(!update_install(NULL) && !starts);cia_title--;
 stage();candidate++;assert(!update_install(NULL) && !starts);candidate--;
 stage();FILE *f=fopen(STAGE,"ab");assert(f);fputc(0,f);fclose(f);assert(!update_install(NULL) && !starts);
 /* Every console-service failure before/within mutation; no leaked FS handle. */
 for(fail_at=1;fail_at<=12;fail_at++){
  stage();bool ok=update_install(NULL);assert(!source_file);
  if(!ok && read_count)assert(cancels==1);
  assert(!ok);
 }
 fail_at=0;stage();assert(update_install(NULL));assert(starts==1 && finishes==1 && !cancels);assert(!access(STAGE,F_OK));assert(!access(RECEIPT,F_OK));update_commit=0;
 playlog_ok=false;assert(!update_flush(NULL));playlog_ok=true;
 config_ok=false;assert(!update_flush(NULL));config_ok=true;save_ok=false;assert(!update_flush(NULL));save_ok=true;stop_ok=false;assert(!update_flush(NULL));stop_ok=true;assert(update_flush(NULL));
 game=true;update_initialized=true;update_machine.state=BB_UPDATE_INSTALL;assert(!bb_update_tick() && !chainloads);game=false;
 /* Real curl against local Range fixture, interrupted prefix/resume, bounded size. */
 stage();remove(STAGE);snprintf(update_manifest.url,sizeof(update_manifest.url),"%s/BreadBox.cia",argv[1]);
 assert(update_download(NULL));assert(update_verify(NULL));
 f=fopen(STAGE,"wb");for(int i=0;i<200;i++)fputc('Z',f);fclose(f);assert(update_download(NULL));assert(update_verify(NULL));
 remove(STAGE);update_net_abort=1;assert(!update_download(NULL));update_net_abort=0;
 snprintf(update_manifest.url,sizeof(update_manifest.url),"%s/ignore/BreadBox.cia",argv[1]);assert(update_download(NULL));assert(update_verify(NULL));
 remove(STAGE);snprintf(update_manifest.url,sizeof(update_manifest.url),"%s/oversize/BreadBox.cia",argv[1]);assert(!update_download(NULL));
 remove(STAGE);snprintf(update_manifest.url,sizeof(update_manifest.url),"%s/truncated/BreadBox.cia",argv[1]);assert(!update_download(NULL));
 struct stat partial;assert(!stat(STAGE,&partial) && partial.st_size>0 && partial.st_size<9000);
 snprintf(update_manifest.url,sizeof(update_manifest.url),"%s/BreadBox.cia",argv[1]);assert(update_download(NULL) && update_verify(NULL));
 remove(STAGE);snprintf(update_manifest.url,sizeof(update_manifest.url),"%s/notfound/BreadBox.cia",argv[1]);assert(!update_download(NULL));
 f=fopen(STAGE,"wb");assert(f);for(unsigned i=0;i<200;i++)fputc('Z',f);fclose(f);
 for(unsigned i=0;i<3;i++){
  snprintf(update_manifest.url,sizeof(update_manifest.url),"%s/%s/BreadBox.cia",argv[1],(const char *[]){"blip","unavailable","range416"}[i]);
  assert(!update_download(NULL));assert(!stat(STAGE,&partial) && partial.st_size==200);
 }
 remove(STAGE);
 /* Bounded worker turns, then verify/ready: production state transitions. */
 update_manifest.size=600000;strcpy(update_manifest.sha256,"21ed8325d8e5cdd9057f06ecc837521881c84cb282faed778b9bf65c9d602f80");
 snprintf(update_manifest.url,sizeof(update_manifest.url),"%s/large/BreadBox.cia",argv[1]);update_machine.state=BB_UPDATE_DOWNLOAD;
 update_stage_job(NULL);assert(update_machine.state==BB_UPDATE_DOWNLOAD && !stat(STAGE,&partial) && partial.st_size==262144);
 update_stage_job(NULL);assert(update_machine.state==BB_UPDATE_DOWNLOAD && !stat(STAGE,&partial) && partial.st_size==524288);
 update_stage_job(NULL);assert(update_machine.state==BB_UPDATE_VERIFY && !stat(STAGE,&partial) && partial.st_size==600000);
 update_stage_job(NULL);assert(update_machine.state==BB_UPDATE_READY);
 remove(STAGE);update_net_busy=0;update_net_abort=0;assert(bb_update_net_stop() && !bb_update_net_busy());assert(update_net_busy==2);assert(!update_download(NULL));
 update_net_busy=1;assert(!bb_update_net_stop() && bb_update_net_busy());assert(update_net_busy==3);__atomic_fetch_and(&update_net_busy,~1u,__ATOMIC_RELEASE);assert(bb_update_net_stop());
 update_preference=0;assert(!strcmp(bb_update_preference(),"Automatic"));
 update_prefs_loaded=1;keys[0]=KEY_RIGHT;keys[1]=KEY_RIGHT;keys[2]=KEY_B;key_index=0;
 bb_update_settings();assert(!strcmp(bb_update_preference(),"Off") && update_net_busy==2);
 update_preference=0;update_initialize_job(NULL);assert(!strcmp(bb_update_preference(),"Off"));
 keys[0]=KEY_LEFT;keys[1]=KEY_B;key_index=0;bb_update_settings();assert(!strcmp(bb_update_preference(),"Ask first"));
 keys[0]=KEY_DOWN;keys[1]=KEY_A;key_index=0;bb_update_settings();assert(update_check_now);
 game=true;key_index=0;bb_update_settings();assert(!key_index);game=false;
 f=fopen(RECEIPT,"w");assert(f);fprintf(f,"%u 1.0.0\n",installed);fclose(f);update_initialize_job(NULL);assert(!strcmp(update_notice,"Updated to 1.0.0"));assert(access(RECEIPT,F_OK));
 /* Exclusive writer reservation closes the check/start race. */
 assert(bb_writer_begin());assert(bb_writer_begin());assert(bb_writers_busy());
 assert(!bb_writers_try_exclusive());bb_writer_end();bb_writer_end();
 assert(bb_writers_try_exclusive());assert(!bb_writer_begin());bb_writers_release_exclusive();
 /* Real pthread installation: main keeps presenting; commit needs an explicit
  * between-frame acknowledgement, and no drawing may follow it. */
 assert(bb_update_known_answer());
 stage();update_checked=true;update_check_now=false;update_preference=0;
 update_machine.state=BB_UPDATE_READY;update_initialized=true;update_approved=true;
 unsigned before_stop=stop_calls;
 assert(bb_writer_begin());assert(!bb_update_tick() && !update_thread);
 assert(stop_calls==before_stop); /* Do not cancel the importer holding this reservation. */
 bb_writer_end();
 assert(!bb_update_tick() && update_thread);
 for(unsigned turn=0;!__atomic_load_n(&update_finish_ready,__ATOMIC_ACQUIRE);turn++){
  assert(turn<10000);bb_update_install_frame();usleep(1000);
 }
 assert(!bb_update_committed() && !finishes && progress_frames);
 bb_update_install_frame();
 for(unsigned turn=0;bb_update_installing();turn++){assert(turn<10000);usleep(1000);}
 assert(bb_update_committed() && finishes==1 && gpu_drains && bb_update_tick());
 journal_is(journal_success,sizeof(journal_success)/sizeof(*journal_success));
 assert(!chainloads);bb_update_shutdown();bb_writers_release_exclusive();update_commit=0;
 stage();fail_at=8;update_install_attempted=false;update_machine.state=BB_UPDATE_READY;
 assert(!bb_update_tick() && update_thread);
 for(unsigned turn=0;bb_update_installing();turn++){assert(turn<10000);bb_update_install_frame();usleep(1000);}
 assert(!bb_update_committed() && !bb_writers_busy());bb_update_tick();assert(restart_scans);fail_at=0;
 assert(journal_last(NULL)==BB_UP_FAILURE); /* an AM failure is journaled after its handles closed */
 /* HOME/sleep closes AM before handing SD to the system; no main ack required. */
 stage();update_install_attempted=false;update_machine.state=BB_UPDATE_READY;
 assert(!bb_update_tick() && update_thread);
 for(unsigned turn=0;!__atomic_load_n(&update_finish_ready,__ATOMIC_ACQUIRE);turn++){assert(turn<10000);usleep(1000);}
 update_apt(APTHOOK_ONSLEEP,NULL);
 assert(!bb_update_installing() && !bb_update_committed() && cancels==1 && !source_file);
 bb_update_tick();assert(!bb_writers_busy());
 /* A second manual request cannot repeat AM installation in this launch. */
 unsigned prior_starts=starts;update_machine.state=BB_UPDATE_READY;
 assert(!bb_update_tick() && starts==prior_starts && !update_thread);

 /* Persistent hash backoff is per manifest SHA and expires after 24 hours. */
 update_net_abort=update_net_busy=0;update_failed_count=0;
 for(unsigned i=0;i<2;i++){
  stage();memset(update_manifest.sha256,'0',64);update_manifest.sha256[64]=0;
  assert(!update_verify(NULL) && access(STAGE,F_OK));
 }
 assert(update_failed_count==2 && update_hash_backoff());
 update_failed_count=0;update_initialize_job(NULL);assert(update_failed_count==2 && update_hash_backoff());
 fake_time+=86400000;assert(!update_hash_backoff());
 fake_time=100;update_manifest.sha256[0]='1';assert(!update_hash_backoff());
 /* Wake and daily checks queue only in an idle room and respect Off. */
 accept_queue=true;update_install_attempted=false;update_machine.state=BB_UPDATE_IDLE;
 update_checked=true;update_check_now=false;update_last_check=fake_time;update_preference=0;
 unsigned before=queued;assert(!bb_update_tick() && queued==before);
 update_apt(APTHOOK_ONWAKEUP,NULL);assert(!bb_update_tick() && queued==before);
 fake_time+=3600000;update_apt(APTHOOK_ONWAKEUP,NULL);assert(!bb_update_tick() && queued==before+1);
 update_job_busy=0;fake_time+=86400000;assert(!bb_update_tick() && queued==before+2);
 update_job_busy=0;update_preference=2;update_apt(APTHOOK_ONRESTORE,NULL);
 assert(!bb_update_tick() && queued==before+2);accept_queue=false;
 /* Normal library publication cancels transport, then resumes the same stage. */
 stage();remove(STAGE);update_machine.state=BB_UPDATE_DOWNLOAD;update_net_abort=1;
 update_stage_job(NULL);assert(update_machine.state==BB_UPDATE_DOWNLOAD && update_transport_deferred==2);
 update_net_abort=update_net_busy=0;update_transport_deferred=0;
 /* A writer racing the preflight defers without burning the AM attempt. */
 update_install_attempted=true;assert(bb_writer_begin());update_install_job(NULL);
 assert(!update_install_attempted && update_machine.state==BB_UPDATE_READY && !bb_update_installing());bb_writer_end();
 /* Power telemetry fails closed; charger bypasses low battery. */
 adapter=false;battery=49;assert(!update_power_ready());battery=50;assert(update_power_ready());
 battery=49;stage();assert(!update_install(NULL) && !starts);
 battery=0;adapter=true;assert(update_power_ready());telemetry_ok=false;assert(!update_power_ready());telemetry_ok=true;
 pending_ours=pending_other=true;assert(update_recover_pending());assert(!pending_ours && pending_other && pending_deleted);
 pending_fail=true;assert(!update_recover_pending());pending_fail=false;
 pending_ours=true;pending_delete_fail=true;assert(!update_recover_pending() && pending_ours && pending_other);pending_delete_fail=false;assert(update_recover_pending());pending_other=false;
 /* A backwards RTC cannot bypass SHA backoff or cause interval underflow. */
 strcpy(update_failed_sha,update_manifest.sha256);update_failed_count=2;update_failed_time=fake_time+10000;assert(update_hash_backoff());
 u64 since=500;assert(update_elapsed(100,&since)==0 && since==100);
 /* Automatic waits for idle plus countdown; any held key postpones this launch. */
 update_job_busy=0;update_check_now=false;update_checked=true;update_preference=0;update_crypto_disabled=false;
 update_machine.state=BB_UPDATE_READY;update_install_attempted=false;update_approved=false;update_explicit_check=false;
 update_last_check=fake_time;update_idle_since=fake_time;update_countdown_since=0;
 assert(!bb_update_tick() && !update_thread);fake_time+=5000;
 assert(!bb_update_tick() && update_countdown_since && !update_thread);
 held=KEY_A;assert(!bb_update_tick() && update_postponed && !update_thread);held=0;
 fake_time+=10000;assert(!bb_update_tick() && !update_thread);
 update_postponed=false;update_crypto_disabled=true;before=queued;update_check_now=true;
 assert(!bb_update_tick() && queued==before && !update_thread);update_crypto_disabled=false;
 /* Persistent install reason survives newer background discovery status. */
 strcpy(update_failure_reason,"AM write or SD read failed");update_status=2;assert(!strcmp(update_settings_status(),"Last install didn't finish") && update_line_bad);   /* plain words; the reason stays in status.txt */
 update_status=10;assert(strstr(update_settings_status(),"50%"));
 /* A warm usable snapshot permits checks while background validation runs. */
 accept_queue=true;update_job_busy=0;update_checked=false;update_check_now=false;update_preference=0;update_retry_after=0;
 index_busy=true;index_ready=false;before=queued;assert(!bb_update_tick() && queued==before);
 index_ready=true;assert(!bb_update_tick() && queued==before+1);update_job_busy=0;index_busy=index_ready=false;accept_queue=false;
 /* ---- Bounded waits: every timeout abandons the install before AM, or cancels it. ---- */
 while(bb_writers_busy())bb_writer_end();
 update_install_abort=0;
 /* A save writer that will not finish: no log close, no AM, ownership kept. */
 stage();sleep_advances=true;save_settles=false;update_install_attempted=true;log_close_calls=0;update_install_busy=1;update_commit=0;
 update_install_job(NULL);save_settles=true;sleep_advances=false;
 assert(!starts && !log_close_calls && update_machine.state==BB_UPDATE_FAILED && !bb_writers_busy() && !bb_update_installing());
 {unsigned r;assert(journal_last(&r)==BB_UP_FAILURE && r==6);}
 /* A log writer that will not stop: the install stops before AM and the log stays. */
 stage();sleep_advances=true;log_stops=false;update_install_attempted=true;update_install_busy=1;
 update_install_job(NULL);log_stops=true;sleep_advances=false;
 assert(!starts && log_close_calls>0 && !bb_writers_busy() && !strcmp(update_failure_reason,"Log writer did not stop"));
 {const unsigned want[]={BB_UP_FLUSH,BB_UP_LOG_CLOSE,BB_UP_FAILURE};journal_is(want,3);}
 /* Main never acknowledges (worker bound): AM is cancelled, never finished. */
 stage();update_finish_allowed=0;sleep_advances=true;update_commit=0;
 assert(!update_install(NULL));sleep_advances=false;
 assert(starts==1 && cancels==1 && !finishes && !bb_update_committed() && !source_file && !strcmp(update_failure_reason,"GPU handoff timed out"));
 assert(journal_last(NULL)==BB_UP_HANDLES_CLOSED);
 /* The GPU never drains (main bound): main aborts, the worker cancels AM. */
 arm_install();update_finish_allowed=0;gpu_busy=true;sleep_advances=true;
 {unsigned before_cancels=cancels,before_starts=starts;
  assert(!bb_update_tick() && update_thread);
  for(unsigned turn=0;bb_update_installing();turn++){assert(turn<100000);bb_update_install_frame();usleep(200);}
  sleep_advances=false;gpu_busy=false;
  assert(!bb_update_committed() && cancels==before_cancels+1 && starts==before_starts+1 && !finishes && !source_file);
  bb_update_tick();assert(!update_thread);
  {const unsigned want[]={BB_UP_FLUSH,BB_UP_LOG_CLOSE,BB_UP_SECOND_SHA,BB_UP_RECEIPT,BB_UP_AM_START,BB_UP_WRITES_DONE,BB_UP_GPU_ACK,BB_UP_HANDLES_CLOSED,BB_UP_FAILURE};
   bb_update_phase_record j[BB_UPDATE_PHASE_MAX];assert(bb_update_phase_load(BB_UPDATE_PHASE_PATH,j,BB_UPDATE_PHASE_MAX)==9);
   for(unsigned i=0;i<9;i++)assert(j[i].phase==want[i]);assert(j[6].result==0);}} /* GPU-ack recorded as failed */
 /* Race corner (a): the worker times out first (main never polled). The worker aborts,
  * so a late main frame must neither acknowledge nor journal GPU-ack. */
 arm_install();update_finish_allowed=0;sleep_advances=true;
 assert(!bb_update_tick() && update_thread);
 for(unsigned turn=0;bb_update_installing();turn++){assert(turn<100000);usleep(200);}
 sleep_advances=false;
 assert(__atomic_load_n(&update_install_abort,__ATOMIC_ACQUIRE) && !finishes && !bb_update_committed());
 {unsigned last=journal_last(NULL);bb_update_install_frame();
  assert(!update_finish_allowed && journal_last(NULL)==last && last!=BB_UP_GPU_ACK);
  bb_update_phase_record j[BB_UPDATE_PHASE_MAX];unsigned n=bb_update_phase_load(BB_UPDATE_PHASE_PATH,j,BB_UPDATE_PHASE_MAX);
  for(unsigned i=0;i<n;i++)assert(j[i].phase!=BB_UP_GPU_ACK);}
 bb_update_tick();assert(!update_thread);
 /* Race corner (b): a writer that finds the journal busy drops its record and never waits. */
 {bb_update_phase_record j[BB_UPDATE_PHASE_MAX];bb_update_record(BB_UP_FLUSH,1);
  unsigned n=bb_update_phase_load(BB_UPDATE_PHASE_PATH,j,BB_UPDATE_PHASE_MAX),skipped=update_journal_skipped,seen=bb_update_phase_last();
  update_journal_busy=1;bb_update_record(BB_UP_INSTALLER_JOIN,1);
  assert(update_journal_skipped==skipped+1 && bb_update_phase_last()==seen && update_journal_busy==1);
  assert(bb_update_phase_load(BB_UPDATE_PHASE_PATH,j,BB_UPDATE_PHASE_MAX)==n);
  update_journal_busy=0;bb_update_record(BB_UP_INSTALLER_JOIN,1);
  assert(bb_update_phase_load(BB_UPDATE_PHASE_PATH,j,BB_UPDATE_PHASE_MAX)==n+1 && bb_update_phase_last()==BB_UP_INSTALLER_JOIN && !update_journal_busy);}
 /* ...and two real threads hammering the journal never produce a gap, a torn or an overwritten line. */
 {pthread_t a,b;bb_update_record(BB_UP_FLUSH,1);
  assert(!pthread_create(&a,NULL,journal_hammer,NULL) && !pthread_create(&b,NULL,journal_hammer,NULL));
  pthread_join(a,NULL);pthread_join(b,NULL);
  bb_update_phase_record j[BB_UPDATE_PHASE_MAX];unsigned n=bb_update_phase_load(BB_UPDATE_PHASE_PATH,j,BB_UPDATE_PHASE_MAX);
  struct stat st;assert(!stat(BB_UPDATE_PHASE_PATH,&st) && n>1 && st.st_size==(off_t)n*BB_UPDATE_PHASE_LINE);
  for(unsigned i=0;i<n;i++)assert(j[i].sequence==i+1);assert(!update_journal_busy);}
 /* A joined installer thread that has not exited: the handle is kept, not freed. */
 arm_install();
 assert(!bb_update_tick() && update_thread);
 for(unsigned turn=0;bb_update_installing();turn++){assert(turn<10000);bb_update_install_frame();usleep(1000);}
 assert(bb_update_committed());
 join_timeouts=1;assert(!bb_update_shutdown_timeout(UPDATE_NS(1)) && update_thread);
 assert(bb_update_shutdown_timeout(UPDATE_NS(1)) && !update_thread);
 /* The APT exit hook after a committed install whose installer never finished: it must
  * return at once (an unbounded wait here froze the console). A timeout in the test
  * kills the process instead of hanging it. */
 update_install_busy=1;alarm(20);update_apt(APTHOOK_ONEXIT,NULL);alarm(0);
 assert(update_install_busy==1);update_install_busy=0;
 /* Handoff: relaunch only when every step was clean and the settle time has passed. */
 sleep_advances=true;
 for(unsigned mode=0;mode<5;mode++){
  update_commit_time=update_now();chainloads=chainload_title=chainload_clears=0;
  bool clean=mode!=1;closed=mode==2;no_gpu=mode==3;
  u64 begin=fake_time;
  if(mode==4)update_commit_time=update_now()-UPDATE_SETTLE_MS; /* long since committed: no extra wait */
  bool requested=bb_update_handoff(clean);
  closed=no_gpu=false;
  bool launched=chainloads+chainload_title==1;
#if BB_UPDATE_HANDOFF
  bool expect=clean && mode!=2 && mode!=3;
#else
  bool expect=false;
#endif
  assert(launched==expect && requested==expect && chainload_clears==!expect);
  if(expect)assert(BB_UPDATE_HANDOFF==2?(chainload_title==1 && !chainloads):(chainloads==1 && !chainload_title));
  assert(journal_last(NULL)==(expect?BB_UP_CHAINLOAD:BB_UP_HOME_RETURN));
  if(expect && mode!=4)assert(fake_time-begin>=UPDATE_SETTLE_MS); /* waited out the settle time */
  if(expect && mode==4)assert(fake_time-begin<100);
 }
 sleep_advances=false;update_commit=0;
 /* Next boot: the previous journal is logged once and acknowledged, never rewritten. */
 {const unsigned partial[]={BB_UP_FLUSH,BB_UP_LOG_CLOSE};
  bb_update_record(BB_UP_FLUSH,1);bb_update_record(BB_UP_LOG_CLOSE,1);journal_is(partial,2);
  journal_logs=0;update_initialize_job(NULL);assert(journal_logs==1);
  unsigned r;assert(journal_last(&r)==BB_UP_NEXT_BOOT && r==BB_UP_LOG_CLOSE);
  update_initialize_job(NULL);update_initialize_job(NULL);
  assert(journal_logs==1); /* a finished journal is not logged again on every boot */
  bb_update_phase_record j[BB_UPDATE_PHASE_MAX];assert(bb_update_phase_load(BB_UPDATE_PHASE_PATH,j,BB_UPDATE_PHASE_MAX)==3);}
 bb_writer_end();assert(bb_writers_busy() && !bb_writers_try_exclusive()); /* fault does not abort or underflow */
 /* ---- UI polish: consent, progress, worker completion, one policy for room and Settings, footer touch ---- */
 {
  update_initialized=true;update_crypto_disabled=false;diagnostic=false;game=false;update_job_busy=0;update_retry_after=0;update_transport_deferred=0;
  update_checked=true;update_check_now=false;update_preference=0;accept_queue=true;index_busy=false;index_ready=true;
  strcpy(update_manifest.display,"1.0.1");update_manifest.size=1000;
  /* (1) A worker still running: its phase is not read and nothing is queued, whatever the phase says. */
  update_machine.state=BB_UPDATE_VERIFY;update_job_busy=1;update_explicit_check=true;update_approved=true;update_view.state=BB_UPDATE_IDLE;
  unsigned before=queued;assert(update_poll(false)==UPDATE_POLL_WAIT && queued==before && update_view.state==BB_UPDATE_IDLE);
  /* ... it completes VERIFY -> READY and releases busy: the next poll sees READY and queues no staging job. */
  update_machine.state=BB_UPDATE_READY;__atomic_store_n(&update_job_busy,0,__ATOMIC_RELEASE);
  assert(update_poll(false)==UPDATE_POLL_WAIT && queued==before && update_view.state==BB_UPDATE_READY);
  /* Forced in-poll interleaving (product regression from the 2026-11-05 re-check): the worker publishes VERIFY ->
     READY and releases busy inside this very poll, before its busy load. Whatever order the poll reads them in,
     it must see READY: nothing queued, no flush, the phase stays READY. */
  update_machine.state=BB_UPDATE_VERIFY;update_view.state=BB_UPDATE_VERIFY;update_job_busy=1;update_explicit_check=true;update_approved=true;
  {unsigned q=queued,flushes=stop_calls;session_hook=complete_in_poll;
   assert(update_poll(false)==UPDATE_POLL_WAIT && !session_hook);
   assert(queued==q && stop_calls==flushes && update_machine.state==BB_UPDATE_READY && update_view.state==BB_UPDATE_READY);}
  /* A staging job that runs anyway in READY (or any non-staging phase) refuses: no flush, no INSTALL. */
  unsigned stops=stop_calls;update_job_busy=1;update_stage_job(NULL);
  assert(update_machine.state==BB_UPDATE_READY && stop_calls==stops && !update_job_busy);
  for(int ph=BB_UPDATE_IDLE;ph<=BB_UPDATE_FAILED;ph++){if(ph==BB_UPDATE_DOWNLOAD||ph==BB_UPDATE_VERIFY)continue;
   update_machine.state=(bb_update_state)ph;update_job_busy=1;update_stage_job(NULL);assert((int)update_machine.state==ph && !update_job_busy && stop_calls==stops);}
  /* (6) Explicit check found an update: the dialog comes before any staging; while it waits nothing says Downloading. */
  __atomic_store_n(&update_dl_bytes,700,__ATOMIC_RELAXED);   /* an older transfer's counter */
  update_machine.state=BB_UPDATE_DOWNLOAD;update_explicit_pending=true;update_explicit_check=true;update_approved=false;update_view.staging=false;
  confirm_answer=false;confirms=0;confirm_saw_progress=false;before=queued;
  assert(update_poll(false)==UPDATE_POLL_WAIT && confirms==1 && !confirm_saw_progress);
  assert(queued==before && update_machine.state==BB_UPDATE_IDLE && !update_approved && !update_explicit_check);   /* Later */
  char t[48];assert(!update_progress_text(t,sizeof(t)));notice_seen[0]=0;update_notice_until=0;bb_update_draw_notice();assert(!strstr(notice_seen,"Downloading"));
  /* Update: approved, the staging job is queued, and only now the progress shows (from the new manifest's counter). */
  update_machine.state=BB_UPDATE_DOWNLOAD;update_explicit_pending=true;update_explicit_check=true;confirm_answer=true;confirm_saw_progress=false;
  __atomic_store_n(&update_dl_bytes,0,__ATOMIC_RELAXED);
  assert(update_poll(false)==UPDATE_POLL_WAIT && confirms==2 && !confirm_saw_progress && update_approved);
  assert(queued==before+1 && queued_fn==update_stage_job && update_view.staging);
  __atomic_store_n(&update_dl_bytes,500,__ATOMIC_RELAXED);assert(update_progress_text(t,sizeof(t)) && !strcmp(t,"Downloading update 50%"));
  bb_update_draw_notice();assert(!strcmp(notice_seen,"Downloading update 50%"));
  assert(!strcmp(update_settings_status(),"Downloading update 50%"));
  /* (7) One policy: a cancelled stage defers; the Settings card waits the same 5 s as the room, then retries. */
  update_job_busy=0;__atomic_store_n(&update_transport_deferred,2,__ATOMIC_RELEASE);before=queued;
  assert(update_poll(false)==UPDATE_POLL_WAIT && queued==before && update_retry_after==update_now()+5000);
  fake_time+=4999;assert(update_poll(false)==UPDATE_POLL_WAIT && queued==before);
  fake_time+=1;assert(update_poll(false)==UPDATE_POLL_WAIT && queued==before+1);update_job_busy=0;
  /* the developer guard holds in Settings too */
  diagnostic=dev_bench=true;before=queued;assert(update_poll(false)==UPDATE_POLL_WAIT && queued==before);diagnostic=dev_bench=false;
  /* Automatic (not explicit) downloads keep their progress notice once staging starts. */
  update_explicit_check=false;update_approved=false;update_view.staging=false;update_machine.state=BB_UPDATE_DOWNLOAD;update_preference=0;
  assert(!update_progress_text(t,sizeof(t)));update_poll(false);update_job_busy=0;assert(update_view.staging && update_progress_text(t,sizeof(t)));
  /* Off never downloads unasked (and shows no progress). */
  update_preference=2;update_view.staging=false;before=queued;update_poll(false);assert(queued==before && !update_progress_text(t,sizeof(t)));
  update_preference=0;update_machine.state=BB_UPDATE_IDLE;update_view.state=BB_UPDATE_IDLE;update_view.staging=false;
  /* Settings never goes past READY: READY is for the room only. */
  update_machine.state=BB_UPDATE_READY;assert(update_poll(false)==UPDATE_POLL_WAIT);assert(update_poll(true)==UPDATE_POLL_READY);
  update_machine.state=BB_UPDATE_IDLE;
  /* (8) J-04: Check now while the library scans: "Checking" at once; the scan cancels the first
     discovery; the retry is still the player's check, so its result is asked, never auto-downloaded. */
  update_machine.state=BB_UPDATE_IDLE;update_view.state=BB_UPDATE_IDLE;update_explicit_check=update_explicit_pending=update_approved=false;
  update_checked=true;update_check_now=false;update_retry_after=0;update_job_busy=0;update_transport_deferred=0;update_preference=0;
  accept_queue=true;index_busy=true;index_ready=false;tap_tab=-1;
  keys[0]=KEY_DOWN;keys[1]=KEY_A;keys[2]=KEY_B;key_index=0;bb_update_settings();
  assert(update_check_now && update_explicit_pending && !strcmp(update_settings_status(),"Checking for updates..."));
  notice_seen[0]=0;update_notice_until=0;bb_update_draw_notice();assert(!strcmp(notice_seen,"Checking for updates"));
  before=queued;assert(update_poll(false)==UPDATE_POLL_WAIT && queued==before && update_explicit_pending);   /* scanning: waits */
  index_busy=false;index_ready=true;
  assert(update_poll(false)==UPDATE_POLL_WAIT && queued==before+1 && queued_fn==update_discover_job && update_explicit_check);
  update_job_busy=0;__atomic_store_n(&update_transport_deferred,1,__ATOMIC_RELEASE);   /* the library publication cancels it */
  assert(update_poll(false)==UPDATE_POLL_WAIT && queued==before+1 && update_explicit_pending);
  fake_time+=5000;assert(update_poll(false)==UPDATE_POLL_WAIT && queued==before+2 && update_explicit_check && update_explicit_pending);
  update_job_busy=0;update_machine.state=BB_UPDATE_DOWNLOAD;confirm_answer=false;confirms=0;
  assert(update_poll(false)==UPDATE_POLL_WAIT && confirms==1 && queued==before+2);   /* asked; Later */
  assert(update_poll(false)==UPDATE_POLL_WAIT && queued==before+2 && !update_view.staging);   /* Automatic downloads nothing */
  update_machine.state=BB_UPDATE_IDLE;update_view.state=BB_UPDATE_IDLE;confirm_answer=true;
  /* (5) The Updates card's footer works by touch: a tap on Back leaves; a tap on Check checks. */
  accept_queue=false;keys[0]=KEY_TOUCH;key_index=0;tap_tab=0;bb_update_settings();assert(key_index==1);
  keys[0]=KEY_DOWN;keys[1]=KEY_TOUCH;keys[2]=KEY_B;key_index=0;tap_tab=1;update_check_now=false;
  /* second frame: the tap on tab 1 (Check) is A on "Check now"; third frame B leaves */
  bb_update_settings();assert(update_check_now && key_index==3);
  tap_tab=-1;update_check_now=false;accept_queue=false;update_explicit_check=false;update_approved=false;
  /* (6) Settings > Updates always opens and says why the updater cannot run; Check now answers with that
   * line and starts nothing, and a preference that was not loaded is never written. */
  {
   unsigned prefs=update_prefs_loaded;bool init=update_initialized;before=queued;update_preference=0;update_prefs_loaded=0;update_initialized=false;accept_queue=true;
   diagnostic=dev_bench=true;   /* the developer remote is on: the updater never started, and the screen must not start it */
   keys[0]=KEY_DOWN;keys[1]=KEY_A;keys[2]=KEY_B;key_index=0;sfx_errors=0;bb_update_settings();
   assert(key_index==3 && sfx_errors==1 && !update_check_now && queued==before && !update_initialized);   /* opened, ran to B, Check now refused, nothing queued */
   accept_queue=false;update_initialized=true;
   assert(!strcmp(update_card_notice(),"Off while remote is on") && !strcmp(update_settings_status(),"Off while remote is on") && !update_line_bad);
   keys[0]=KEY_RIGHT;keys[1]=KEY_A;keys[2]=KEY_B;key_index=0;sfx_errors=0;bb_update_settings();
   assert(key_index==3 && sfx_errors==2 && update_preference==0);   /* Install: nothing written */
   assert(!strcmp(update_card_notice(),"Off while remote is on"));
   diagnostic=dev_bench=false;homebrew=true;   /* started from the Homebrew Launcher */
   assert(!strcmp(update_card_notice(),"Needs the installed app"));
   update_prefs_loaded=1;keys[0]=KEY_DOWN;keys[1]=KEY_A;keys[2]=KEY_B;key_index=0;sfx_errors=0;bb_update_settings();
   assert(key_index==3 && sfx_errors==1 && !update_check_now && !update_explicit_pending && queued==before);
   assert(!strcmp(update_card_notice(),"Needs the installed app"));
   homebrew=false;update_prefs_loaded=0;   /* the updater is still starting */
   assert(!strcmp(update_card_notice(),"Starting...") && !strcmp(update_settings_status(),"Starting..."));
   update_initialized=false;accept_queue=true;keys[0]=KEY_B;key_index=0;bb_update_settings();   /* the screen starts it itself */
   assert(update_initialized && queued==before+1 && queued_fn==update_initialize_job);
   update_job_busy=0;accept_queue=false;
   update_prefs_loaded=1;   /* ready: the normal card, no standing line */
   assert(!update_unavailable() && !strcmp(update_card_notice(),""));
   keys[0]=KEY_DOWN;keys[1]=KEY_A;keys[2]=KEY_B;key_index=0;sfx_errors=0;bb_update_settings();assert(update_check_now && !sfx_errors);
   update_check_now=update_explicit_pending=false;
   /* the updater disabled itself (self-test, or the old update could not be cleared): Check now says why and never
    * waits on a check that cannot start */
   unsigned asked=queued;update_explicit_check=update_approved=false;
   for(unsigned status=11;status<=13;status+=2){
    unsigned saved=update_status;update_crypto_disabled=true;update_status=status;
    keys[0]=KEY_DOWN;keys[1]=KEY_A;keys[2]=KEY_B;key_index=0;sfx_errors=0;bb_update_settings();
    assert(key_index==3 && sfx_errors==1 && !update_check_now && !update_explicit_pending && queued==asked);
    assert(!strcmp(update_card_notice(),update_status_text[status]) && update_line_bad);
    update_crypto_disabled=false;update_status=saved;
   }
   update_prefs_loaded=prefs;update_initialized=init;queued=before;
  }
  puts("PASS: updater UI policy: worker completion acquired before its phase, staging refuses other phases, consent before any progress, Later, progress %, one retry policy in Settings, developer guard, READY only in the room, footer taps");
 }
 update_network_close();
 printf("Host ASan pthread canary: %zu / 98304 bytes touched (includes pthread startup; not ARM stack evidence)\n",host_stack_high_water);
 puts("PASS: production AM preflight/cancel/finish, save/config gates, game rule, keyless discovery/title/URL/hash rejection, real curl Range/multi-turn/truncation/resume/ignored-range/oversize, preferences/receipt, SHA-256/parser self-test, pthread progress/commit barrier, writer exclusion, HOME cancellation, one install/launch");
}
