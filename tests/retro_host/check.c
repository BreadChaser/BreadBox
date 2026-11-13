#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "retro_host.h"
#include "../../systems/gba/gba_timing.h"
static bool duplicate,allow_activate=true;
static unsigned lifecycle;
static bool activate(void){return allow_activate;}
static void deactivate(void){assert(lifecycle==0);lifecycle=1;}
static void release(void){assert(lifecycle==1);lifecycle=2;}
static uint64_t now;static unsigned runs,frames,audio_count,tick_reads,native_pictures,native_nulls;
static unsigned char ram[16],state[32];
static int turbo_expect=-1;
static int16_t delivered[4];
static retro_environment_t env;static retro_video_refresh_t video;
static retro_audio_sample_batch_t batch;static retro_input_state_t input;
static uint64_t ticks(void){tick_reads++;return now;}
static void native_video(const void *p,unsigned w,unsigned h,size_t pitch)
{assert(w==8 && h==8 && pitch==16);if(p)native_pictures++;else native_nulls++;}
static void sleep_ticks(uint64_t n){now+=n;}
static void set_env(retro_environment_t cb){env=cb;}
static void set_video(retro_video_refresh_t cb){video=cb;}
static void set_batch(retro_audio_sample_batch_t cb){batch=cb;}
static void set_input(retro_input_state_t cb){input=cb;}
static void set_sample(retro_audio_sample_t cb){(void)cb;}
static void set_poll(retro_input_poll_t cb){(void)cb;}
static void nothing(void){}
static bool load(const struct retro_game_info *g){(void)g;struct retro_system_av_info a={.timing={60,32768}};assert(env(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO,&a));return true;}
static void av(struct retro_system_av_info *a){*a=(struct retro_system_av_info){.timing={60,32768}};}
static void *memory(unsigned id){assert(id==RETRO_MEMORY_SAVE_RAM);return ram;}
static size_t memory_size(unsigned id){(void)id;return sizeof(ram);}
static size_t state_size(void){return sizeof(state);}
static bool serialize(void *p,size_t n){assert(n==sizeof(state));memcpy(p,state,n);return true;}
static bool unserialize(const void *p,size_t n){memcpy(state,p,n);memset(ram,0xff,sizeof(ram));return state[0]!=0xee;}
static bool picture(const void *p,unsigned w,unsigned h,size_t pitch,enum retro_pixel_format f)
{assert(p && w==8 && h==8 && pitch==16 && f==RETRO_PIXEL_FORMAT_RGB565);frames++;return true;}
static void sound(const int16_t *p,size_t n){assert(p && n==2);memcpy(delivered,p,sizeof(delivered));audio_count++;}
static void run(void){uint16_t p[64]={0};int16_t a[4]={0};video(duplicate?NULL:p,8,8,16);batch(a,2);if(turbo_expect>=0){assert(input(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A)==(turbo_expect&1));assert(input(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==!!(turbo_expect&2));}runs++;}
static const bb_retro_core core={.activate=activate,.deactivate=deactivate,.set_environment=set_env,.set_video_refresh=set_video,
 .set_audio_sample=set_sample,.set_audio_sample_batch=set_batch,.set_input_poll=set_poll,.set_input_state=set_input,
 .init=nothing,.deinit=nothing,.run=run,.reset=nothing,.unload_game=nothing,.load_game=load,
 .get_system_av_info=av,.get_memory_data=memory,.get_memory_size=memory_size,.serialize_size=state_size,.serialize=serialize,.unserialize=unserialize};
static unsigned morton(unsigned x,unsigned y){return (x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);}
int main(void)
{
 bb_retro_host h,other;bb_retro_ext ext={.release=release};bb_retro_desc d={.ext=&ext,.core=&core,.max_width=8,.max_height=8,.keys={[8]=1}};
 bb_retro_services s={.ticks=ticks,.sleep=sleep_ticks,.ticks_per_second=1000000,.video=picture,.audio=sound};struct retro_game_info g={0};
 allow_activate=false;assert(!bb_retro_open(&h,&d,&s,&g));allow_activate=true;
 assert(bb_retro_open(&h,&d,&s,&g));assert(!bb_retro_open(&other,&d,&s,&g));
 enum retro_pixel_format f=RETRO_PIXEL_FORMAT_RGB565;assert(env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT,&f));
 for(unsigned i=0;i<10000;i++){assert(bb_retro_step(&h,1));assert(input(0,RETRO_DEVICE_JOYPAD,0,8));assert(!input(0,RETRO_DEVICE_JOYPAD,1,8));bb_retro_pace(&h);}
 assert(now==166666666 && runs==10000 && frames==10000 && audio_count==10000);
 assert(h.audio_frames==20000);
 assert(h.audio_nonzero==0 && h.audio_hash==2166136261u);
 assert(input(0,RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD,0),0,8));
 /* Shared turbo keys pulse A/B for three frames on, three frames off, including when
  * the corresponding ordinary face button is not held. */
 d.turbo_a_key=2;d.turbo_b_key=4;h.frames=0;
 turbo_expect=1;assert(bb_retro_step(&h,2));
 turbo_expect=1;assert(bb_retro_step(&h,2));
 turbo_expect=1;assert(bb_retro_step(&h,2));
 turbo_expect=0;assert(bb_retro_step(&h,2));
 turbo_expect=0;assert(bb_retro_step(&h,4));
 turbo_expect=0;assert(bb_retro_step(&h,4));
 turbo_expect=2;assert(bb_retro_step(&h,4));turbo_expect=-1;
 struct retro_system_av_info changed={.timing={50,44100}};assert(!env(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO,&changed));
 duplicate=true;now+=11000000;assert(bb_retro_step(&h,0) && !h.stalled && !h.picture);duplicate=false;
 int16_t pcm[]={0,12,-30,0};batch(pcm,2);
 assert(!memcmp(delivered,pcm,sizeof(pcm)) && h.audio_frames==20018 && h.audio_nonzero==0 && h.audio_hash==2166136261u);
 h.services.audio=NULL;h.audit_audio=true;batch(pcm,2);
 assert(h.audio_frames==20020 && h.audio_nonzero==2 && h.audio_hash!=2166136261u);
 char buf[32];size_t n;assert(!bb_retro_state_save(&h,buf,31,&n));assert(bb_retro_state_save(&h,buf,32,&n));
 memset(ram,0x5a,sizeof(ram));assert(bb_retro_state_load(&h,buf,n,true)==BB_STATE_OK);assert(ram[0]==0x5a);
 buf[0]=0xee;assert(bb_retro_state_load(&h,buf,n,true)==BB_STATE_PARTLY);
 unsigned stopped=runs;size_t unchanged=0;
 assert(h.invalid && !bb_retro_step(&h,0));bb_retro_run_frame(&h,0,false);assert(runs==stopped);
 assert(!bb_retro_state_save(&h,buf,sizeof(buf),&unchanged));
 assert(bb_retro_state_load(&h,buf,n,false)!=BB_STATE_OK);
assert(ram[0]==0x5a);
 assert(bb_retro_state_load(&h,buf,n-1,true)==BB_STATE_DAMAGED);
 bb_retro_close(&h);assert(!bb_retro_step(&h,0));assert(lifecycle==2);lifecycle=0;assert(bb_retro_open(&h,&d,&s,&g));bb_retro_close(&h);assert(lifecycle==2);
 /* An adapter-paced session receives NULL itself; the host neither reads
  * time nor substitutes its own picture/watchdog/presentation policy. */
 lifecycle=0;d.video_refresh=native_video;s.video=NULL;
 assert(!bb_retro_open(&h,&d,&s,&g));
 assert(bb_retro_bind(&h,&d,&s));h.loaded=true;
 unsigned before_ticks=tick_reads;h.services.video=NULL;
 for(unsigned i=0;i<300;i++) {duplicate=i%3==1;bb_retro_run_frame(&h,0,i%3==1);}
 assert(tick_reads==before_ticks && native_pictures==200 && native_nulls==100);
 assert(!bb_retro_step(&h,0));bb_retro_pace(&h);assert(tick_reads==before_ticks);
 bb_retro_unbind(&h);assert(lifecycle==1);duplicate=false;
 uint16_t out[256],pixels[64];for(unsigned i=0;i<64;i++)pixels[i]=i*977;
 assert(bb_retro_upload(out,16,16,pixels,8,8,16,RETRO_PIXEL_FORMAT_RGB565));
 for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)assert(out[morton(x,y)]==pixels[y*8+x]);
 unsigned char unaligned[8*35+1];
 for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){uint32_t p=(x<<16)|(y<<8)|255;memcpy(unaligned+1+y*35+x*4,&p,4);}
 assert(bb_retro_upload(out,16,16,unaligned+1,8,8,35,RETRO_PIXEL_FORMAT_XRGB8888));
 for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)assert(out[morton(x,y)]==((x>>3)<<11|(y>>2)<<5|31));
 uint16_t white=0x7fff;assert(bb_retro_upload(out,8,8,&white,1,1,2,RETRO_PIXEL_FORMAT_0RGB1555));assert(out[0]==0xffff);
 assert(!bb_retro_upload(out,8,8,pixels,9,8,18,RETRO_PIXEL_FORMAT_RGB565));
 bb_retro_clock c;bb_retro_clock_reset(&c,0,268123480,16777216,280896);
 for(unsigned i=0;i<600000;i++)bb_retro_clock_advance(&c,c.next);
 assert(c.next==(uint64_t)((__uint128_t)600000*268123480*280896/16777216));
 now=c.next+2*c.whole;assert(bb_retro_clock_skip(&c,now));assert(bb_retro_clock_skip(&c,now));assert(!bb_retro_clock_skip(&c,now));
 /* Replay deadline boundaries, persistent overload, SD stalls and explicit
  * rebases through the established GBA clock and the general rational clock. */
 gba_clock old;bb_retro_clock shared;uint64_t tick=123;
 gba_clock_reset(&old,tick,268123480);bb_retro_clock_reset(&shared,tick,268123480,16777216,280896);
 for(unsigned i=0;i<1000000;i++) {
  if(!(i%193)){tick+=268123480/3;gba_clock_reset(&old,tick,268123480);bb_retro_clock_reset(&shared,tick,268123480,16777216,280896);}
  assert(gba_clock_skip(&old,tick)==bb_retro_clock_skip(&shared,tick));
  tick+=old.whole*(i%7)+(i%3==0);
  assert(gba_clock_advance(&old,tick)==bb_retro_clock_advance(&shared,tick));
  assert(old.next==shared.next && old.fraction==shared.fraction && old.consecutive==shared.consecutive);
 }
 float sx,sy;bb_retro_scale_aspect(256,240,4.0f/3,1,&sx,&sy);assert(sx==1.25f && sy==1);
 bb_retro_scale_aspect(256,240,4.0f/3,0,&sx,&sy);assert(sx==1 && sy==1);
 bb_retro_scale(240,160,1,&sx,&sy);assert(sx==1.5f && sy==1.5f);
 puts("shared host: callbacks, input, exact rational pacing, state/battery, pixel formats and failed lifecycle PASS");
}
