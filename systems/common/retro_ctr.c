/* 3DS devices for the shared libretro host. GPL-2.0-or-later.
 * Persistence and pause policy belong to the shell services. */
#include <string.h>
#include "libretro.h"
#include "app_paths.h"
#include "app_log.h"
#include "platform_video.h"
#include "dev_switches.h"
#include "audio.h"
#include "app_input.h"
#include "ui.h"
#include "playlog.h"
#include "home.h"
#include "app_main.h"
#include "retro_ctr.h"
#include "devtools.h"
#include "bb_save.h"
#include "fs_path.h"
#include "bb_pause.h"
#include "bb_feature_ui.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
static bb_retro_host host;
static C3D_Tex texture;
static Tex3DS_SubTexture sub;
static bool ready,sound,filter;
static bb_cheats cheats;
static bool muted;
static int cheat_count(void){return cheats.count;}
static const char *cheat_label(int i){return cheats.entries[i].name;}
static const char *cheat_value(int i){return cheats.entries[i].enabled?"On":"Off";}
static void cheat_toggle(int i){if(!bb_cheats_toggle(&cheats,i))bb_pause_notice("Could not save cheats");}
static const char *cheat_add(void){return bb_cheats_keyboard(&cheats);}
const bb_cheat_ops bb_retro_ctr_cheats={cheat_count,cheat_label,cheat_value,cheat_toggle,cheat_add};
static unsigned screen_size=1;
static uint32_t injected;static unsigned injected_frames;
static char state_base[PATH_LEN-24],battery_path[PATH_LEN];
static uint64_t ticks(void){return svcGetSystemTick();}
static void sleep_ticks(uint64_t n){svcSleepThread(n*1000000000ULL/SYSCLOCK_ARM11);}
static void audio(const int16_t *p,size_t n){if(sound && !muted)audio_write(p,n);}
static void log_line(const char *s){log_printf("retro: %s",s);}
static unsigned power2(unsigned n){unsigned p=8;while(p<n && p<1024)p*=2;return p;}
static bool upload(const void *data,unsigned w,unsigned h,size_t pitch,enum retro_pixel_format fmt)
{
 if(!gspHasGpuRight() || w>1024 || h>1024)return false;
 video_gpu_frame_begin();video_gpu_wait_previous();
 unsigned tw=power2(w),th=power2(h);
 if(ready && (texture.width!=tw || texture.height!=th)){C3D_TexDelete(&texture);ready=false;}
 if(!ready){
  if(!C3D_TexInit(&texture,tw,th,GPU_RGB565))return false;
  ready=true;memset(texture.data,0,texture.size);
  C3D_TexSetWrap(&texture,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
  C3D_TexSetFilter(&texture,filter?GPU_LINEAR:GPU_NEAREST,filter?GPU_LINEAR:GPU_NEAREST);
 }
 if(!bb_retro_upload(texture.data,tw,th,data,w,h,pitch,fmt))return false;
 sub=(Tex3DS_SubTexture){w,h,0,1,(float)w/tw,1-(float)h/th};
 GSPGPU_FlushDataCache(texture.data,texture.size);C2D_Prepare();return true;
}
static void release_texture(void)
{
 /* HOME -> Close has already relinquished GSP: neither wait nor submit. */
 if(gspHasGpuRight()){video_gpu_idle();if(ready)C3D_TexDelete(&texture);}
 ready=false;
}
static void draw(void *unused)
{
 (void)unused;if(!ready)return;
 float sx,sy;bb_retro_scale_aspect(sub.width,sub.height,host.av.geometry.aspect_ratio,screen_size,&sx,&sy);
 C2D_Image image={&texture,&sub};
 C2D_DrawImageAt(image,(400-sub.width*sx)/2,(240-sub.height*sy)/2,.5f,NULL,sx,sy);
}
/* Save postcards are written from linear memory, never the game's heap. */
static void *picture_memory;
/* The last picture for save postcards: the CPU copy of the tiled texture. */
static bool picture(uint16_t *px,unsigned *w,unsigned *h)
{
 if(!ready || sub.width>BB_THUMB_MAX_W || sub.height>BB_THUMB_MAX_H)return false;
 const uint16_t *in=texture.data;unsigned tw=texture.width;
 for(unsigned y=0;y<sub.height;y++)for(unsigned x=0;x<sub.width;x++)
  px[y*sub.width+x]=in[((((y>>3)*(tw>>3)+(x>>3))<<6)|(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3))];
 *w=sub.width;*h=sub.height;return true;
}
static size_t state_size(void){return host.loaded?host.desc->core->serialize_size():0;}
static bool state_save(void *p,size_t n,size_t *len){return bb_retro_state_save(&host,p,n,len);}
static bb_state_result state_load(const void *p,size_t n,bool keep){return bb_retro_state_load(&host,p,n,keep);}
static bool battery(void **p,size_t *n){return bb_retro_battery(&host,p,n);}
static size_t rtc_size(void){return bb_retro_rtc_size(&host);}
static bool rtc_get(void *p,size_t n){return bb_retro_rtc_get(&host,p,n);}
static bool rtc_set(const void *p,size_t n){return bb_retro_rtc_set(&host,p,n);}
const bb_save_ops bb_retro_ctr_save_ops={.state_size=state_size,.state_save=state_save,.state_load=state_load,
 .battery=battery,.rtc_size=rtc_size,.rtc_get=rtc_get,.rtc_set=rtc_set};
static void pause_audio(bool paused){if(sound)audio_pause(paused);playlog_pause(paused);}
static void rebase(void){bb_retro_rebase(&host);}
static void reset(void){host.desc->core->reset();}
static void restored(void)
{if(sound){audio_exit();sound=audio_init(host.av.timing.sample_rate);audio_pause(true);}rebase();}
bool bb_retro_ctr_state(int slot,bool save)
{
 if(!host.loaded || slot<0 || slot>BB_SLOT_LAST)return false;   /* a file index: devcontrol maps the remote's card number (bb_slots.h) */
 bool ok=bb_save_state(slot,save);
 if(!save && ok && sound && !bb_pause_active())audio_pause(false);
 return ok;
}
bool bb_retro_ctr_key(const char *name,unsigned frames)
{
 static const char *names[]={"a","b","start","select","up","down","left","right","l","r"};
 static const uint32_t keys[]={KEY_A,KEY_B,KEY_START,KEY_SELECT,KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT,KEY_L,KEY_R};
 if(!host.loaded || bb_pause_active() || injected_frames || !frames || frames>600)return false;
 for(unsigned i=0;i<10;i++)if(!strcmp(name,names[i])){injected=keys[i];injected_frames=frames;return true;}
 return false;
}
static void event(APT_HookType type,void *unused)
{
 (void)unused;
 if(type==APTHOOK_ONSUSPEND || type==APTHOOK_ONSLEEP || type==APTHOOK_ONEXIT)bb_pause_suspend(!host.stalled);
 if(type==APTHOOK_ONRESTORE || type==APTHOOK_ONWAKEUP)bb_pause_restore();
}
bb_run_result bb_retro_ctr_run(const bb_launch *l,const bb_retro_desc *desc,char *error,size_t cap)
{
 log_printf("retro: start system=%s\n",desc->system->id);
 screen_size=desc->screen_size;filter=desc->filter;muted=false;
 struct retro_system_info info={0};desc->core->get_system_info(&info);
 log_printf("retro: core info fullpath=%d\n",info.need_fullpath);
 void *rom=NULL;size_t size=0;
 if(!info.need_fullpath){
  FILE *f=fopen(l->path,"rb");if(!f)goto failed;
  if(fseek(f,0,SEEK_END)){fclose(f);goto failed;}
  long len=ftell(f);if(len<=0 || len>16*1024*1024 || fseek(f,0,SEEK_SET)){fclose(f);goto failed;}
  size=(size_t)len;rom=malloc(size);
  bool ok=rom && fread(rom,1,size,f)==size && !ferror(f);fclose(f);if(!ok)goto failed;
 }
 bb_retro_services services={ticks,SYSCLOCK_ARM11,sleep_ticks,upload,audio,log_line};
 struct retro_game_info game={.path=l->path,.data=rom,.size=size};
 char save_dir[PATH_LEN];
 if(l->save_dir)snprintf(save_dir,sizeof(save_dir),"%s",l->save_dir);
 else snprintf(save_dir,sizeof(save_dir),APP_SD_ROOT "/saves/%s",desc->system->id);
 bb_retro_desc config=*desc;config.save_dir=save_dir;
 bb_controls_device_open(&bb_active_controls,desc->system->id,l->id.key,false);
 bb_controls_map(&bb_active_controls,config.keys,&config.turbo_a_key,&config.turbo_b_key);
 if(desc->system->settings_open)desc->system->settings_open(l->id.key);
 char battery_name[PATH_LEN];
 if(snprintf(battery_name,sizeof(battery_name),"%s.sav",l->id.key)>=(int)sizeof(battery_name) || !fs_path_join(battery_path,sizeof(battery_path),save_dir,battery_name))goto failed;
 mkdir(APP_SD_ROOT "/saves",0777);mkdir(save_dir,0777);
 mkdir(APP_SD_ROOT "/states",0777);
 char dir[PATH_LEN];snprintf(dir,sizeof(dir),APP_SD_ROOT "/states/%s",desc->system->id);mkdir(dir,0777);
 if(!fs_path_join(state_base,sizeof(state_base),dir,l->id.key))goto failed;
 log_printf("retro: content bytes=%u; open\n",(unsigned)size);
 if(!bb_retro_open(&host,&config,&services,&game))goto failed;
 log_printf("retro: loaded fps=%.8f\n",host.av.timing.fps);
 bb_cheats_open(&cheats,desc->system->id,l->id.key,desc->core->cheat_reset,desc->core->cheat_set);
 picture_memory=linearAlloc(BB_THUMB_BYTES);   /* NULL: saves without pictures */
 bb_save_config saves={.system=desc->system,.key=l->id.key,.battery_path=battery_path,
  .state_base=state_base,.identity=l->id.crc,.writer_core=g_new3ds?2:1,.notice=bb_pause_notice,.restored=restored,
  .picture=picture,.picture_buffer=picture_memory,.played=playlog_played};
 bb_pause_config pause={.system=desc->system,.title=l->entry?l->entry->title:l->id.key,
  .draw_game=draw,.pause=pause_audio,.rebase=rebase,.reset=reset};
 bb_pause_begin(&pause);
 if(!bb_save_begin(&saves,error,cap)){if(picture_memory)linearFree(picture_memory);picture_memory=NULL;bb_cheats_close(&cheats);bb_retro_close(&host);free(rom);return BB_RUN_FAILED;}
 ready=false;sound=!sw_nosound && audio_init(host.av.timing.sample_rate);
 if(sound)audio_pause(false);
 injected_frames=0;ui_wants_3d=false;
 aptHookCookie cookie;aptHook(&cookie,event,NULL);
 int answer=!l->remote && !l->resume && bb_save_exists(BB_SLOT_CONTINUE)?bb_pause_prompt():BB_CONTINUE_ASK;
 bool resume=!l->remote && (l->resume || answer==BB_CONTINUE_YES), back=answer==BB_CONTINUE_BACK;   /* Back: nothing runs */
 bool state_ok=l->slot>=0?(bb_slot_file(l->slot)>=0 && bb_save_state((unsigned)bb_slot_file(l->slot),false)):!resume || bb_save_state(BB_SLOT_CONTINUE,false);
 if(sound)audio_pause(false);
 char metadata[128];snprintf(metadata,sizeof(metadata),"%s:%s",desc->system->id,l->id.key);
 if(!back){playlog_begin(metadata);home_note_game(l->path,pause.title,pause.title,"continue");}
 dev_game_started(l->path,state_ok);
 uint64_t report=ticks(),report_frames=0,report_drawn=0,report_shown=0;
 uint64_t drawn=0,shown=0,emu_ticks=0,present_ticks=0;
 bb_run_result result=BB_RUN_QUIT;
 while(!back && bb_save_runnable() && aptMainLoop()){
  if(dev_tick(true))break;
  input_poll();uint32_t held=input_keys_held();
  int remote_ui=dev_ui_pending();
  unsigned actions=bb_controls_actions(&bb_active_controls,held,input_keys_down(),&held);
  if(actions&6) {
   bb_pause_quick((actions&2)!=0);
   rebase();held=0;injected_frames=0;
   report=ticks();report_frames=host.frames;report_drawn=drawn;report_shown=shown;emu_ticks=present_ticks=0;
  }
  if((actions&1) || (input_keys_down()&KEY_TOUCH) || remote_ui==DEV_UI_PAUSE || remote_ui==DEV_UI_SETTINGS || remote_ui==DEV_UI_STATES){
   if(remote_ui!=DEV_UI_NONE)dev_ui_take();
   if(bb_pause_run())break;
   bb_controls_map(&bb_active_controls,config.keys,&config.turbo_a_key,&config.turbo_b_key);
   rebase();held=0;injected_frames=0;report=ticks();report_frames=host.frames;
   report_drawn=drawn;report_shown=shown;emu_ticks=present_ticks=0;
  }
  if(!bb_save_runnable()){result=BB_RUN_FAILED;break;}
  if(injected_frames){held|=injected;injected_frames--;}
  if(desc->common_settings) {
   screen_size=bb_setting(desc->system,0);bool next_filter=bb_setting(desc->system,1);muted=!bb_setting(desc->system,2);
   if(ready && filter!=next_filter && gspHasGpuRight()){video_gpu_idle();C3D_TexSetFilter(&texture,next_filter?GPU_LINEAR:GPU_NEAREST,next_filter?GPU_LINEAR:GPU_NEAREST);}
   filter=next_filter;
  }
  uint64_t emu_start=ticks();
  if(!bb_retro_step(&host,held)){result=host.stalled?BB_RUN_STALLED:BB_RUN_QUIT;break;}
  emu_ticks+=ticks()-emu_start;
  if(host.picture){
   drawn++;
   if(!host.skip){uint64_t present_start=ticks();ui_begin();video_present(false,draw,ui_game_blank,NULL);present_ticks+=ticks()-present_start;shown++;}
  }
  if(!(host.frames%180))bb_save_queue();
  bb_session_tick();
  uint64_t now=ticks();
  if(now-report>=SYSCLOCK_ARM11){
   char stats[256];audio_stats(stats,sizeof(stats));
   double interval=(now-report)/(double)SYSCLOCK_ARM11;
   unsigned interval_frames=host.frames-report_frames;
   char fields[192];
   snprintf(fields,sizeof(fields),"audio_frames=%llu audio_nonzero=%llu audio_hash=%08lx pictures=%u presented=%u",
    (unsigned long long)host.audio_frames,(unsigned long long)host.audio_nonzero,(unsigned long)host.audio_hash,
    (unsigned)(drawn-report_drawn),(unsigned)(shown-report_shown));
   dev_system_sample(desc->system->id,interval,interval_frames,drawn-report_drawn,shown-report_shown,
    interval_frames?emu_ticks*1000.0/(SYSCLOCK_ARM11*interval_frames):0,
    shown-report_shown?present_ticks*1000.0/(SYSCLOCK_ARM11*(shown-report_shown)):0,
    stats,host.av.timing.fps,fields);
   emu_ticks=present_ticks=0;
   report=now;report_frames=host.frames;report_drawn=drawn;report_shown=shown;
  }
  bb_retro_pace(&host);
 }
 bool execution_ok=bb_save_runnable();
 bool saved=bb_save_end(!host.stalled);aptUnhook(&cookie);playlog_end();
 if(picture_memory){linearFree(picture_memory);picture_memory=NULL;}
 if(sound){audio_pause(true);audio_exit();sound=false;}
 release_texture();
 bb_cheats_close(&cheats);bb_retro_close(&host);free(rom);dev_game_ended(saved);
 if(!saved){snprintf(error,cap,"%s",execution_ok?"Could not save battery. Retry from your collection.":"Postcard load failed. Restart the game to continue.");return BB_RUN_FAILED;}
 return result;
failed:
 free(rom);snprintf(error,cap,"Could not load core or content");return BB_RUN_FAILED;
}
