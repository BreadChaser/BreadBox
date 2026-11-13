/* Player updater. Main owns policy/presentation; low lane owns network/staging.
 * A dedicated worker owns verification/AM, with a between-frame commit barrier.
 * A handoff flag publishes the worker's complete result with release/acquire. */
#include <string.h>
#include "app_policy.h"
#include "safefile.h"
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
#include "bb_update_phase.h"
#include "bb_index.h"
#include "bb_save.h"
#include "bb_writers.h"
#include "bb_registry.h"
#include "safefile.h"
#include "devtools.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define UPDATE_ROOT APP_SD_ROOT "/updates"
#define STAGE UPDATE_ROOT "/BreadBox.cia.part"
#define PREF APP_SD_ROOT "/updates.txt"
#define RECEIPT UPDATE_ROOT "/updated.txt"
#define STATUS UPDATE_ROOT "/status.txt"
#if defined(BB_UPDATE_TEST_REMOTE) && !defined(BB_UPDATE_DEV_URL)
#error BB_UPDATE_TEST_REMOTE requires a development URL
#endif
#ifdef BB_UPDATE_DEV_URL
static const char update_dev_url_marker[]="BB_UPDATE_DEV_URL=" BB_UPDATE_DEV_URL;
#endif
#ifndef BB_UPDATE_DEV_URL
#define RELEASE_API "https://github.com/BreadChaser/BreadBox/releases/latest/download/update.json"
#endif
/* Bounds for waits on the install path. None of them kills a thread. Before the
 * commit no handle is abandoned: a timeout before AM_StartCiaInstall abandons the
 * install and one before AM_FinishCiaInstall cancels the AM import. After the commit
 * AM is done, but a timeout there selects the HOME return and lets the process exit
 * with the installer's FS/AM tail still in flight (installer join) or a live SOC
 * session (developer network join, the covers.c pm-crash precedent; developer builds
 * only). AM_FinishCiaInstall itself cannot be bounded or cancelled. */
#define UPDATE_NS(ms) ((u64)(ms)*1000000ULL)
#define UPDATE_PREP_MS 5000    /* cancellable consumer quiescence */
#define UPDATE_CANCEL_MS 30000 /* terminal process exit if an SDK call never returns */
#define UPDATE_SAVE_MS 15000   /* background save/history writer */
#define UPDATE_LOG_MS 5000     /* log writer */
#define UPDATE_ACK_MAIN_MS 4000 /* main: GPU drained since the last progress frame */
#define UPDATE_ACK_WORKER_MS 10000 /* worker: waits for main's acknowledgment */
#define UPDATE_SETTLE_MS 2000  /* after AM commit, before asking APT to relaunch */
/* How a committed install ends: 0 HOME, 1 relaunch self (soft reset), 2 launch the
 * installed title by ID. Hardware-selectable at build time; see docs/auto-update.md. */
#ifndef BB_UPDATE_HANDOFF
#define BB_UPDATE_HANDOFF 1
#endif
static bb_update_machine update_machine;
static Thread update_thread;
static u64 update_commit_time,update_ack_since;
static unsigned update_phase_seen;
static u64 update_now(void);
/* Journal write (updates/phase.txt). On the normal path the install steps hand over
 * to each other through acquire/release flags, but a timeout lets two writers meet
 * (the worker's own ack timeout against main's ack, or main's installer-join record
 * against the installer's handles-closed record). The journal's sequence and
 * seek-plus-write are not synchronized, so appends take a try-lock: a writer that
 * finds it busy drops its record and never waits. A flag, not a LightLock: a zeroed
 * LightLock is not unlocked, and no single-threaded point precedes the first record. */
static unsigned update_journal_busy,update_journal_skipped;
void bb_update_record(unsigned phase,uint32_t result)
{
 if(__atomic_exchange_n(&update_journal_busy,1,__ATOMIC_ACQUIRE)){__atomic_fetch_add(&update_journal_skipped,1,__ATOMIC_RELAXED);return;}
 (void)bb_update_phase_write(BB_UPDATE_PHASE_PATH,phase,result,(uint32_t)update_now(),phase==BB_UP_FLUSH);
 __atomic_store_n(&update_phase_seen,phase,__ATOMIC_RELEASE);
 __atomic_store_n(&update_journal_busy,0,__ATOMIC_RELEASE);
}
unsigned bb_update_phase_last(void){return __atomic_load_n(&update_phase_seen,__ATOMIC_ACQUIRE);}
static unsigned update_install_busy,update_commit,update_progress,update_wake,update_finish_ready,update_finish_allowed,update_install_abort;
static u64 update_last_check,update_idle_since,update_countdown_since,update_retry_after;
static unsigned update_transport_deferred;
static bool update_postponed,update_crypto_disabled;
static char update_failure_reason[96],update_restart_root[1024];
static u64 update_power_checked;static bool update_power_cached;
static unsigned update_check_failures;
static aptHookCookie update_apt_cookie;
static CURLM *update_multi;
static char update_asset_url[2048],update_asset_source[512];
static _Atomic unsigned update_status;
/* What the Updates card says (J-05): plain words that fit its one foot line, no codes. The technical
 * reason of a failed install stays in status.txt and the log. */
static const char *const update_status_text[]={"Not checked yet","Checking for updates...","Update found","No connection. Check Wi-Fi",
 "Update info was damaged","Download was damaged","Install failed. Try again","BreadBox is up to date",
 "Try the update tomorrow","Couldn't write the SD card","Plug in or charge to 50%","Updater self-test failed",
 "Unfinished update removed","Couldn't clear old update","Check the date and time","Stopped. Will try again",
 "No update info right now"};   /* 16: the server answered, without the update file (404, 503 ...): not Wi-Fi */
/* Results that are good news are written in ink, not red. */
static bool update_status_good(unsigned status){return status==0 || status==1 || status==2 || status==7 || status==12;}
#define BACKOFF UPDATE_ROOT "/failed.txt"
static char update_failed_sha[65];
static unsigned update_failed_count;
static u64 update_failed_time;
static void update_network_close(void)
{if(update_multi){curl_multi_cleanup(update_multi);update_multi=NULL;}update_asset_url[0]=0;}
static bb_update_manifest update_manifest;
static int update_preference; /* Automatic, Ask first, Off */
static bool update_initialized,update_checked,update_check_now,update_explicit_check,update_approved,update_install_attempted;
static unsigned update_job_busy,update_net_busy,update_net_abort,update_prefs_loaded;
bool bb_update_net_busy(void){return (__atomic_load_n(&update_net_busy,__ATOMIC_ACQUIRE)&1)!=0;}
bool bb_update_net_stop(void){unsigned old=__atomic_fetch_or(&update_net_busy,2,__ATOMIC_ACQ_REL);__atomic_store_n(&update_net_abort,1,__ATOMIC_RELEASE);if(!(old&1))update_network_close();return !(old&1);}
static unsigned update_installed_version;
static char update_notice[64];
static u64 update_notice_until;
static bool update_space(uint64_t bytes)
{
 FS_ArchiveResource r;
 return R_SUCCEEDED(FSUSER_GetSdmcArchiveResource(&r)) &&
  (uint64_t)r.clusterSize*r.freeClusters>bytes;
}
static bool update_cancelled(void)
{return (__atomic_load_n(&update_net_busy,__ATOMIC_ACQUIRE)&2) || __atomic_load_n(&update_net_abort,__ATOMIC_ACQUIRE) || bb_index_cancelled() || aptShouldClose() || !gspHasGpuRight();}
typedef struct {char *data;FILE *file;size_t used,limit,space_checked,base;long response;bool ranged;} sink;
static size_t update_dl_bytes; /* the staged file's bytes so far: "Downloading update NN%" */
static size_t update_header(char *p,size_t a,size_t b,void *ud)
{
 if(a && b>SIZE_MAX/a)return 0;size_t n=a*b;sink *s=ud;
 if(n>=12 && !memcmp(p,"HTTP/",5)){
  char *at=memchr(p,' ',n);
  if(at && p+n-at>=4 && at[1]>='0' && at[1]<='9' && at[2]>='0' && at[2]<='9' && at[3]>='0' && at[3]<='9')
   s->response=(at[1]-'0')*100+(at[2]-'0')*10+at[3]-'0';
 }
 return n;
}
static size_t update_receive(char *p,size_t a,size_t b,void *ud)
{
 sink *s=ud;if(a && b>SIZE_MAX/a)return 0;size_t n=a*b;
 if(update_cancelled())return 0;
 /* Ignore redirect/error bodies, and a full response to a resumed Range. */
 if(s->file && s->ranged && s->response==200)return 0; /* stop ignored Range immediately */
 if(s->response>=300)return n;
 if(n>s->limit-s->used)return 0;
 if(s->file && (!s->space_checked || s->used+1-s->space_checked>=1048576)){if(!update_space(2097152))return 0;s->space_checked=s->used+1;}
 if(s->file){if(fwrite(p,1,n,s->file)!=n)return 0;}else memcpy(s->data+s->used,p,n);
 s->used+=n;if(s->file)__atomic_store_n(&update_dl_bytes,s->base+s->used,__ATOMIC_RELAXED);return n;
}
/* Inspect the literal authority, without accepting userinfo, ports, suffixes or
 * subdomains. CDN query strings are opaque signed GitHub release links. */
static bool update_transport_url(const char *url)
{
#ifdef BB_UPDATE_DEV_URL
 (void)url;return true; /* Explicit private fixtures retain local HTTP transport. */
#else
 static const char *const origins[]={"https://github.com/","https://objects.githubusercontent.com/","https://release-assets.githubusercontent.com/"};
 if(!url)return false;
 for(unsigned i=0;i<sizeof(origins)/sizeof(*origins);i++)if(!strncmp(url,origins[i],strlen(origins[i])))return true;
 return false;
#endif
}
static bool update_fetch(const char *url,sink *s,const char *range,long *status)
{
 if(update_cancelled() || sw_nonet || sw_nocovers)return false;
 unsigned expected=0;
 if(!__atomic_compare_exchange_n(&update_net_busy,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return false;
 bool resolved=s->file && update_asset_url[0] && !strcmp(url,update_asset_source);
 const char *request=resolved?update_asset_url:url;
 if(!update_transport_url(request)){update_asset_url[0]=0;__atomic_fetch_and(&update_net_busy,~1u,__ATOMIC_RELEASE);return false;}
 CURL *c=curl_easy_init();if(!update_multi)update_multi=curl_multi_init();CURLM *multi=update_multi;
 if(!c || !multi){if(c)curl_easy_cleanup(c);__atomic_fetch_and(&update_net_busy,~1u,__ATOMIC_RELEASE);return false;}
 curl_easy_setopt(c,CURLOPT_URL,request);
 s->ranged=range!=NULL;
 curl_easy_setopt(c,CURLOPT_HEADERFUNCTION,update_header);
 curl_easy_setopt(c,CURLOPT_HEADERDATA,s);
 curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);
 curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
 curl_easy_setopt(c,CURLOPT_CAINFO,"romfs:/certs.pem");
 curl_easy_setopt(c,CURLOPT_USERAGENT,"BreadBox updater/1");
 curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
 curl_easy_setopt(c,CURLOPT_MAXREDIRS,5L);
#ifndef BB_UPDATE_DEV_URL
 curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"https");
 curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS_STR,"https");
#endif
 curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,10L);
 curl_easy_setopt(c,CURLOPT_TIMEOUT,45L);
 curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
 curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,update_receive);
 curl_easy_setopt(c,CURLOPT_WRITEDATA,s);
 if(range)curl_easy_setopt(c,CURLOPT_RANGE,range);
 else if(s->file){curl_easy_setopt(c,CURLOPT_TIMEOUT,0L);curl_easy_setopt(c,CURLOPT_LOW_SPEED_LIMIT,1024L);curl_easy_setopt(c,CURLOPT_LOW_SPEED_TIME,60L);}
 bool ok=false;CURLcode error=CURLE_OK;int running=1,left;
 curl_multi_add_handle(multi,c);
 while(!update_cancelled()){
  if(curl_multi_perform(multi,&running)!=CURLM_OK)break;
  if(!running){CURLMsg *msg;while((msg=curl_multi_info_read(multi,&left)))if(msg->msg==CURLMSG_DONE){error=msg->data.result;ok=error==CURLE_OK;}break;}
  curl_multi_poll(multi,NULL,0,0,NULL); /* never block the shared SOC IPC session */
  bb_index_visible_checkpoint();svcSleepThread(10000000);
 }
 if(error==CURLE_PEER_FAILED_VERIFICATION)update_status=14;
 curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,status);
 char *final=NULL;
 if(curl_easy_getinfo(c,CURLINFO_EFFECTIVE_URL,&final)!=CURLE_OK || !update_transport_url(final)){ok=false;update_asset_url[0]=0;}
 if(ok && s->file && (*status==200 || *status==206) && final && strlen(final)<sizeof(update_asset_url) && strlen(url)<sizeof(update_asset_source)){strcpy(update_asset_url,final);strcpy(update_asset_source,url);}
 if(*status==403 || *status==410)update_asset_url[0]=0;
 curl_multi_remove_handle(multi,c);curl_easy_cleanup(c);
 __atomic_fetch_and(&update_net_busy,~1u,__ATOMIC_RELEASE);
#ifdef BB_UPDATE_DEV_URL
 log_printf("update: fixture transport code=%d (%s) cancel=%d\n",(int)error,curl_easy_strerror(error),update_cancelled());
#endif
 log_printf("update: GET %s HTTP %ld curl_ok=%d bytes=%lu\n",url,*status,ok,(unsigned long)s->used);
 if(resolved && (*status==403 || *status==410)){s->response=0;return update_fetch(url,s,range,status);}
 return ok;
}
static bool update_memory_fetch(const char *url,void *data,size_t cap,size_t *size,long *code)
{
 sink s={.data=data,.limit=cap};*code=0;
 bool ok=update_fetch(url,&s,NULL,code) && *code==200;*size=s.used;return ok;
}
static bool update_title_version(void)
{
 u64 self=0;AM_TitleInfo installed;
 if(envIsHomebrew()){log_printf("update: installed CIA required\n");return false;}
 Result rc=APT_GetProgramID(&self);
 if(R_FAILED(rc) || self!=BB_UPDATE_TITLE){log_printf("update: program identity unavailable rc=%08lx\n",(unsigned long)rc);return false;}
 if(R_FAILED(rc=amInit())){log_printf("update: AM unavailable rc=%08lx\n",(unsigned long)rc);return false;}
 rc=AM_GetTitleInfo(MEDIATYPE_SD,1,&self,&installed);amExit();
 if(R_FAILED(rc)){log_printf("update: installed version unavailable rc=%08lx\n",(unsigned long)rc);return false;}
 update_installed_version=installed.version;return true;
}
/* Fail closed when power telemetry or pending-import recovery is unavailable. */
static bool update_power_ready(void)
{
 bool adapter=false;u8 percent=0;
 if(R_FAILED(ptmuInit()))return false;
 Result rc=PTMU_GetAdapterState(&adapter);ptmuExit();
 if(R_SUCCEEDED(rc) && adapter){log_printf("update: power charger=1 ready=1\n");return true;}
 if(R_FAILED(mcuHwcInit()))return false;
 rc=MCUHWC_GetBatteryLevel(&percent);mcuHwcExit();
 bool ready=R_SUCCEEDED(rc) && percent>=50;
 log_printf("update: power charger=0 battery=%u rc=%08lx ready=%d\n",percent,(unsigned long)rc,ready);return ready;
}
static bool update_recover_pending(void)
{
 if(envIsHomebrew())return true;
 u64 self=0;if(R_FAILED(APT_GetProgramID(&self)) || self!=BB_UPDATE_TITLE)return false;
 if(R_FAILED(amInit()))return false;
 u32 count=0,got=0;u64 *ids=NULL;bool ok=false;
 if(R_FAILED(AM_GetNumPendingTitles(&count,MEDIATYPE_SD)) || count>4096)goto done;
 if(!count){ok=true;goto done;}
 ids=malloc(count*sizeof(*ids));if(!ids)goto done;
 if(R_FAILED(AM_GetPendingTitleList(&got,ids,count,MEDIATYPE_SD)) || got>count)goto done;
 ok=true;
 for(u32 i=0;i<got;i++)if(ids[i]==BB_UPDATE_TITLE){
  Result rc=AM_DeletePendingTitle(MEDIATYPE_SD,BB_UPDATE_TITLE);
  log_printf("update: pending import cleanup rc=%08lx\n",(unsigned long)rc);
  ok=R_SUCCEEDED(rc);update_status=ok?12:13;break;
 }
 done:log_printf("update: pending titles count=%lu enumerated=%lu ready=%d\n",(unsigned long)count,(unsigned long)got,ok);free(ids);amExit();return ok;
}
static void update_save_status(void)
{
 if(update_failure_reason[0])log_printf("update: status %u: %s\n",(unsigned)update_status,update_failure_reason);
 FILE *f=safe_begin(STATUS);if(f){bool ok=fprintf(f,"%u %s\n",(unsigned)update_status,update_failure_reason)>0;safe_finish(f,STATUS,ok);}
}
/* RTC subtraction never wraps. Backwards jumps restart the minimum interval. */
static u64 update_elapsed(u64 now,u64 *since)
{if(now<*since)*since=now;return now-*since;}
static void update_initialize_job(void *unused)
{
 (void)unused;
 update_crypto_disabled=!bb_update_known_answer();
 log_printf("update: SHA-256/manifest known-answer %s\n",update_crypto_disabled?"FAIL":"PASS");
 mkdir(UPDATE_ROOT,0777);
 safe_recover(STATUS);FILE *status_file=fopen(STATUS,"r");unsigned prior_status;
 if(status_file){if(fscanf(status_file,"%u",&prior_status)==1 && prior_status<sizeof(update_status_text)/sizeof(*update_status_text))update_status=prior_status;fgets(update_failure_reason,sizeof(update_failure_reason),status_file);update_failure_reason[strcspn(update_failure_reason,"\r\n")]=0;fclose(status_file);}
 if(!update_recover_pending()){update_crypto_disabled=true;update_status=13;}
 if(update_crypto_disabled && update_status!=13)update_status=11;
 safe_recover(PREF);FILE *f=fopen(PREF,"r");int p;
 if(f){if(fscanf(f,"%d",&p)==1 && p>=0 && p<=2)__atomic_store_n(&update_preference,p,__ATOMIC_RELEASE);fclose(f);}
 safe_recover(BACKOFF);f=fopen(BACKOFF,"r");if(f){unsigned long long t;if(fscanf(f,"%64s %u %llu",update_failed_sha,&update_failed_count,&t)==3)update_failed_time=t;fclose(f);}
 safe_recover(RECEIPT);f=fopen(RECEIPT,"r");
 if(f){unsigned ver;char display[32];
  if(fscanf(f,"%u %31s",&ver,display)==2 && update_title_version() && ver==update_installed_version)
   snprintf(update_notice,sizeof(update_notice),"Updated to %s",display);
  fclose(f);remove(RECEIPT);if(update_notice[0]){remove(STAGE);remove(STAGE ".sha");remove(STATUS);update_failure_reason[0]=0;}
 }
 /* The previous install's journal locates any stop exactly (the log is closed before AM).
  * Acknowledge the boot once, so a journal that ends at a chainload or HOME request is
  * distinguishable from one that never got that far. Also readable from the remote:
  * get sdmc:/3ds/BreadBox/updates/phase.txt */
 bb_update_phase_record journal[BB_UPDATE_PHASE_MAX];unsigned records=bb_update_phase_load(BB_UPDATE_PHASE_PATH,journal,BB_UPDATE_PHASE_MAX);
 if(records){
  char text[BB_UPDATE_PHASE_MAX*24];bb_update_phase_text(journal,records,text,sizeof(text));
  unsigned last=journal[records-1].phase;
  if(last!=BB_UP_NEXT_BOOT){ /* logged once; a journal with next-boot stays on the card for the remote */
   log_printf("update: previous install journal (%u records): %s\n",records,text);
   log_printf("update: previous install ended after %s\n",bb_update_phase_name(last));
   bb_update_record(BB_UP_NEXT_BOOT,last);
  }
 }
 __atomic_store_n(&update_prefs_loaded,1,__ATOMIC_RELEASE);
 __atomic_store_n(&update_job_busy,0,__ATOMIC_RELEASE);
}
static bool update_hash_backoff(void)
{
 if(strcmp(update_failed_sha,update_manifest.sha256) || update_failed_count<2)return false;
 u64 now=osGetTime();if(now<update_failed_time){update_failed_time=now;
  FILE *f=safe_begin(BACKOFF);if(f){bool ok=fprintf(f,"%s %u %llu\n",update_failed_sha,update_failed_count,(unsigned long long)now)>0;safe_finish(f,BACKOFF,ok);}}
 return now-update_failed_time<86400000;
}
static u64 update_now(void){return svcGetSystemTick()/(SYSCLOCK_ARM11/1000);} /* monotonic ms */
static void update_discover_job(void *unused)
{
 (void)unused;update_status=3;__atomic_store_n(&update_dl_bytes,0,__ATOMIC_RELAXED);
 if(update_cancelled() || !update_title_version())goto done;
 char url[600];size_t n;
#ifdef BB_UPDATE_DEV_URL
 log_printf("update: emulator fixture %s\n",update_dev_url_marker);
 snprintf(url,sizeof(url),"%supdate.json",BB_UPDATE_DEV_URL);
#else
 snprintf(url,sizeof(url),"%s",RELEASE_API);
#endif
 char data[2048];
 long code;
 if(!update_memory_fetch(url,data,sizeof(data),&n,&code)){if(code>0 && code!=200 && update_status==3)update_status=16;goto done;}
 update_status=4;
 if(!bb_update_manifest_parse(data,n,&update_manifest)){log_printf("update: manifest rejected\n");goto done;}
 update_status=7;if(!bb_update_newer(BB_UPDATE_TITLE,update_manifest.version,update_installed_version)){log_printf("update: up to date (manifest=%u installed=%u)\n",update_manifest.version,update_installed_version);remove(STAGE);remove(STAGE ".sha");goto done;}
 if(update_hash_backoff()){update_status=8;log_printf("update: hash retry backoff\n");goto done;}
 update_status=3;
 /* A partial file is keyed by its manifest SHA, not by a changing 'latest' URL. */
 char prior[65]="";FILE *f=fopen(STAGE ".sha","r");if(f){fscanf(f,"%64s",prior);fclose(f);}
 if(strcmp(prior,update_manifest.sha256)){remove(STAGE);f=fopen(STAGE ".sha","w");if(!f)goto done;
  bool wrote=fprintf(f,"%s",update_manifest.sha256)==64;if(fclose(f) || !wrote)goto done;}
 if(!update_space((uint64_t)update_manifest.size*3+4u*1024u*1024u)){log_printf("update: not enough free space for %lu bytes\n",(unsigned long)update_manifest.size);goto done;}
 log_printf("update: manifest version=%u size=%lu installed=%u\n",update_manifest.version,(unsigned long)update_manifest.size,update_installed_version);
 update_status=2;update_machine.state=BB_UPDATE_DOWNLOAD;
 done:if(update_cancelled())__atomic_store_n(&update_transport_deferred,1,__ATOMIC_RELEASE);
 else if(update_status==2 || update_status==7)update_check_failures=0;else if(update_check_failures<3)update_check_failures++;
 __atomic_store_n(&update_job_busy,0,__ATOMIC_RELEASE);
}
static bool update_download(void *unused)
{
 (void)unused;struct stat st;size_t offset=stat(STAGE,&st)?0:(size_t)st.st_size;
 if(offset>update_manifest.size){remove(STAGE);offset=0;}
 if(offset==update_manifest.size)return true;
 /* One bounded range per queue turn; let scans and visible reads run between chunks. */
 size_t n=update_manifest.size-offset;if(n>262144)n=262144;
 char range[64];snprintf(range,sizeof(range),"%lu-%lu",(unsigned long)offset,(unsigned long)(offset+n-1));
 FILE *f=fopen(STAGE,offset?"ab":"wb");if(!f)return false;
 __atomic_store_n(&update_dl_bytes,offset,__ATOMIC_RELAXED);
 sink s={.file=f,.limit=n,.base=offset};long code=0;
 bool ok=update_fetch(update_manifest.url,&s,range,&code);int close=fclose(f);
 /* If Range was ignored, discard; never append an entire response to a prefix. */
 if(code!=206){
  if(code==416){struct stat complete;return !stat(STAGE,&complete) && (size_t)complete.st_size==update_manifest.size;}
  if(code!=200)return false;
  remove(STAGE);
  /* Static hosts can ignore Range. Start a fresh bounded full GET; its manifest
   * size/hash still gate AM. Servers with Range retain chunked resume. */
  f=fopen(STAGE,"wb");if(!f)return false;
  __atomic_store_n(&update_dl_bytes,0,__ATOMIC_RELAXED);
  s=(sink){.file=f,.limit=update_manifest.size};code=0;
  ok=update_fetch(update_manifest.url,&s,NULL,&code);close=fclose(f);
  return ok && code==200 && !close && s.used==update_manifest.size;
 }
 if(!ok || close || s.used!=n)return false;
 return true;
}
static bool update_hash_cancelled(void *unused)
{if(unused && bb_update_installing()){size_t *hashed=unused;__atomic_store_n(&update_progress,(unsigned)((uint64_t)*hashed*9/update_manifest.size),__ATOMIC_RELEASE);if(*hashed<update_manifest.size)*hashed+=16384;}return bb_update_installing()?(__atomic_load_n(&update_install_abort,__ATOMIC_ACQUIRE) || aptShouldClose() || !gspHasGpuRight()):update_cancelled();}
static bool update_verify(void *unused)
{(void)unused;size_t hashed=0;bb_update_hash_result result=bb_update_hash_check_cancel(STAGE,&update_manifest,update_hash_cancelled,&hashed);
 if(result==BB_UPDATE_HASH_IO){update_status=9;return false;}
 bool ok=result==BB_UPDATE_HASH_OK;if(!ok){
 remove(STAGE);update_status=5;
 if(strcmp(update_failed_sha,update_manifest.sha256)){strcpy(update_failed_sha,update_manifest.sha256);update_failed_count=0;}
 update_failed_count++;update_failed_time=osGetTime();
 FILE *f=safe_begin(BACKOFF);if(f){bool wrote=fprintf(f,"%s %u %llu\n",update_failed_sha,update_failed_count,(unsigned long long)update_failed_time)>0;safe_finish(f,BACKOFF,wrote);}
 log_printf("update: hash failed count=%u\n",update_failed_count);
 }return ok;}
static bool update_flush(void *unused)
{
 (void)unused;if(bb_session_active() || aptShouldClose() || !gspHasGpuRight())return false;
 if(!covers_stop())return false;
 playlog_flush();
 /* A save writer that does not finish keeps its file ownership: abandon the install. */
 u64 start=update_now();
 while(!bb_save_settle_timeout(UPDATE_NS(50))){
  if(__atomic_load_n(&update_install_abort,__ATOMIC_ACQUIRE) || update_now()-start>=UPDATE_SAVE_MS){log_printf("update: save writer did not finish\n");return false;}
  svcSleepThread(1000000);
 }
 config_save();
 return !bb_save_final_failed() && !playlog_pending() && config_last_save_ok();
}
static bool update_install(void *unused)
{
 (void)unused;bool ok=false,started=false;Handle source=0,dest=0;
 strcpy(update_failure_reason,"Install preflight or second SHA failed");
 if(bb_session_active() || aptShouldClose() || !gspHasGpuRight() || !update_power_ready())return false;
 bool verified=update_verify(NULL);bb_update_record(BB_UP_SECOND_SHA,verified);
 if(!verified || !update_title_version() ||
 !bb_update_newer(BB_UPDATE_TITLE,update_manifest.version,update_installed_version) || !update_space((uint64_t)update_manifest.size*2+2097152) || R_FAILED(amInit()))return false;
 unsigned char *buf=malloc(16384);AM_TitleInfo info;
 if(!buf){strcpy(update_failure_reason,"Not enough memory");goto done;}
 strcpy(update_failure_reason,"CIA identity or SD read failed");
 if(R_FAILED(FSUSER_OpenFileDirectly(&source,ARCHIVE_SDMC,fsMakePath(PATH_EMPTY,""),fsMakePath(PATH_ASCII,APP_SD_PATH "/updates/BreadBox.cia.part"),FS_OPEN_READ,0)) ||
 R_FAILED(AM_GetCiaFileInfo(MEDIATYPE_SD,&info,source)) || info.titleID!=BB_UPDATE_TITLE || info.version!=update_manifest.version ||
 !bb_update_newer(info.titleID,info.version,update_installed_version))goto done;
 if(!update_recover_pending())goto done;
 strcpy(update_failure_reason,"Could not persist update receipt");
 /* Write receipt before mutation; only the new installed version displays it. */
 FILE *f=safe_begin(RECEIPT);if(!f)goto done;
 bool wrote=fprintf(f,"%u %s\n",update_manifest.version,update_manifest.display)>0;
 bool receipt=safe_finish(f,RECEIPT,wrote);bb_update_record(BB_UP_RECEIPT,receipt);
 if(!receipt)goto done;
 if(!update_power_ready())goto done;
 strcpy(update_failure_reason,"AM start failed");
 Result rc=AM_StartCiaInstall(MEDIATYPE_SD,&dest);
 bb_update_record(BB_UP_AM_START,(uint32_t)rc);
 if(R_FAILED(rc))goto done;
 started=true;strcpy(update_failure_reason,"AM write or SD read failed");
 u32 offset=0;
 for(;offset<update_manifest.size;){u32 n=update_manifest.size-offset,read=0,written=0;if(n>16384)n=16384;
  if(update_hash_cancelled(NULL) || R_FAILED(FSFILE_Read(source,&read,offset,buf,n)) || read!=n ||
  R_FAILED(FSFILE_Write(dest,&written,offset,buf,n,0)) || written!=n)goto done;
  offset+=n;__atomic_store_n(&update_progress,10u+(unsigned)((uint64_t)offset*89/update_manifest.size),__ATOMIC_RELEASE);
  svcSleepThread(1000000);
 }
 /* Journal first (main may not draw or acknowledge until this is on the card), then
  * ask main to drain the GPU. Main answers between frames; both sides are bounded. */
 bb_update_record(BB_UP_WRITES_DONE,offset);
 __atomic_store_n(&update_finish_ready,1,__ATOMIC_RELEASE);
 u64 waited=update_now();
 while(!__atomic_load_n(&update_finish_allowed,__ATOMIC_ACQUIRE)){
  if(update_hash_cancelled(NULL))goto done;
  /* Abort first, so main stops acknowledging (and journaling) an install that is being cancelled. */
  if(update_now()-waited>UPDATE_ACK_WORKER_MS){__atomic_store_n(&update_install_abort,1,__ATOMIC_RELEASE);strcpy(update_failure_reason,"GPU handoff timed out");goto done;}
  svcSleepThread(1000000);
 }
 strcpy(update_failure_reason,"AM commit failed");
 if(update_hash_cancelled(NULL))goto done;
 rc=AM_FinishCiaInstall(dest);
 update_commit_time=update_now();
 bb_update_record(BB_UP_FINISH,(uint32_t)rc);
 if(R_FAILED(rc))goto done;
 started=false;ok=true;__atomic_store_n(&update_commit,1,__ATOMIC_RELEASE);
 done:if(started)AM_CancelCIAInstall(dest);if(source)FSFILE_Close(source);amExit();free(buf);
 bb_update_record(BB_UP_HANDLES_CLOSED,ok);
 if(!ok){log_printf("update: install version=%u failed\n",update_manifest.version);remove(RECEIPT);}return ok;
}
static const bb_update_ops update_ops={update_download,update_verify,update_flush,update_install,NULL};
static void update_stage_job(void *unused)
{
 (void)unused;
 /* Only the staging phases: a job queued from a stale phase read must never flush or install. */
 bb_update_state phase=update_machine.state;
 if(phase!=BB_UPDATE_DOWNLOAD && phase!=BB_UPDATE_VERIFY){log_printf("update: staging refused in phase %d\n",(int)phase);__atomic_store_n(&update_job_busy,0,__ATOMIC_RELEASE);return;}
 if(phase==BB_UPDATE_DOWNLOAD){
  if(!update_download(NULL)){if(update_cancelled()){__atomic_store_n(&update_transport_deferred,2,__ATOMIC_RELEASE);update_machine.state=BB_UPDATE_DOWNLOAD;}else{update_status=3;update_machine.state=BB_UPDATE_FAILED;}}
  else {struct stat st;if(!stat(STAGE,&st) && (size_t)st.st_size==update_manifest.size)update_machine.state=BB_UPDATE_VERIFY;}
 }else {bb_update_step(&update_machine,&update_ops,!update_cancelled());
  if(update_machine.state==BB_UPDATE_FAILED && update_cancelled()){update_machine.state=BB_UPDATE_VERIFY;__atomic_store_n(&update_transport_deferred,2,__ATOMIC_RELEASE);}}
 __atomic_store_n(&update_job_busy,0,__ATOMIC_RELEASE);
}
static bool update_enqueue(void (*fn)(void *))
{
 __atomic_fetch_and(&update_net_busy,1,__ATOMIC_ACQ_REL);
 __atomic_store_n(&update_net_abort,0,__ATOMIC_RELEASE);
 __atomic_store_n(&update_job_busy,1,__ATOMIC_RELEASE);
 if(bb_index_background_start() && bb_index_defer_low(fn,NULL))return true;
 __atomic_store_n(&update_job_busy,0,__ATOMIC_RELEASE);return false;
}
/* Before AM, cancellation/deadline may stop waiting but cannot free a live
 * consumer. An unresponsive consumer selects the shell's process-only exit. */
static bool update_prepare(void)
{
 u64 start=update_now();bool covers=false;
 for(;;){
  if(update_now()-start>=UPDATE_PREP_MS){
   if(!covers || !bb_index_shutdown_done())bb_exit_abandon();
   strcpy(update_failure_reason,"Update preparation interrupted");return false;
  }
  if(!covers){
   covers=covers_stop();
   if(covers)bb_index_shutdown();
  }
  if(covers && bb_index_shutdown_done()){
   if(__atomic_load_n(&update_install_abort,__ATOMIC_ACQUIRE)){
    strcpy(update_failure_reason,"Update preparation interrupted");return false;
   }
   return true;
  }
  svcSleepThread(1000000);
 }
}
static void update_install_job(void *unused)
{
 (void)unused;bool exclusive=false;char previous_reason[sizeof(update_failure_reason)];strcpy(previous_reason,update_failure_reason);
 /* Import retirement needs the index worker until all handles are closed. */
 bool ok=update_prepare();
 if(ok)update_network_close();
 if(ok && !__atomic_load_n(&update_install_abort,__ATOMIC_ACQUIRE))ok=update_flush(NULL);
 else ok=false;
 bb_update_record(BB_UP_FLUSH,ok);
 strcpy(update_failure_reason,"Save or configuration flush failed");
 if(ok && !__atomic_load_n(&update_install_abort,__ATOMIC_ACQUIRE)){
  /* A writer thread that will not stop leaves the log (and the install) as they were. */
  u64 start=update_now();bool closed=false;
  while(!(closed=log_close_timeout(UPDATE_NS(50)))){
   if(__atomic_load_n(&update_install_abort,__ATOMIC_ACQUIRE) || update_now()-start>=UPDATE_LOG_MS)break;
   svcSleepThread(1000000);
  }
  bb_update_record(BB_UP_LOG_CLOSE,closed);
  if(!closed){ok=false;strcpy(update_failure_reason,"Log writer did not stop");}
  else if(!bb_writers_try_exclusive()){strcpy(update_failure_reason,previous_reason);update_install_attempted=false;update_machine.state=BB_UPDATE_READY;__atomic_store_n(&update_install_busy,0,__ATOMIC_RELEASE);return;}
  else exclusive=true;
 }
 if(__atomic_load_n(&update_install_abort,__ATOMIC_ACQUIRE))ok=false;
 if(ok)ok=update_install(NULL);
 if(!ok && exclusive)bb_writers_release_exclusive();
 if(!ok){update_status=__atomic_load_n(&update_install_abort,__ATOMIC_ACQUIRE)?15:6;if(update_status==15)strcpy(update_failure_reason,"Update interrupted; retry next start");bb_update_record(BB_UP_FAILURE,update_status);update_save_status();update_machine.state=BB_UPDATE_FAILED;}
 else update_machine.state=BB_UPDATE_RELAUNCH;
 __atomic_store_n(&update_install_busy,0,__ATOMIC_RELEASE);
}
bool bb_update_installing(void){return __atomic_load_n(&update_install_busy,__ATOMIC_ACQUIRE)!=0;}
bool bb_update_committed(void){return __atomic_load_n(&update_commit,__ATOMIC_ACQUIRE)!=0;}
bool bb_update_shutdown_timeout(u64 timeout)
{
 if(update_thread){if(threadJoin(update_thread,timeout)!=0)return false;threadFree(update_thread);update_thread=NULL;}
 bb_update_net_stop();return true;
}
void bb_update_shutdown(void)
{(void)bb_update_shutdown_timeout(U64_MAX);}
/* Successful install, main thread, after every service and worker is down. Journals
 * the choice (the jump itself happens in aptExit, after main returns), waits out the
 * settle time since AM's commit, and requests the relaunch only when all of this
 * holds: every teardown step finished in time, the system is not closing us, and we
 * still own the GPU. Otherwise clear the chainloader and let APT return to HOME. */
bool bb_update_handoff(bool clean)
{
 bool relaunch=BB_UPDATE_HANDOFF && clean && !aptShouldClose() && gspHasGpuRight();
 while(relaunch && update_now()-update_commit_time<UPDATE_SETTLE_MS){
  svcSleepThread(10000000);
  relaunch=!aptShouldClose() && gspHasGpuRight(); /* only a system close order or a lost GPU ends the wait: HOME is not polled here */
 }
 if(relaunch){
  bb_update_record(BB_UP_CHAINLOAD,BB_UPDATE_HANDOFF);
#if BB_UPDATE_HANDOFF==2
  aptSetChainloader(BB_UPDATE_TITLE,MEDIATYPE_SD);
#else
  aptSetChainloaderToSelf(); /* consumed by aptExit */
#endif
 }else{
  bb_update_record(BB_UP_HOME_RETURN,clean);
  aptClearChainloader();
 }
 return relaunch;
}
static void update_apt(APT_HookType type,void *unused)
{
 (void)unused;
 if(type==APTHOOK_ONWAKEUP || type==APTHOOK_ONRESTORE)__atomic_store_n(&update_wake,1,__ATOMIC_RELEASE);
 if(type==APTHOOK_ONSUSPEND || type==APTHOOK_ONSLEEP || type==APTHOOK_ONEXIT){
  /* After the commit there is nothing left to cancel. Main has already joined the
   * installer or given up on it (bounded), and its tail may be stuck with busy still
   * set: waiting for it here would be the unbounded wait the join timeout removed. */
  if(type==APTHOOK_ONEXIT && bb_update_committed())return;
  __atomic_store_n(&update_install_abort,1,__ATOMIC_RELEASE);
  /* Worker cancellation closes the staging/AM handles before APT hands off SD. */
  u64 start=update_now();
  while(bb_update_installing()){
   if(update_now()-start>=UPDATE_CANCEL_MS){bb_exit_abandon();break;}
   svcSleepThread(1000000);
  }
 }
}
const char *bb_update_preference(void){return (const char *const[]){"Automatic","Ask first","Off"}[__atomic_load_n(&update_preference,__ATOMIC_ACQUIRE)];}
/* An explicit Check now: the room's notice and the Updates card say what happens (owner 2026-11-05: it
 * showed nothing). Checking, then the confirm dialog as soon as an update is found (before any download),
 * then "Downloading update NN%", or "BreadBox is up to date" / the failure. */
static bool update_explicit_pending;
static bool update_developer_busy(void)
{
 /* Explicit private hardware fixture: permit diagnostics, never benchmarks.
  * Packaging rejects this build outside cia-development via the dev URL. */
#ifdef BB_UPDATE_TEST_REMOTE
 return dev_bench;
#else
 return dev_enabled();
#endif
}
/* The UI's own copy of the worker's results. The worker writes the phase and the manifest while a job runs;
 * the UI reads them only after an acquire load of update_job_busy has seen the worker idle (update_poll),
 * and drawing reads only this copy (and the atomic byte counter). */
typedef struct {bb_update_state state;size_t size;bool staging;char display[sizeof(update_manifest.display)];} update_view_t;
static update_view_t update_view;
static void update_view_refresh(void)
{
 update_view.state=update_machine.state;update_view.size=update_manifest.size;
 snprintf(update_view.display,sizeof(update_view.display),"%s",update_manifest.display);
}
/* Check now stays the player's check until a discovery really finishes (J-04): it waits for the
 * library scan, and a discovery the scan cancels is retried as the same explicit check, so its
 * result is always asked about, never turned into an automatic download. */
static bool update_explicit_retry;
static bool update_check_start(void)
{
 bool explicit=update_check_now || update_explicit_retry;
 if(sw_nonet || sw_nocovers){if(explicit){update_check_now=update_explicit_retry=update_explicit_pending=false;}return false;}
 if(!bb_index_complete() && bb_index_busy())return false;
 update_explicit_check=explicit;update_approved=false;update_machine.state=BB_UPDATE_IDLE;update_view.staging=false;
 if(!update_enqueue(update_discover_job))return false;
 update_status=1;update_last_check=update_now();update_checked=true;update_explicit_pending=explicit;update_check_now=update_explicit_retry=false;return true;
}
static void update_say(const char *text,unsigned ms){snprintf(update_notice,sizeof(update_notice),"%s",text);update_notice_until=osGetTime()+ms;}
static const char *update_settings_status(void);
/* After an explicit check's discovery (worker idle, acquired): ask now, or say the result. False: Later. */
static bool update_explicit_result(void)
{
 if(!update_explicit_pending)return true;
 update_explicit_pending=false;
 if(update_view.state==BB_UPDATE_DOWNLOAD){
  char text[160];snprintf(text,sizeof(text),"BreadBox %s is ready to download and install. Your saved games will be kept.",update_view.display);
  if(!ui_confirm_choice("Update available",text,"A  Update","B  Later",false)){update_machine.state=BB_UPDATE_IDLE;update_view.state=BB_UPDATE_IDLE;update_explicit_check=false;return false;}
  update_approved=true;return true;
 }
 update_say(update_status==7?"BreadBox is up to date":update_settings_status(),4000);return true;
}
static unsigned update_download_percent(void)
{
 size_t size=update_view.size,done=__atomic_load_n(&update_dl_bytes,__ATOMIC_RELAXED);
 return size?(unsigned)((uint64_t)(done<size?done:size)*100/size):0;
}
/* A download the player has agreed to (explicit: accepted the dialog) or that Automatic / Ask first makes. */
static bool update_download_wanted(void)
{return update_explicit_check?update_approved:__atomic_load_n(&update_preference,__ATOMIC_ACQUIRE)!=2;}
enum {UPDATE_POLL_WAIT,UPDATE_POLL_READY};
/* One pre-install policy for the room's tick and the Updates card: worker completion first (acquire), then
 * its phase; transport deferral and retry delay; discovery; the explicit answer; staging. READY is returned
 * to the room only: installing never starts from Settings. */
static int update_poll(bool room)
{
 if(bb_session_active() || update_developer_busy() || __atomic_load_n(&update_job_busy,__ATOMIC_ACQUIRE))return UPDATE_POLL_WAIT;
 update_view_refresh();
 if(!update_initialized || update_crypto_disabled)return UPDATE_POLL_WAIT;
 u64 now=update_now(),elapsed=update_elapsed(now,&update_last_check);
 unsigned deferred=__atomic_exchange_n(&update_transport_deferred,0,__ATOMIC_ACQ_REL);
 if(deferred){update_retry_after=now+5000;if(deferred==1){update_checked=false;if(update_explicit_pending)update_explicit_retry=true;}}
 if(now<update_retry_after && !update_check_now)return UPDATE_POLL_WAIT;
 if(room){
  bool wake=__atomic_exchange_n(&update_wake,0,__ATOMIC_ACQ_REL);
  if(wake){update_idle_since=now;update_countdown_since=0;}
  u64 minimum=update_check_failures>=3?86400000:update_check_failures==2?21600000:3600000;
  if((wake && elapsed>=minimum) || (update_last_check && elapsed>=86400000))update_checked=false;
  if(input_keys_held()){update_idle_since=now;if(update_countdown_since){update_postponed=true;update_countdown_since=0;}}
  if(!update_idle_since)update_idle_since=now;
 }
 if((!update_checked && __atomic_load_n(&update_preference,__ATOMIC_ACQUIRE)!=2) || update_check_now || update_explicit_retry){update_check_start();return UPDATE_POLL_WAIT;}
 if(!update_explicit_result())return UPDATE_POLL_WAIT;
 if(update_view.state==BB_UPDATE_DOWNLOAD || update_view.state==BB_UPDATE_VERIFY){
  if(update_download_wanted() && update_enqueue(update_stage_job))update_view.staging=true;
  return UPDATE_POLL_WAIT;
 }
 return room && update_view.state==BB_UPDATE_READY?UPDATE_POLL_READY:UPDATE_POLL_WAIT;
}
bool bb_update_tick(void)
{
 if(bb_exit_abandoned())return true;
 if(bb_update_committed())return true;
 if(bb_update_installing())return false;
 /* A release of busy may race the first commit check. Recheck before any IO. */
 if(bb_update_committed())return true;
 if(update_thread){bb_update_shutdown();bb_index_background_start();if(update_restart_root[0])bb_index_start(update_restart_root,false);log_resume();log_writer_start(g_new3ds?2:0);covers_resume();music_play(true);}
 if(bb_session_active() || update_developer_busy() || dev_covers_blocked() || !aptMainLoop() || !gspHasGpuRight() || __atomic_load_n(&update_job_busy,__ATOMIC_ACQUIRE))return false;
 if(!update_initialized){if(update_enqueue(update_initialize_job)){update_initialized=true;aptHook(&update_apt_cookie,update_apt,NULL);}return false;}
 if(update_crypto_disabled)return false;
 if(update_notice[0] && !update_notice_until)update_notice_until=osGetTime()+5000;
 if(update_poll(true)==UPDATE_POLL_READY){
  u64 now=update_now();
  if(update_install_attempted || update_postponed || bb_save_final_failed())return false;
  if(!update_power_checked || update_elapsed(now,&update_power_checked)>=5000){update_power_cached=update_power_ready();update_power_checked=now;}
  if(!update_power_cached){update_status=10;return false;}
  if(update_preference==2 && !update_explicit_check)return false;
  if(!update_approved && (update_preference==1 || update_explicit_check) && !ui_confirm_choice("Update available","BreadBox can update now. Your saved games will be kept.","A  Update","B  Later",false)){update_machine.state=BB_UPDATE_IDLE;return false;}
  if(!update_approved && update_preference==0 && !update_explicit_check){
   if(update_elapsed(now,&update_idle_since)<5000)return false;
   if(!update_countdown_since)update_countdown_since=now;
   u64 countdown=update_elapsed(now,&update_countdown_since);
   if(countdown<3000){snprintf(update_notice,sizeof(update_notice),"Updating BreadBox in %u... Any button: later",3-(unsigned)(countdown/1000));update_notice_until=osGetTime()+100;return false;}
  }
  update_approved=true;
  /* covers_stop cancels LOW import work. Let a reserved writer finish before
   * pausing consumers; retain the second check for a writer starting meanwhile. */
  if(bb_writers_busy())return false;
  if(!covers_stop() || bb_writers_busy() || bb_save_final_failed())return false;
  snprintf(update_restart_root,sizeof(update_restart_root),"%s",bb_index_root());
  /* Worker drains consumers while main displays the progress card. */
  music_play(false);
  __atomic_store_n(&update_progress,0,__ATOMIC_RELEASE);
  __atomic_store_n(&update_install_busy,1,__ATOMIC_RELEASE);
  __atomic_store_n(&update_install_abort,0,__ATOMIC_RELEASE);
  __atomic_store_n(&update_finish_ready,0,__ATOMIC_RELEASE);
  __atomic_store_n(&update_finish_allowed,0,__ATOMIC_RELEASE);
  update_ack_since=0;
  update_machine.state=BB_UPDATE_INSTALL;
  update_install_attempted=true;
  update_thread=threadCreate(update_install_job,NULL,96*1024,0x31,g_new3ds?2:0,false);
  if(!update_thread){__atomic_store_n(&update_install_busy,0,__ATOMIC_RELEASE);update_status=6;strcpy(update_failure_reason,"Could not start update worker");update_save_status();update_machine.state=BB_UPDATE_FAILED;}
 }
 if(!__atomic_load_n(&update_job_busy,__ATOMIC_ACQUIRE) && update_machine.state==BB_UPDATE_FAILED){covers_resume();music_play(true);update_machine.state=BB_UPDATE_IDLE;update_view.state=BB_UPDATE_IDLE;update_view.staging=false;}
 return bb_update_committed();
}
/* Progress text from the UI's copy only: a staging attempt the player wanted, never before the explicit OK. */
static bool update_progress_text(char *text,size_t cap)
{
 if(!update_view.staging || !update_download_wanted())return false;
 if(update_view.state==BB_UPDATE_VERIFY)snprintf(text,cap,"Checking the update");
 else if(update_view.state==BB_UPDATE_DOWNLOAD)snprintf(text,cap,"Downloading update %u%%",update_download_percent());
 else return false;
 return true;
}
void bb_update_draw_notice(void)
{
 char text[48];
 if(update_notice_until && osGetTime()<update_notice_until){ui_notice(update_notice,60);return;}
 if(update_explicit_pending){ui_notice("Checking for updates",60);return;}
 if(update_progress_text(text,sizeof(text)))ui_notice(text,60);
}
/* A retained final save can outlive the player's Later choice. Keep consent/staging,
 * disclose the blocker, and let the next eligible room tick retry after retry/discard. */
static bool update_waits_for_save(void)
{return update_view.state==BB_UPDATE_READY && bb_save_final_failed();}
static char update_card_line[64];
static bool update_line_bad;   /* the line update_settings_status returned last is a problem (red) */
static const char *update_settings_status(void)
{
 update_line_bad=false;
 if(update_explicit_pending)return "Checking for updates..."; /* only a check really queued (a disabled one says why) */
 if(update_explicit_check && update_progress_text(update_card_line,sizeof(update_card_line)))return update_card_line;
 if(update_waits_for_save()){update_line_bad=true;return "Waiting: save not written";}
 if(update_view.state==BB_UPDATE_READY && update_explicit_check && update_approved)return "Leave Settings to install";
 unsigned status=update_status;
 /* Keep the last install phase visible across subsequent background checks.
  * Current power, integrity, TLS and disabled states take priority. */
 if(update_failure_reason[0] && (status<=3 || status==6 || status==7 || status==16)){update_line_bad=true;return "Last install didn't finish";}
 update_line_bad=!update_status_good(status);
 return update_status_text[status];
}
/* The room's Settings card (place_cards.h): the updater's state in two words, NULL when nothing is known yet.
 * UI thread; reads the UI's copy and, with the worker idle, the status. A background re-check keeps the last
 * answer (no line that comes and goes); a check that failed says nothing here (Settings > Updates says why). */
static const char *update_glance_last;
const char *bb_update_glance(void)
{
 if(__atomic_load_n(&update_preference,__ATOMIC_ACQUIRE)==2)return "Updates off";
 if(update_crypto_disabled)return NULL;
 if(__atomic_load_n(&update_job_busy,__ATOMIC_ACQUIRE) || update_status==1)return update_glance_last;
 update_glance_last=update_view.state==BB_UPDATE_READY?"Update ready":
  update_view.state==BB_UPDATE_DOWNLOAD || update_view.state==BB_UPDATE_VERIFY?"Update found":update_status==7?"Up to date":NULL;
 return update_glance_last;
}
static flist_t update_list;static int update_selected;
static void update_row(int i,void *unused,ui_card_row_t *r)
{(void)unused;*r=(ui_card_row_t){.label=i?"Check now":"Install",.value=i?">":bb_update_preference(),.arrows=!i};}   /* not "Updates" again under the title */
/* The card's foot line: what this visit's check is doing or found; no standing "Last check" status (owner 2026-11-05) */
static const char *update_card_notice(void)
{return update_waits_for_save() || update_explicit_pending || update_explicit_check || update_view.staging?update_settings_status():"";}
/* The card's footer: drawn and touched from the same list (a tap on Back or Check works like B or A). */
static int update_bar(ui_button_t *bar)
{bar[0]=(ui_button_t){KEY_B,"Back",UI_ICON_BACK,false};bar[1]=(ui_button_t){KEY_A,update_selected?"Check":"Change",UI_ICON_CHANGE,false};return 2;}
static void update_bottom(void *unused);

void bb_update_settings(void)
{
 /* Called only from the app Settings update_row, never while a game is active. */
 if(bb_session_active() || !__atomic_load_n(&update_prefs_loaded,__ATOMIC_ACQUIRE))return;
 update_selected=0;update_list=(flist_t){0};ui_swap_enter();
 while(aptMainLoop()){
  if(dev_tick(false) || dev_ui_pending())break;
  ui_button_t bar[2];int nbar=update_bar(bar);
  input_poll();u32 k=ui_card_input(&update_list,2,&update_selected,ui_bar_input(bar,nbar,input_keys_down()));k=ui_swap_keys(k,UI_SWAP_MENU);
  if(k&KEY_B)break;
  if(!update_selected && (k&(KEY_A|KEY_LEFT|KEY_RIGHT))){int next=(update_preference+((k&KEY_LEFT)?2:1))%3;
   FILE *f=safe_begin(PREF);if(f){bool wrote=fprintf(f,"%d\n",next)>0;if(safe_finish(f,PREF,wrote)){__atomic_store_n(&update_preference,next,__ATOMIC_RELEASE);if(next==2)bb_update_net_stop();sfx_play(SFX_VALUE);}}
   if(__atomic_load_n(&update_preference,__ATOMIC_ACQUIRE)!=next)sfx_play(SFX_ERROR);   /* could not be written: it stays as it was */
  }else if(update_selected && (k&KEY_A) && !update_explicit_pending){update_check_now=update_explicit_pending=true;}
  else if(k&KEY_A)sfx_cancel();   /* a check is already running: nothing new */   /* "Checking..." at once, even while the library scans */
  /* the room's tick is not running: the same policy runs here, never past READY */
  update_poll(false);
  ui_card_update(&update_list,update_selected,2);music_update();ui_begin();video_present_vsync(false,ui_menu_top,update_bottom,NULL);
 }
 ui_swap_enter();
}

static void update_progress_bottom(void *unused);
void bb_update_install_frame(void)
{
 /* Main acknowledges only between completed frames. From this point even the
  * progress font must stay untouched until AM has failed or we chainload. */
 if(__atomic_load_n(&update_finish_ready,__ATOMIC_ACQUIRE)){
  if(!__atomic_load_n(&update_finish_allowed,__ATOMIC_ACQUIRE) && !__atomic_load_n(&update_install_abort,__ATOMIC_ACQUIRE)){
   if(!gspHasGpuRight()){__atomic_store_n(&update_install_abort,1,__ATOMIC_RELEASE);return;}
   /* The last progress frame must have left the GPU before the title is replaced.
    * Poll the existing queue (no empty frames, no unbounded wait); if it never
    * drains, abort before AM_FinishCiaInstall and cancel the import. */
   u64 now=update_now();if(!update_ack_since)update_ack_since=now;
   if(!video_gpu_poll_idle()){
    if(now-update_ack_since>UPDATE_ACK_MAIN_MS){bb_update_record(BB_UP_GPU_ACK,0);__atomic_store_n(&update_install_abort,1,__ATOMIC_RELEASE);}
    svcSleepThread(1000000);return;
   }
   bb_update_record(BB_UP_GPU_ACK,1);
   __atomic_store_n(&update_finish_allowed,1,__ATOMIC_RELEASE);
  }
  svcSleepThread(1000000);return;
 }
 if(!gspHasGpuRight()){svcSleepThread(1000000);return;}
 input_poll();ui_begin();video_present_vsync(false,update_progress_bottom,update_progress_bottom,NULL);
}

#include "components/update.inc"
