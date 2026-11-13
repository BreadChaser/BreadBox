/* Real core + production host/save services. GPL-2.0-or-later. */
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "retro_host.h"
#include "bb_save.h"
#include "bb_registry.h"
#include "bb_arena.h"
#include "history.h"
#include "app_policy.h"
static bb_retro_host host;
static bb_retro_core core;
static uint64_t tick;
static uint32_t pixels,pcm;
static unsigned pictures,nonzero;
static uint64_t audio_nonzero;
static bb_system_t test_system;
const bb_system_t *const bb_systems[]={&test_system};
const unsigned bb_system_count=1;
static void *loan;
static bb_arena_t arena;
bool bb_exec_held(void){return loan!=NULL;}
bool bb_platform_budget(const bb_system_t *s){return s==&test_system;}
bool bb_platform_claim(const bb_system_t *s){assert(s==&test_system && !loan);loan=malloc(4096);return loan && bb_arena_claim(&arena,(uintptr_t)loan,4096,(uintptr_t)loan);}
void bb_platform_drop(void){assert(loan);bb_arena_drop(&arena);free(loan);loan=NULL;}
void bb_platform_release_menu(void){}
void bb_platform_restore_menu(void){}
int bb_platform_count(const bb_system_t *s){(void)s;return 0;}
static void session_begin(void){bb_launch l={0};assert(bb_session_begin(&test_system,&l));assert(bb_session_system()==&test_system && bb_exec_held());}
static void session_end(void){bb_session_end();assert(!bb_session_active() && !bb_exec_held() && !arena.held);}

bool breadbox_import_busy(void){return false;}
void log_printf(const char *fmt,...){(void)fmt;}
static uint64_t ticks(void){return tick;}
static bool picture(const void *p,unsigned w,unsigned h,size_t pitch,enum retro_pixel_format fmt){
 assert(p && w && h && pitch>=w*2);pictures++;
 unsigned bytes=fmt==RETRO_PIXEL_FORMAT_XRGB8888?4:2;
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w*bytes;x++){
  unsigned v=((const unsigned char *)p)[y*pitch+x];pixels=(pixels^v)*16777619u;nonzero+=v!=0;
 }return true;
}
static void audio(const int16_t *p,size_t n){for(size_t i=0;i<n*2;i++){pcm=(pcm^(uint16_t)p[i])*16777619u;audio_nonzero+=p[i]!=0;}}
static void frames(unsigned n){for(unsigned i=0;i<n;i++){tick=host.clock.next;assert(bb_retro_step(&host,0));bb_retro_pace(&host);}}
static void hashes(void){pixels=pcm=2166136261u;pictures=nonzero=0;audio_nonzero=0;}
static size_t state_size(void){return core.serialize_size();}
static bool state_save(void *p,size_t n,size_t *len){return bb_retro_state_save(&host,p,n,len);}
static bb_state_result state_load(const void *p,size_t n,bool keep){return bb_retro_state_load(&host,p,n,keep);}
static bool battery(void **p,size_t *n){return bb_retro_battery(&host,p,n);}
static const bb_save_ops saves={.state_size=state_size,.state_save=state_save,.state_load=state_load,.battery=battery};
#define BIND(n) do{*(void **)(&core.n)=dlsym(lib,"retro_" #n);assert(core.n);}while(0)
int main(int argc,char **argv){
 assert(argc==5);void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!lib){fprintf(stderr,"%s\n",dlerror());abort();}
 BIND(set_environment);BIND(set_video_refresh);BIND(set_audio_sample);BIND(set_audio_sample_batch);BIND(set_input_poll);BIND(set_input_state);
 BIND(init);BIND(deinit);BIND(run);BIND(reset);BIND(unload_game);BIND(load_game);BIND(get_system_info);BIND(get_system_av_info);
 BIND(get_memory_data);BIND(get_memory_size);BIND(serialize_size);BIND(serialize);BIND(unserialize);
 FILE *f=fopen(argv[2],"rb");assert(f);fseek(f,0,SEEK_END);long len=ftell(f);rewind(f);void *rom=malloc(len);assert(rom && fread(rom,1,len,f)==(size_t)len);fclose(f);
 test_system=(bb_system_t){.abi=BB_SYSTEM_ABI,.id=argv[3],.save=&saves};
 const struct retro_variable vars[]={{"gpsp_bios","builtin"},{"gpsp_boot_mode","game"},{NULL,NULL}};
 bb_retro_desc d={.system=&test_system,.core=&core,.variables=vars,.system_dir=".",.max_width=256,.max_height=240};
 bb_retro_services s={.ticks=ticks,.ticks_per_second=1000000,.video=picture,.audio=audio};
 struct retro_game_info g={.path=argv[2],.data=rom,.size=len};
 assert(!chdir(argv[4]));history_set_root("history");
 char state_base[512];snprintf(state_base,sizeof(state_base),APP_SD_ROOT "/states/%s/cc0",argv[3]);
 bb_save_config cfg={.system=&test_system,.key="cc0",.battery_path="battery.sav",.state_base=state_base,.identity=123,.content_bytes=len,.writer_core=1};
 uint32_t cold_pixels=0,cold_pcm=0;
 for(unsigned run=0;run<2;run++){
  tick=0;session_begin();assert(bb_retro_open(&host,&d,&s,&g));hashes();frames(120);
  fprintf(stderr,"boot %s pictures=%u nonzero=%u audio=%llu nonzero=%llu\n",argv[3],pictures,nonzero,(unsigned long long)host.audio_frames,(unsigned long long)audio_nonzero);
  assert(pictures>100 && nonzero>0 && host.audio_frames>0 && audio_nonzero>0);
  if(run){assert(pixels==cold_pixels && pcm==cold_pcm);}else{cold_pixels=pixels;cold_pcm=pcm;}
  bb_retro_close(&host);assert(!host.loaded && !host.initialized);void *p;size_t n;assert(!battery(&p,&n));session_end();
 }
 session_begin();assert(bb_retro_open(&host,&d,&s,&g));frames(120);char error[192];assert(bb_save_begin(&cfg,error,sizeof(error)));
 void *ram;size_t n;assert(battery(&ram,&n) && n);unsigned char *saved=malloc(n);assert(saved);
 memset(ram,0x59,n);assert(bb_save_flush());memcpy(saved,ram,n);
 /* Verify canonical raw state bytes as well as observable replay. */
 size_t sn=state_size(),written=0;void *sa=malloc(sn),*sb=malloc(sn);assert(sa && sb);
 assert(state_save(sa,sn,&written) && written==sn);frames(3);
 assert(state_load(sa,sn,false)==BB_STATE_OK);
 assert(state_save(sb,sn,&written) && !memcmp(sa,sb,sn));free(sa);free(sb);
 assert(bb_save_state(0,true));assert(bb_save_state(0,false));hashes();frames(30);uint32_t want_pixels=pixels,want_pcm=pcm;assert(battery(&ram,&n) && ((unsigned char *)ram)[0]!=saved[0]);
 assert(bb_save_state(0,false));hashes();frames(30);fprintf(stderr,"replay pixels %08x/%08x PCM %08x/%08x\n",pixels,want_pixels,pcm,want_pcm);assert(pixels==want_pixels && pcm==want_pcm);
 assert(battery(&ram,&n));memcpy(saved,ram,n);assert(bb_save_checkpoint(true));assert(bb_save_state(BB_SLOT_CONTINUE,false));hashes();frames(30);want_pixels=pixels;want_pcm=pcm;
 assert(battery(&ram,&n));memcpy(saved,ram,n);assert(bb_save_end(false));bb_retro_close(&host);session_end(); /* simulated HOME-close: drain before core unload */
 session_begin();assert(bb_retro_open(&host,&d,&s,&g));assert(bb_save_begin(&cfg,error,sizeof(error)));
 assert(battery(&ram,&n)); /* battery was advanced by the test ROM before checkpoint */
 FILE *bf=fopen("battery.sav","rb");assert(bf);unsigned char *disk=malloc(n);assert(disk);assert(fread(disk,1,n,bf)==n);assert(fgetc(bf)==EOF);fclose(bf);assert(!memcmp(disk,saved,n) && !memcmp(ram,saved,n));free(disk);
 bb_content_id id={.key="cc0",.crc=123};uint32_t saved_time=0;assert(bb_save_continue(&test_system,&id,&saved_time) && saved_time);
 assert(bb_save_exists(BB_SLOT_CONTINUE));assert(bb_save_state(BB_SLOT_CONTINUE,false));
 hashes();frames(30);fprintf(stderr,"replay pixels %08x/%08x PCM %08x/%08x\n",pixels,want_pixels,pcm,want_pcm);assert(pixels==want_pixels && pcm==want_pcm);
 assert(bb_save_end(false));bb_retro_close(&host);assert(!host.loaded && !host.initialized && !bb_save_queue());session_end();
 if(!strcmp(argv[3],"nes")){
  session_begin();struct retro_game_info bad={.data="invalid",.size=7};
  assert(!bb_retro_open(&host,&d,&s,&bad));assert(!host.loaded && !host.initialized);session_end();
 }
 free(saved);free(rom);assert(!dlclose(lib));
 printf("%s behavioral PASS: two cold boots x120, nonempty pixels/non-silent PCM, replay x30, disk battery, Continue/reopen x30, teardown\n",argv[3]);
}
