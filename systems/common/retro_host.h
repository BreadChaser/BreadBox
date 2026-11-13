/* Shared libretro session mechanics. GPL-2.0-or-later.
 * One active core: libretro callbacks have no userdata. No filesystem/UI policy;
 * save service and pause shell own files, history, Continue and APT decisions. */
#ifndef BB_RETRO_HOST_H
#define BB_RETRO_HOST_H
#include <libretro.h>
#include "bb_system.h"
#include "retro_clock.h"
typedef struct {
 void (*cheat_reset)(void);void (*cheat_set)(unsigned,bool,const char *);
 bool (*activate)(void); void (*deactivate)(void); /* generated per-core C++ arrays */
 void (*set_environment)(retro_environment_t);
 void (*set_video_refresh)(retro_video_refresh_t);
 void (*set_audio_sample)(retro_audio_sample_t);
 void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
 void (*set_input_poll)(retro_input_poll_t);
 void (*set_input_state)(retro_input_state_t);
 void (*init)(void), (*deinit)(void), (*run)(void), (*reset)(void), (*unload_game)(void);
 bool (*load_game)(const struct retro_game_info *);
 void (*get_system_info)(struct retro_system_info *);
 void (*get_system_av_info)(struct retro_system_av_info *);
 void *(*get_memory_data)(unsigned);
 size_t (*get_memory_size)(unsigned), (*serialize_size)(void);
 bool (*serialize)(void *,size_t), (*unserialize)(const void *,size_t);
} bb_retro_core;
typedef struct {
 /* Called before standard environment handling. Return true when handled. */
 bool (*environment)(unsigned,void *);
 void (*before_frame)(bool *skip);
 void (*after_frame)(void);
 /* true: extension presented this frame (e.g. layered/indexed compositor). */
 bool (*video)(const void *,unsigned,unsigned,size_t);
 bool (*prepare)(const struct retro_game_info *);
 void (*release)(void);
 /* RTC opt-in; RAM only. No automatic filesystem writes. */
 size_t (*rtc_size)(void);
 bool (*rtc_get)(void *,size_t), (*rtc_set)(const void *,size_t);
 uint32_t (*battery_generation)(void);
} bb_retro_ext;
typedef struct {
 const bb_system_t *system;
 const bb_retro_core *core;
 const bb_retro_ext *ext;
 const char *system_dir, *save_dir;
 const struct retro_variable *variables; /* terminated by key == NULL */
 uint32_t keys[16];
 /* Optional shared turbo A/B controls, normally bound to the physical X/Y keys. */
 uint32_t turbo_a_key,turbo_b_key;
 unsigned max_width,max_height;
 unsigned screen_size; /* 0 Original, 1 Fit, 2 Stretch */
 bool filter;
 bool common_settings; /* schema rows: screen size, filter, audio */
 /* Externally paced adapters own upload/NULL/watchdog policy. Installing the
  * callback directly avoids dispatch and a second tick read per picture.
  * Such adapters use bind/run_frame, never step/pace. */
 retro_video_refresh_t video_refresh;
 uint64_t fps_num,fps_den; /* optional exact clock ratio, AV fps otherwise */
} bb_retro_desc;
typedef struct {
 /* Device services; platform adapter uses audio.c and the texture presenter. */
 uint64_t (*ticks)(void);
 uint64_t ticks_per_second;
 void (*sleep)(uint64_t ticks);
 bool (*video)(const void *,unsigned,unsigned,size_t,enum retro_pixel_format);
 void (*audio)(const int16_t *,size_t);
 void (*log)(const char *);
} bb_retro_services;
typedef struct {
 const bb_retro_desc *desc;
 bb_retro_services services;
 struct retro_system_av_info av;
 enum retro_pixel_format format;
 bb_retro_clock clock;
 uint32_t held;
 bool loaded,initialized,skip,dirty,picture,shutdown,stalled,invalid;
 uint64_t frames,audio_frames,audio_nonzero,last_video;
 uint32_t audio_hash;
 /* PCM auditing is diagnostic work, independent of delivery to the device. */
 bool audit_audio;
} bb_retro_host;
bool bb_retro_open(bb_retro_host *,const bb_retro_desc *,const bb_retro_services *,const struct retro_game_info *);
void bb_retro_close(bb_retro_host *);
/* Migration entry: install shared callbacks, then let the adapter own init/load
 * and its existing APT/save sequence until the save service lands. */
bool bb_retro_bind(bb_retro_host *,const bb_retro_desc *,const bb_retro_services *);
void bb_retro_unbind(bb_retro_host *);
void bb_retro_run_frame(bb_retro_host *,uint32_t,bool);
size_t bb_retro_rtc_size(bb_retro_host *);
bool bb_retro_rtc_get(bb_retro_host *,void *,size_t);
bool bb_retro_rtc_set(bb_retro_host *,const void *,size_t);
/* One CPU/audio frame. Caller handles events/pause, battery polling and display.
 * Returns false on shutdown/stall; sleep/pacing is explicit after presentation. */
bool bb_retro_step(bb_retro_host *,uint32_t held);
void bb_retro_pace(bb_retro_host *);
void bb_retro_rebase(bb_retro_host *);
bool bb_retro_battery(bb_retro_host *,void **,size_t *);
bool bb_retro_state_save(bb_retro_host *,void *,size_t,size_t *);
bb_state_result bb_retro_state_load(bb_retro_host *,const void *,size_t,bool);
/* Unaligned, pitched source -> RGB565 PICA tiles. False leaves output intact. */
bool bb_retro_upload(uint16_t *,unsigned,unsigned,const void *,unsigned,unsigned,size_t,enum retro_pixel_format);
void bb_retro_scale_aspect(unsigned,unsigned,float,unsigned,float *,float *);
void bb_retro_scale(unsigned,unsigned,unsigned,float *,float *);
#endif
