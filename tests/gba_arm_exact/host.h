#include "retro_host.h"
extern void retro_set_environment(retro_environment_t cb);
extern void retro_set_video_refresh(retro_video_refresh_t cb);
extern void retro_set_audio_sample(retro_audio_sample_t cb);
extern void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb);
extern void retro_set_input_poll(retro_input_poll_t cb);
extern void retro_set_input_state(retro_input_state_t cb);
extern void retro_init(void);
extern void retro_deinit(void);
extern void retro_run(void);
extern void retro_reset(void);
extern void retro_unload_game(void);
extern bool retro_load_game(const struct retro_game_info *g);
extern void retro_get_system_info(struct retro_system_info *p);
extern void retro_get_system_av_info(struct retro_system_av_info *p);
extern void * retro_get_memory_data(unsigned id);
extern size_t retro_get_memory_size(unsigned id);
extern size_t retro_serialize_size(void);
extern bool retro_serialize(void *p,size_t n);
extern bool retro_unserialize(const void *p,size_t n);
static const bb_retro_core test_core={
.set_environment=retro_set_environment,
.set_video_refresh=retro_set_video_refresh,
.set_audio_sample=retro_set_audio_sample,
.set_audio_sample_batch=retro_set_audio_sample_batch,
.set_input_poll=retro_set_input_poll,
.set_input_state=retro_set_input_state,
.init=retro_init,
.deinit=retro_deinit,
.run=retro_run,
.reset=retro_reset,
.unload_game=retro_unload_game,
.load_game=retro_load_game,
.get_system_info=retro_get_system_info,
.get_system_av_info=retro_get_system_av_info,
.get_memory_data=retro_get_memory_data,
.get_memory_size=retro_get_memory_size,
.serialize_size=retro_serialize_size,
.serialize=retro_serialize,
.unserialize=retro_unserialize,
};
static bb_retro_host test_host;
static uint64_t host_tick(void){return (uint64_t)(now()*1000000);}
static bool host_picture(const void *p,unsigned w,unsigned h,size_t pitch,enum retro_pixel_format f)
{(void)f;video(p,w,h,pitch);return true;}
static void host_audio(const int16_t *p,size_t n){audio(p,n);}
static bool host_env(unsigned cmd,void *p){return env(cmd,p);}
static const bb_retro_ext test_ext={.environment=host_env};
static void host_bind(void){
 static bb_retro_desc d={.core=&test_core,.ext=&test_ext,.system_dir=".",.max_width=240,.max_height=160};
 static const bb_retro_services s={.ticks=host_tick,.ticks_per_second=1000000,.video=host_picture,.audio=host_audio};
 for(unsigned i=0;i<16;i++)d.keys[i]=1u<<i;
 if(!bb_retro_bind(&test_host,&d,&s))abort();
}

