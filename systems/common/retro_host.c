#include "retro_host.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>
static bb_retro_host *active;
static void rh_core_log(enum retro_log_level level,const char *fmt,...)
{
 (void)level;if(!active || !active->services.log)return;
 char text[512];va_list ap;va_start(ap,fmt);vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);
 active->services.log(text);
}
static bool rh_environment(unsigned cmd,void *data)
{
 bb_retro_host *h=active;if(!h)return false;
 if(h->desc->ext && h->desc->ext->environment && h->desc->ext->environment(cmd,data))return true;
 switch(cmd){
 case RETRO_ENVIRONMENT_SHUTDOWN:h->shutdown=true;return true;
 case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:((struct retro_log_callback *)data)->log=rh_core_log;return true;
 case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:*(const char **)data=h->desc->system_dir;return true;
 case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char **)data=h->desc->save_dir;return true;
 case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:{
  enum retro_pixel_format f=*(enum retro_pixel_format *)data;
  if(f!=RETRO_PIXEL_FORMAT_RGB565 && f!=RETRO_PIXEL_FORMAT_XRGB8888 && f!=RETRO_PIXEL_FORMAT_0RGB1555)return false;
  h->format=f;return true;
 }
 case RETRO_ENVIRONMENT_GET_VARIABLE:{
  struct retro_variable *v=data;v->value=NULL;
  for(const struct retro_variable *p=h->desc->variables;p && p->key;p++)
   if(!strcmp(v->key,p->key)){v->value=p->value;return true;}
  return false;
 }
 case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool *)data=h->dirty;h->dirty=false;return true;
 case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:*(unsigned *)data=h->skip?2:3;return true;
 case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:return true;
 case RETRO_ENVIRONMENT_GET_CAN_DUPE:*(bool *)data=true;return true;
 case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:*(unsigned *)data=0;return true;
 case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:{
  const struct retro_system_av_info *av=data;
  /* Rate changes need a device reopen; reject instead of playing at wrong rate. */
  if(h->loaded && (av->timing.fps!=h->av.timing.fps || av->timing.sample_rate!=h->av.timing.sample_rate))return false;
  h->av=*av;return true;
 }
 case RETRO_ENVIRONMENT_SET_VARIABLES:case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
 case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
 case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:return true;
 default:return false;
 }
}
static void rh_video(const void *data,unsigned w,unsigned height,size_t pitch)
{
 bb_retro_host *h=active;
 if(!h)return;
 if(h->desc->ext && h->desc->ext->video && h->desc->ext->video(data,w,height,pitch)){
  h->picture=data!=NULL;h->last_video=h->services.ticks();return;
 }
 /* A duplicated picture is still a live video callback (static menus). */
 if(!data){h->last_video=h->services.ticks();return;}
 if(data==RETRO_HW_FRAME_BUFFER_VALID)return;
 unsigned bytes=h->format==RETRO_PIXEL_FORMAT_XRGB8888?4:2;
 if(!w || !height || w>h->desc->max_width || height>h->desc->max_height || pitch<(size_t)w*bytes)return;
 h->last_video=h->services.ticks();
 h->picture=h->services.video(data,w,height,pitch,h->format);
}
static size_t rh_audio_batch(const int16_t *data,size_t n)
{
 if(active && data){
  if(active->audit_audio)for(size_t i=0;i<n*2;i++){
   active->audio_nonzero+=data[i]!=0;
   active->audio_hash=(active->audio_hash^(uint16_t)data[i])*16777619u;
  }
  active->audio_frames+=n;if(active->services.audio)active->services.audio(data,n);}return n;
}
static void rh_audio_sample(int16_t l,int16_t r){int16_t data[]={l,r};rh_audio_batch(data,1);}
static void rh_input_poll(void){}
static int16_t rh_input_state(unsigned port,unsigned device,unsigned index,unsigned id)
{
 if(!active || port || index || (device&RETRO_DEVICE_MASK)!=RETRO_DEVICE_JOYPAD)return 0;
 unsigned mask=0;for(unsigned i=0;i<16;i++)if(active->held&active->desc->keys[i])mask|=1u<<i;
 if(active->frames%6<3) {
  if(active->desc->turbo_a_key && (active->held&active->desc->turbo_a_key))mask|=1u<<RETRO_DEVICE_ID_JOYPAD_A;
  if(active->desc->turbo_b_key && (active->held&active->desc->turbo_b_key))mask|=1u<<RETRO_DEVICE_ID_JOYPAD_B;
 }
 return id==RETRO_DEVICE_ID_JOYPAD_MASK?(int16_t)mask:id<16?!!(mask&(1u<<id)):0;
}
void bb_retro_rebase(bb_retro_host *h)
{
 uint64_t num=h->desc->fps_num,den=h->desc->fps_den;
 if(!num || !den){num=(uint64_t)(h->av.timing.fps*1000000.0+0.5);den=1000000;}
 bb_retro_clock_reset(&h->clock,h->services.ticks(),h->services.ticks_per_second,num,den);
 h->last_video=h->clock.next;
}
bool bb_retro_bind(bb_retro_host *h,const bb_retro_desc *d,const bb_retro_services *s)
{
 if(active || !h || !d || !d->core || !s || !s->ticks || !s->ticks_per_second || (!s->video && !d->video_refresh) || !d->max_width || !d->max_height)return false;
 const bb_retro_core *c=d->core;
 *h=(bb_retro_host){.desc=d,.services=*s,.format=RETRO_PIXEL_FORMAT_0RGB1555,.audio_hash=2166136261u};active=h;
 if(c->activate && !c->activate()){active=NULL;return false;}
 c->set_environment(rh_environment);c->set_video_refresh(d->video_refresh?d->video_refresh:rh_video);c->set_audio_sample(rh_audio_sample);
 c->set_audio_sample_batch(rh_audio_batch);c->set_input_poll(rh_input_poll);c->set_input_state(rh_input_state);
 return true;
}
void bb_retro_unbind(bb_retro_host *h)
{
 if(h && active==h){if(h->desc->core->deactivate)h->desc->core->deactivate();active=NULL;}
}
bool bb_retro_open(bb_retro_host *h,const bb_retro_desc *d,const bb_retro_services *s,const struct retro_game_info *game)
{
 if(!game || (d && d->video_refresh) || !bb_retro_bind(h,d,s))return false;
 const bb_retro_core *c=d->core;
 if(d->ext && d->ext->prepare && !d->ext->prepare(game))goto fail;
 if(s->log)s->log("core init\n");
 c->init();h->initialized=true;
 if(s->log)s->log("core load\n");
 h->loaded=c->load_game(game);if(!h->loaded)goto fail;
 c->get_system_av_info(&h->av);
 if(!isfinite(h->av.timing.fps) || h->av.timing.fps<1 || h->av.timing.fps>1000 || !isfinite(h->av.timing.sample_rate) || h->av.timing.sample_rate<1 || h->av.timing.sample_rate>384000)goto fail;
 if((d->fps_num && !d->fps_den) || (d->fps_den && !d->fps_num) || (d->fps_den && s->ticks_per_second>UINT64_MAX/d->fps_den))goto fail;
 bb_retro_rebase(h);return true;
fail:bb_retro_close(h);return false;
}
void bb_retro_close(bb_retro_host *h)
{
 if(!h || active!=h)return;
 if(h->loaded)h->desc->core->unload_game();
 if(h->initialized)h->desc->core->deinit();
 const bb_retro_ext *ext=h->desc->ext;
 bb_retro_unbind(h);
 if(ext && ext->release)ext->release();
 memset(h,0,sizeof(*h));
}
bool bb_retro_step(bb_retro_host *h,uint32_t held)
{
 if(!h || active!=h || !h->loaded || h->invalid || h->desc->video_refresh || h->shutdown || h->stalled)return false;
 h->held=held;h->skip=bb_retro_clock_skip(&h->clock,h->services.ticks());h->picture=false;
 if(h->desc->ext && h->desc->ext->before_frame)h->desc->ext->before_frame(&h->skip);
 bb_retro_run_frame(h,held,h->skip);
 if(h->desc->ext && h->desc->ext->after_frame)h->desc->ext->after_frame();
 h->stalled=h->services.ticks()-h->last_video>10*h->services.ticks_per_second;
 return !h->shutdown && !h->stalled;
}
void bb_retro_run_frame(bb_retro_host *h,uint32_t held,bool skip)
{
 if(!h || active!=h || h->invalid)return;
 h->held=held;h->skip=skip;h->picture=false;
 h->desc->core->run();h->frames++;
}
size_t bb_retro_rtc_size(bb_retro_host *h)
{return h && active==h && h->desc->ext && h->desc->ext->rtc_size?h->desc->ext->rtc_size():0;}
bool bb_retro_rtc_get(bb_retro_host *h,void *p,size_t n)
{return p && n && n==bb_retro_rtc_size(h) && h->desc->ext->rtc_get && h->desc->ext->rtc_get(p,n);}
bool bb_retro_rtc_set(bb_retro_host *h,const void *p,size_t n)
{return p && n && n==bb_retro_rtc_size(h) && h->desc->ext->rtc_set && h->desc->ext->rtc_set(p,n);}
void bb_retro_pace(bb_retro_host *h)
{
 if(!h || active!=h || h->desc->video_refresh)return;
 uint64_t now=h->services.ticks();bb_retro_clock_advance(&h->clock,now);
 if(h->clock.next>now && h->services.sleep)h->services.sleep(h->clock.next-now);
}
bool bb_retro_battery(bb_retro_host *h,void **data,size_t *size)
{
 if(!h || active!=h || !h->loaded || !data || !size)return false;
 *data=h->desc->core->get_memory_data(RETRO_MEMORY_SAVE_RAM);
 *size=h->desc->core->get_memory_size(RETRO_MEMORY_SAVE_RAM);return *data && *size;
}
bool bb_retro_state_save(bb_retro_host *h,void *buf,size_t cap,size_t *len)
{
 if(!h || active!=h || !h->loaded || h->invalid || !buf)return false;
 size_t n=h->desc->core->serialize_size();if(!n || cap<n)return false;
 if(!h->desc->core->serialize(buf,n))return false;
 if(len)*len=n;return true;
}
bb_state_result bb_retro_state_load(bb_retro_host *h,const void *buf,size_t len,bool keep)
{
 if(!h || active!=h || !h->loaded || h->invalid || !buf || len!=h->desc->core->serialize_size())return BB_STATE_DAMAGED;
 void *ram=NULL,*saved=NULL;size_t n=0;
 if(keep && bb_retro_battery(h,&ram,&n)){
  saved=malloc(n);if(!saved)return BB_STATE_OTHER;memcpy(saved,ram,n);
 }
 bool ok=h->desc->core->unserialize(buf,len);
 if(saved){
  /* Some cores replace their backing allocation on state restore. */
  void *current=NULL;size_t size=0;
  if(!bb_retro_battery(h,&current,&size) || size!=n){free(saved);h->invalid=true;return BB_STATE_PARTLY;}
  memcpy(current,saved,n);free(saved);
 }
 if(!ok)h->invalid=true;
 bb_retro_rebase(h);return ok?BB_STATE_OK:BB_STATE_PARTLY;
}
static unsigned rh_morton(unsigned x,unsigned y)
{return (x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);}
bool bb_retro_upload(uint16_t *out,unsigned tw,unsigned th,const void *data,unsigned w,unsigned h,size_t pitch,enum retro_pixel_format format)
{
 unsigned bpp=format==RETRO_PIXEL_FORMAT_XRGB8888?4:2;
 if(!out || !data || !w || !h || tw%8 || th%8 || w>tw || h>th || pitch<(size_t)w*bpp || (format!=RETRO_PIXEL_FORMAT_RGB565 && format!=RETRO_PIXEL_FORMAT_0RGB1555 && format!=RETRO_PIXEL_FORMAT_XRGB8888))return false;
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
  const unsigned char *in=(const unsigned char *)data+y*pitch+x*bpp;
  uint16_t p;
  if(bpp==4){uint32_t v;memcpy(&v,in,4);p=((v>>8)&0xf800)|((v>>5)&0x07e0)|((v>>3)&31);}
  else{memcpy(&p,in,2);if(format==RETRO_PIXEL_FORMAT_0RGB1555)p=((p&0x7c00)<<1)|((p&0x3e0)<<1)|((p>>4)&0x20)|(p&31);}
  out[((y>>3)*(tw>>3)+(x>>3))*64+rh_morton(x,y)]=p;
 }
 return true;
}
void bb_retro_scale(unsigned w,unsigned h,unsigned mode,float *sx,float *sy)
{
 *sx=*sy=1;if(!w || !h)return;
 if(mode==2){*sx=400.0f/w;*sy=240.0f/h;}
 else if(mode==1){float x=400.0f/w,y=240.0f/h;*sx=*sy=x<y?x:y;}
}

void bb_retro_scale_aspect(unsigned w,unsigned h,float aspect,unsigned mode,float *sx,float *sy)
{
 bb_retro_scale(w,h,mode,sx,sy);
 if(mode!=1 || !w || !h || !(aspect>0))return;
 float width=240*aspect,height=240;
 if(width>400){width=400;height=400/aspect;}
 *sx=width/w;*sy=height/h;
}
