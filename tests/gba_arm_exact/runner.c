/* GPL-2.0-or-later. Exact-output host driver for the real ARM gpSP core.
 * Freeze only the host wall clock used to seed RTC. Guest cycles are unmodified.
 * Cold loads assert JIT without an artificial variable update.
 * Ordinary BreadBox cold loads start with vars_dirty=false.
 */
#include <libretro.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
extern void retro_profile_read(unsigned long long*);
time_t time(time_t *out) {time_t v=1700000000;if(out)*out=v;return v;}
static int jit;
extern unsigned dynarec_enable;
static unsigned callbacks;
static double frame_start;
extern void retro_set_run_guard(bool (*guard)(void));
static double now(void) {struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return ts.tv_sec+ts.tv_nsec*1e-9;}
static bool guard(void) {return now()-frame_start>2.0;}
static uint32_t video_hash=2166136261u,audio_hash=2166136261u;
static uint64_t samples,nonzero;

#ifdef BASELINE
static void gba_stereo_config(uint16_t *p,unsigned n) {(void)p;(void)n;}
static int gba_stereo_valid(void) {return 0;}
#else
extern void gba_stereo_config(uint16_t*,unsigned);
extern int gba_stereo_valid(void);
#endif
static unsigned distinct,valid,changed;
static uint16_t right[240*160];
static unsigned frame,strength;
static FILE *out,*pcm;
static unsigned keys[30000];
static const char *prefix;
static void log_cb(enum retro_log_level l,const char*f,...) {(void)l;va_list a;va_start(a,f);vfprintf(stderr,f,a);va_end(a);}
static bool env(unsigned cmd,void *p) {
 if(cmd==RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY) {*(const char**)p=getenv("GBA_SYSTEM_DIR")?getenv("GBA_SYSTEM_DIR"):".";return true;}
 if(cmd==RETRO_ENVIRONMENT_GET_LOG_INTERFACE) {((struct retro_log_callback*)p)->log=log_cb;return true;}
 if(cmd==RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) return *(enum retro_pixel_format*)p==RETRO_PIXEL_FORMAT_RGB565;
 if(cmd==RETRO_ENVIRONMENT_GET_VARIABLE) {
  struct retro_variable *v=p;v->value=NULL;
  if(!strcmp(v->key,"gpsp_drc"))v->value=jit?"enabled":"disabled";
  else if(!strcmp(v->key,"gpsp_bios"))v->value=getenv("GBA_OFFICIAL")?"auto":"builtin";
  else if(!strcmp(v->key,"gpsp_sound_rate"))v->value="32768";
  else if(!strcmp(v->key,"gpsp_boot_mode"))v->value="game";
  else if(!strcmp(v->key,"gpsp_frameskip"))v->value="disabled";
  else if(!strcmp(v->key,"gpsp_rtc_time_source"))v->value="deterministic";
  else if(!strcmp(v->key,"gpsp_color_correction"))v->value="disabled";
  else if(!strcmp(v->key,"gpsp_frame_mixing"))v->value="disabled";
  else if(!strcmp(v->key,"gpsp_serial"))v->value="disabled";
  return v->value!=NULL;
 }
 if(cmd==RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE) {*(bool*)p=false;return true;}
 if(cmd==RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE) {*(unsigned*)p=3;return true;}
 if(cmd==RETRO_ENVIRONMENT_GET_CAN_DUPE) {*(bool*)p=true;return true;}
 if(cmd==RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION) {*(unsigned*)p=0;return true;}
 return false;
}
static void video(const void*p,unsigned w,unsigned h,size_t pitch) {
 if(!p)return;
 callbacks++;
 if(getenv("AUDIT")) {
  unsigned differences=0;
  for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
   uint16_t c=((const uint16_t*)((const char*)p+y*pitch))[x];
   video_hash=(video_hash^c)*16777619u;
   if(strength && gba_stereo_valid()) differences+=c!=right[y*240+x];
  }
  distinct+=differences>0;valid+=gba_stereo_valid();changed+=differences;
 }
 if(out && !(frame%(getenv("CAP_EVERY")?(unsigned)atoi(getenv("CAP_EVERY")):4)) && (!getenv("CAP_START") || frame >= (unsigned)atoi(getenv("CAP_START")))) {
   for(unsigned y=0;y<160;y++) fwrite((const char*)p+y*pitch,2,240,out);
   if(strength && gba_stereo_valid()) fwrite(right,2,240*160,out);
   else for(unsigned y=0;y<160;y++) fwrite((const char*)p+y*pitch,2,240,out);
 }
 if(!(frame%60)) printf("frame=%u valid=%d\n",frame,gba_stereo_valid());
}
static size_t audio(const int16_t*p,size_t n) {if(pcm)fwrite(p,4,n,pcm);samples+=n;for(size_t i=0;i<n*2;i++){nonzero+=p[i]!=0;audio_hash=(audio_hash^(uint16_t)p[i])*16777619u;}return n;}
static void sample(int16_t l,int16_t r) {int16_t p[2]={l,r};audio(p,1);}
static void poll(void) {}
static int16_t input(unsigned p,unsigned d,unsigned i,unsigned id) {
 (void)p;(void)d;(void)i;
 return id==256?keys[frame]:((keys[frame]>>id)&1);
}
#ifdef BB_SHARED_HOST
#include "host.h"
#endif
int main(int argc,char **argv) {
 if(argc<7)return 2;if(getenv("AUDIO_OUT"))pcm=fopen(getenv("AUDIO_OUT"),"wb");strength=atoi(argv[4]);prefix=argv[5];if(strcmp(prefix,"-"))out=fopen(prefix,"wb");
 FILE *script=fopen(argv[6],"r");if(script){unsigned a,b,k;while(fscanf(script,"%u %u %x",&a,&b,&k)==3)for(unsigned j=a;j<b&&j<30000;j++)keys[j]=k;fclose(script);}
jit=atoi(argv[1]);unsigned frames=atoi(argv[3]);
#ifdef BB_SHARED_HOST
 host_bind();retro_init();
#else
 retro_set_environment(env);retro_set_video_refresh(video);retro_set_audio_sample_batch(audio);retro_set_audio_sample(sample);retro_set_input_poll(poll);retro_set_input_state(input);retro_init();
#endif
 struct retro_game_info game={.path=argv[2]};if(!retro_load_game(&game))return 3;
#ifdef ARENA_REUSE
 /* Exercise one real GBA JIT session first, release it, then emulate PS1's
  * arena writes before a second GBA claim in the same process. */
 retro_set_run_guard(guard);
 for(frame=0;frame<60;frame++){frame_start=now();
#ifdef BB_SHARED_HOST
 bb_retro_run_frame(&test_host,keys[frame],false);
#else
 retro_run();
#endif
}
 retro_unload_game();retro_deinit();
#ifdef BB_SHARED_HOST
 bb_retro_unbind(&test_host);
#endif
 extern void arena_reuse_after_ps1(void);arena_reuse_after_ps1();
#ifdef BB_SHARED_HOST
 host_bind();retro_init();
#else
 retro_set_environment(env);retro_set_video_refresh(video);retro_set_audio_sample_batch(audio);
 retro_set_audio_sample(sample);retro_set_input_poll(poll);retro_set_input_state(input);retro_init();
#endif
 if(!retro_load_game(&game))return 7;
 video_hash=audio_hash=2166136261u;samples=nonzero=callbacks=0;
 if(out)out=freopen(prefix,"wb",out);
 if(pcm)pcm=freopen(getenv("AUDIO_OUT"),"wb",pcm);
#endif
 if(argc>7 && strcmp(argv[7],"-")) {FILE*f=fopen(argv[7],"rb");if(f){fread(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM),1,retro_get_memory_size(RETRO_MEMORY_SAVE_RAM),f);fclose(f);}}
 if(argc>8 && strcmp(argv[8],"-")) {FILE*f=fopen(argv[8],"rb");if(f){size_t n=retro_serialize_size();void*p=malloc(n);if(fread(p,1,n,f)!=n || !retro_unserialize(p,n))return 5;free(p);fclose(f);}}
 if (!!dynarec_enable != !!jit) {fprintf(stderr,"Wrong cold-load engine\n");return 6;}
 printf("cold_load_engine=%s\n",jit?"ARM dynarec":"interpreter");
 gba_stereo_config(right,strength);
 struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);double start=ts.tv_sec+ts.tv_nsec*1e-9;
 retro_set_run_guard(guard);
 for(frame=0;frame<frames;frame++) {frame_start=now();
#ifdef BB_SHARED_HOST
 bb_retro_run_frame(&test_host,keys[frame],false);
#else
 retro_run();
#endif
if(now()-frame_start>2.0) {fprintf(stderr,"GUARD frame=%u callbacks=%u\n",frame,callbacks);break;}}
 clock_gettime(CLOCK_MONOTONIC,&ts);printf("wall_ms_per_frame=%.6f\n",(ts.tv_sec+ts.tv_nsec*1e-9-start)*1000/frames);if(out)fclose(out);
 printf("distinct=%u valid=%u changed_pixels=%u callbacks=%u\n",distinct,valid,changed,callbacks);
 printf("frames=%u video=%08x audio=%08x samples=%llu nonzero=%llu\n",frames,video_hash,audio_hash,(unsigned long long)samples,(unsigned long long)nonzero);
 if(argc>9 && strcmp(argv[9],"-")){size_t n=retro_serialize_size();void*p=malloc(n);if(retro_serialize(p,n)){FILE*f=fopen(argv[9],"wb");fwrite(p,1,n,f);fclose(f);}free(p);}
 unsigned long long profile[20];retro_profile_read(profile);printf("PROFILE");for(unsigned i=0;i<20;i++)printf(" %llu",profile[i]);puts("");
if(getenv("REQUIRE_JIT")) {
 extern unsigned char *rom_translation_cache,*ram_translation_cache,*rom_translation_ptr,*ram_translation_ptr;
 extern unsigned dynarec_enable;
 printf("jit_used=%u rom_emitted=%ld ram_emitted=%ld\n",dynarec_enable,
 (long)(rom_translation_ptr-rom_translation_cache),(long)(ram_translation_ptr-ram_translation_cache));
 if(!dynarec_enable || (rom_translation_ptr==rom_translation_cache && ram_translation_ptr==ram_translation_cache))return 6;
}
retro_unload_game();retro_deinit();if(pcm)fclose(pcm);return 0;
}
