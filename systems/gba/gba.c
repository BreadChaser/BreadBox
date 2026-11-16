#include <time.h>
/* BreadBox's gpSP host. PS1 never calls through this adapter. */

#include "app_policy.h"
#include <string.h>
#include "libretro.h"
#include "history.h"
#include "theme.h"
#include "ui_format.h"
#include "app_paths.h"
#include "app_log.h"
#include "platform_video.h"
#include "dev_switches.h"
#include "ps1_hooks.h"
#include "audio.h"
#include "app_input.h"
#include "ui_list.h"
#include "ui_pause.h"
#include "ui.h"
#include "playlog.h"
#include "home.h"
#include "app_main.h"
#include "gba.h"
#include "bb_arena.h"
#include "bb_system.h"
#include "../common/retro_host.h"
#include "gba_upload.h"
#include "bb_save.h"
#include "bb_pause.h"
extern int gba_remote_cpuopt,gba_remote_journal,gba_remote_indexed,gba_remote_latch;
#include "index_gpu.h"
static bool indexed_ready,indexed_picture;
extern void gpsp_gba_journal_config(unsigned,void*);
extern void gpsp_gba_journal_read(u64*,unsigned*);
static void *journal_atlas;
extern void gpsp_gba_cpu_config(unsigned);
extern int gba_remote_prof;
#ifdef BBGBA_TIMING
extern void gpsp_gba_timing_config(int);
extern void gpsp_gba_timing_read(u64 *,unsigned *);
static u64 cpu_diag[3];
#define DIAG_START(name) u64 name=gba_remote_prof?svcGetSystemTick():0
#define DIAG_END(name,n) do {if(gba_remote_prof)cpu_diag[n]+=svcGetSystemTick()-name;}while(0)
#else
#define DIAG_START(name) ((void)0)
#define DIAG_END(name,n) ((void)0)
#endif
#include "gba_settings.h"
#include "gba_speed.h"
#include "gba_touch.h"
#include "retro_rewind.h"
#include "statsview.h"
#include "bb_feature_ui.h"
extern void bb_gba_cheats_open(const char *);
extern void bb_gba_cheats_close(void);
#include "gba_cart.h"
#include "bb_index.h"
#include "gba_timing.h"
#include "safefile.h"
#include "history.h"
#include "gba_saveid.h"
#include "devtools.h"
#include "scene.h"
#include <libretro.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <errno.h>
#include <zlib.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

/* gpSP's partial-linked globals all have gpsp_ prefixes. */
void gpsp_retro_set_environment(retro_environment_t);
void gpsp_retro_set_video_refresh(retro_video_refresh_t);
void gpsp_retro_set_audio_sample(retro_audio_sample_t);
void gpsp_retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void gpsp_retro_set_input_poll(retro_input_poll_t);
void gpsp_retro_set_input_state(retro_input_state_t);
extern int gpsp_dynarec_enable;
void gpsp_retro_init(void);
void gpsp_retro_deinit(void);
bool gpsp_retro_load_game(const struct retro_game_info *);
void gpsp_retro_unload_game(void);
void gpsp_retro_run(void);
void gpsp_retro_set_run_guard(bool (*guard)(void));
void gpsp_retro_reset(void);
void gpsp_retro_get_system_av_info(struct retro_system_av_info *);
void *gpsp_retro_get_memory_data(unsigned);
size_t gpsp_retro_get_memory_size(unsigned);
size_t gpsp_retro_serialize_size(void);
bool gpsp_retro_serialize(void *, size_t);
bool gpsp_retro_unserialize(const void *, size_t);

extern bool gpsp_ctr_jit_has_mappings(void);

/* libctru calls this after main/exit(), before __libctru_exit bulk-frees the
 * regular heap. Its aliased JIT pages must remain owned until svcExitProcess.
 * A zero size makes that bulk FREE fail validation without touching any page;
 * linear-heap and service cleanup stay intact. Do not return to the HBL loader
 * with live aliases: force the final svcExitProcess path instead.
 * The kernel reclaims the retained heap, aliases and handle as one process. */
void userAppExit(void)
{
    /* In-place static arena: no heap aliases survive process exit. */
}

GbaSettings gba_settings __attribute__((weak)) = {1, 0, 0, 0, 0, 5};
#define GBA_SAVES APP_SD_ROOT "/saves/gba"
#define GBA_STATES APP_SD_ROOT "/states/gba"
#define GBA_FPS (16777216.0 / 280896.0)
#define GBA_SAVE_BYTES 0x20000

static C3D_Tex texture, right_texture;
static u16 *right_pixels;
static bool right_ready, stereo_frame;
extern void gpsp_gba_stereo_config(u16 *, unsigned);
extern int gpsp_gba_stereo_valid(void);
extern unsigned gpsp_gba_stereo_flags(void);
extern void gpsp_retro_profile_read(unsigned long long *out);
extern void gpsp_retro_set_solar_level(unsigned);
extern int gpsp_retro_has_solar_sensor(void);
extern int gpsp_retro_get_rtc(u32 out[3]);
extern void gpsp_retro_set_rtc(const u32 in[3]);
extern void gpsp_gba_stereo_admit(int);
extern void gpsp_gba_rom_budget(unsigned);
#include <assert.h>
extern const bb_system_t bb_system_gba;
static bool core_held(void)
{
    bool held=bb_exec_held() && bb_session_system()==&bb_system_gba;
#ifdef BB_DEBUG_ARENA
    assert(held && "gpSP entry outside its arena loan");
#endif
    return held;
}
extern u32 gpsp_gba_battery_generation(void);
/* Generated entry guards preserve direct-call layout. */
#include "retro_guards.h"

/* Differential hardware samples include eye composition, upload and presentation.
 * Baseline is ordinary mono, never a peak or a timing-model estimate. */
static gba_costs mono_costs, extra_costs, present_costs, skipped_costs;
static u64 stereo_mono_cost, stereo_extra_cost, stereo_present_cost;
static unsigned stereo_mode, stereo_review, stereo_phase, stereo_debt;
static bool stereo_attempt, stereo_force;
static bool skip_video;
/* Many GBA games alternate two pictures every frame and rely on the slow GBA
 * LCD to merge them: Classic NES Series titles pick different NES lines and
 * columns each frame, others flicker sprites at 30 Hz. The 3DS LCD shows each
 * phase sharply and every skipped or late frame repeats one phase: flicker.
 * Like stereo's 30-pair cadence, mono then shows only one phase: every other
 * frame is run without video. Every 16 frames a probe renders two of them
 * (never shown) so the lock ends when the game stops alternating. */
static bool probe_frame, phase_lock;
static unsigned lock_hold, probe_count;
/* frame-text.ld places it in padding, so no pinned function moves. */
__attribute__((noinline)) static void gba_track_alternation(const void *data, size_t pitch);

static void gba_stereo_prepare(bool late) {
    float slider=slider_get();
    unsigned strength=gba_settings.stereo && gba_3d_available() && !gba_settings.color_correction && !gba_settings.frame_blend && slider>.01f ? (unsigned)(slider*256.0f) : 0;
    if(!strength && right_ready && gspHasGpuRight()) { gpsp_gba_stereo_config(NULL,0); video_gpu_idle(); C3D_TexDelete(&right_texture); free(right_pixels); right_pixels=NULL; right_ready=stereo_frame=false; }
    if(strength && !right_ready && gspHasGpuRight()) { right_pixels=malloc(240*160*2); if(right_pixels && C3D_TexInit(&right_texture,256,256,GPU_RGB565)) { right_ready=true; memset(right_texture.data,0,right_texture.size); C3D_TexSetWrap(&right_texture,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE); } else { free(right_pixels); right_pixels=NULL; } }
    if(right_ready) C3D_TexSetFilter(&right_texture,gba_settings.smoothing?GPU_LINEAR:GPU_NEAREST,gba_settings.smoothing?GPU_LINEAR:GPU_NEAREST);
    ui_wants_3d=right_ready && strength; gpsp_gba_stereo_config(right_pixels,ui_wants_3d?strength:0);
    stereo_attempt=false;
    if(!ui_wants_3d) {stereo_mode=stereo_phase=stereo_review=stereo_debt=0;gpsp_gba_stereo_admit(0);return;}
    /* Calibrate once before entering stereo. Never use mono probe pictures
     * after admission: both eyes must keep the same scene and disparity. */
    if(mono_costs.count>=30 && !skipped_costs.count && !stereo_force) skip_video=true;
    if(mono_costs.count>=30 && (!stereo_mode || !stereo_review)) {
        stereo_mono_cost=gba_cost_median(&mono_costs);
        stereo_present_cost=gba_cost_median(&present_costs);
        stereo_extra_cost=gba_cost_median(&extra_costs);
        u64 estimate=extra_costs.count?stereo_extra_cost:SYSCLOCK_ARM11*(g_new3ds?1:2)/1000;
        u64 skipped=skipped_costs.count?gba_cost_median(&skipped_costs):stereo_mono_cost;
        unsigned predicted=gba_stereo_mode(stereo_mono_cost,estimate,stereo_present_cost,skipped,
            (u64)(SYSCLOCK_ARM11/GBA_FPS),SYSCLOCK_ARM11/1000);
        stereo_mode=gba_stereo_latch(stereo_mode,predicted);
        stereo_review=180;
    }
    if(stereo_review) --stereo_review;
    if(stereo_force && mono_costs.count>=30) stereo_mode=60;
    /* At 30, the intervening CPU/audio-only frame keeps BOTH previous eyes.
     * A late 60-picture frame is still stereo; sustained debt latches 30. */
    stereo_attempt=gba_stereo_draw(stereo_mode,&stereo_phase,late,stereo_force,&skip_video);
    gpsp_gba_stereo_admit(stereo_attempt);
}
static Tex3DS_SubTexture subtexture = {240, 160, 0, 1, 240.0f/256, 1-160.0f/256};
static bool texture_ready, audio_ready;
static bool game_active;
bool gba_layer3d_display(void) { return game_active && stereo_frame; }
static char error_text[256], save_path[PATH_LEN], state_base[PATH_LEN-24];
static u32 rom_identity, rom_bytes;
static char save_id[GBA_ID_LEN];
static char auto_path[PATH_LEN];
static unsigned test_frames, frames, test_mode;
static bb_retro_host retro_host;
#define audio_frames retro_host.audio_frames
#define audio_nonzero retro_host.audio_nonzero
#define audio_hash retro_host.audio_hash
static u32 held;
/* fast-forward and rewind frames make no sound; ▶▶/◀◀ on the top screen */
static bool audio_muted;
static int speed_icon;


static char notice[120];
static u64 notice_until;
static bool bottom_dirty,top_dirty,notice_visible;
static bool touch_intro_pending,touch_intro_painted,touch_intro_visible;
static unsigned bottom_paints;
static bool vars_dirty, clock_reset;

static void memory_log(const char *where)
{
    struct mallinfo mi=mallinfo();
    log_printf("gba: memory %s heap_used=%u heap_free=%u linear_free=%u\n",
        where,(unsigned)mi.uordblks,bb_heap_available(),(unsigned)linearSpaceFree());
}

bool gba_active(void) { return game_active; }
const char *gba_error(void) { return error_text[0] ? error_text : "Could not start the GBA game"; }
static void message(const char *text)
{
    u64 now=svcGetSystemTick();
    touch_intro_visible=false;
    if(strcmp(notice,text) || now>=notice_until)bottom_dirty=top_dirty=true;
    snprintf(notice, sizeof(notice), "%s", text);
    notice_until = now + 3ULL * SYSCLOCK_ARM11;notice_visible=true;
}
static void touch_intro_did_paint(void)
{
 // The compositor samples/clears this state before calling draw_bottom.
 // A shaded paint cannot start or acknowledge a first-use instruction.
 if(__atomic_load_n(&ui_resume_pending,__ATOMIC_ACQUIRE))return;
 if(!touch_intro_painted) {
  notice_until=svcGetSystemTick()+3ULL*SYSCLOCK_ARM11;
  notice_visible=touch_intro_visible=true;
 }
 touch_intro_painted=true;
}
static bool environment(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        const char *value = NULL;
        if (!strcmp(v->key, "gpsp_drc")) value = "enabled";
        else if (!strcmp(v->key, "gpsp_bios")) value = "auto";
        else if (!strcmp(v->key, "gpsp_boot_mode")) value = "game";
        else if (!strcmp(v->key, "gpsp_sound_rate")) value = "32768";
        else if (!strcmp(v->key, "gpsp_frameskip")) value = "disabled";
        else if (!strcmp(v->key, "gpsp_rtc_time_source")) value = "system";
        else if (!strcmp(v->key, "gpsp_rtc")) value = "auto";
        else if (!strcmp(v->key, "gpsp_color_correction")) value = gba_settings.color_correction ? "enabled" : "disabled";
        else if (!strcmp(v->key, "gpsp_frame_mixing")) value = gba_settings.frame_blend ? "enabled" : "disabled";
        else if (!strcmp(v->key, "gpsp_serial")) value = "disabled";
        else if (!strcmp(v->key, "gpsp_rumble")) value = "disabled";
        v->value = value; return value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        *(unsigned *)data = skip_video && !probe_frame ? 2 : 3; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = vars_dirty; vars_dirty = false; return true;
    default: return false;
    }
}

/* PICA tiled RGB565 texture. Open the next command buffer, then finish the
 * preceding submission before modifying its texture. The newly opened frame
 * is submitted by video_present, without two empty GPU submissions per frame. */
static unsigned morton(unsigned x, unsigned y)
{
    return (x&1) | ((y&1)<<1) | ((x&2)<<1) | ((y&2)<<2) | ((x&4)<<2) | ((y&4)<<3);
}
static unsigned dev_drawn;
static bool picture_ready;
static unsigned picture_null, picture_blank, picture_repeat;
/* A start holds the shared dim over the game's blank white/black first pictures and lifts it at the first
 * real one (or after BOOT_HOLD_FRAMES), instead of showing a white flash. The logic sits at the end of the
 * file, so the pinned frame code (gba_run, video_callback) only grows by the calls. */
#define BOOT_HOLD_FRAMES 90
static bool boot_hold;
static void boot_begin(bool on);
static void boot_sample(const void *data,size_t pitch);
static void boot_step(u64 paused_ticks,unsigned frame,bool picture);
static void boot_end(void);
static void set_menu_title(const char *name);
static u32 picture_hash;
static u64 run_deadline, last_video;
static bool run_stalled;
static bool frame_guard(void)
{
    if(svcGetSystemTick()<run_deadline)return false;
    run_stalled=true;return true;
}
static void video_callback(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (!data || w != 240 || h != 160 || !texture_ready) { ++picture_null; return; }
    last_video=svcGetSystemTick();
    if(!ui_wants_3d) gba_track_alternation(data,pitch);
    if(probe_frame) return;
    if(dev_remote)dev_drawn++;
    DIAG_START(tb);video_gpu_frame_begin();DIAG_END(tb,0);
    DIAG_START(tw);video_gpu_wait_previous();DIAG_END(tw,1);
    DIAG_START(tu);
    indexed_picture=indexed_ready && gba_index_gpu_render(data,pitch,right_pixels);
    u16 *out = texture.data;
    if(!indexed_picture) {
    if(gba_remote_cpuopt&8) gba_upload_tiles_local(out,data,pitch);
    else if(gba_remote_cpuopt&1) gba_upload_tiles(out,data,pitch);
    else for (unsigned y = 0; y < h; y++) {
        const u16 *in = (const u16 *)((const u8 *)data + y*pitch);
        for (unsigned x = 0; x < w; x++)
            out[((y>>3)*32+(x>>3))*64+morton(x,y)] = in[x];
    }
    GSPGPU_FlushDataCache(out, texture.size);
    }
    stereo_frame=ui_wants_3d && gpsp_gba_stereo_valid() && !gba_settings.color_correction && !gba_settings.frame_blend;
    bb_session_set_stereo(stereo_frame);
    ui_wants_3d=stereo_frame;
    if(stereo_frame && !indexed_picture) { out=right_texture.data; if(gba_remote_cpuopt&8) gba_upload_tiles_local(out,right_pixels,240*2);else if(gba_remote_cpuopt&1) gba_upload_tiles(out,right_pixels,240*2);else for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) out[((y>>3)*32+(x>>3))*64+morton(x,y)]=right_pixels[y*240+x]; GSPGPU_FlushDataCache(out,right_texture.size); }
    /* Citro2D keeps curTex across frames. With an empty bottom screen the
     * next picture uses the same texture object, so merely cleaning ARM data
     * does not invalidate PICA's texture cache. Re-prepare the 2D batch after
     * every upload, even in mono; its next image bind clears that cache. */
    C2D_Prepare();DIAG_END(tu,2);
    picture_ready=true;
    if(boot_hold)boot_sample(data,pitch);
    if(dev_remote) {
        u32 hash=2166136261u;bool black=true,white=true;
        for(unsigned y=0;y<h;y+=20) for(unsigned x=0;x<w;x+=30) {
            u16 pixel=*(const u16 *)((const u8 *)data+y*pitch+x*2);
            hash=(hash^pixel)*16777619u;black &= pixel==0;white &= pixel==0xffff;
        }
        picture_blank+=black || white;picture_repeat+=hash==picture_hash;picture_hash=hash;
    }
}
static void audio_sink(const int16_t *data,size_t n)
{
    if(audio_ready && !audio_muted)audio_write(data,n);
}
/* libctru flips its back-buffer index at each swap, but GSP shows the new
 * buffer only at the next VBlank. A mono picture presented within the same
 * LCD refresh as the previous one would be copied into the buffer still on
 * screen (a tear) and would replace the unshown picture (a drop). Wait for
 * the latch; stereo pairs at 30 never present twice per refresh. */
static unsigned latch_waits;
static u64 latch_ticks;
/* frame-text.ld places it in padding, so no pinned function moves. */
__attribute__((noinline)) static u64 wait_top_latch(void);
static u64 host_ticks(void) {return svcGetSystemTick();}
static void host_log(const char *text) {log_printf("gba: %s",text);}
extern const bb_retro_core bb_retro_gba;
static uint32_t host_generation(void){return gpsp_gba_battery_generation();}
static size_t host_rtc_size(void){return 3*sizeof(u32);}
static bool host_rtc_get(void *p,size_t n){return n==3*sizeof(u32) && gpsp_retro_get_rtc(p);}
static bool host_rtc_set(const void *p,size_t n){if(n!=3*sizeof(u32))return false;gpsp_retro_set_rtc(p);return true;}
static const bb_retro_ext extensions={.environment=environment,.rtc_size=host_rtc_size,.rtc_get=host_rtc_get,.rtc_set=host_rtc_set,.battery_generation=host_generation};
static bb_retro_desc descriptor={.system=&bb_system_gba,.core=&bb_retro_gba,.ext=&extensions,
    .system_dir=APP_SD_ROOT "/bios",.save_dir=GBA_SAVES,.max_width=240,.max_height=160,
    .fps_num=16777216,.fps_den=280896,.video_refresh=video_callback,
    .keys={[RETRO_DEVICE_ID_JOYPAD_A]=KEY_A,[RETRO_DEVICE_ID_JOYPAD_B]=KEY_B,
    [RETRO_DEVICE_ID_JOYPAD_START]=KEY_START,[RETRO_DEVICE_ID_JOYPAD_SELECT]=KEY_SELECT,
    [RETRO_DEVICE_ID_JOYPAD_L]=KEY_L,[RETRO_DEVICE_ID_JOYPAD_R]=KEY_R,
    [RETRO_DEVICE_ID_JOYPAD_UP]=KEY_UP|KEY_CPAD_UP,[RETRO_DEVICE_ID_JOYPAD_DOWN]=KEY_DOWN|KEY_CPAD_DOWN,
    [RETRO_DEVICE_ID_JOYPAD_LEFT]=KEY_LEFT|KEY_CPAD_LEFT,[RETRO_DEVICE_ID_JOYPAD_RIGHT]=KEY_RIGHT|KEY_CPAD_RIGHT}};
static void touch_prepare(void)
{
 bb_controls_map(&bb_active_controls,descriptor.keys,&descriptor.turbo_a_key,&descriptor.turbo_b_key);
 gba_touch_begin(descriptor.keys,&descriptor.turbo_a_key,&descriptor.turbo_b_key,g_new3ds,
  gba_speed_rewind_allowed() && bb_rewind_active());
 gba_touch_lit(gba_speed_ff());bottom_dirty=true;
}
static bool fps_pending;
static void save_fps(void) {fps_pending=true;}
static void flush_fps(void) {if(fps_pending){gba_touch_fps_save_global();fps_pending=false;}}
static const bb_retro_services services={.ticks=host_ticks,.ticks_per_second=SYSCLOCK_ARM11,.audio=audio_sink,.log=host_log};

static void draw_top(void *unused)
{
    (void)unused;
    if (!texture_ready) return;
    C2D_Image image = {indexed_picture?gba_index_gpu_texture(ui_eye>0 && stereo_frame):ui_eye>0 && stereo_frame ? &right_texture : &texture, &subtexture};
    float sx, sy;
    if (gba_settings.screen_size == 0) sx = sy = 1.0f;
    else if (gba_settings.screen_size == 2) { sx = 400.0f/240.0f; sy = 240.0f/160.0f; }
    else { sx = sy = 1.5f; } /* 360x240: preserve the GBA's 3:2 picture. */
    C2D_DrawImageAt(image, (400-240*sx)/2, (240-160*sy)/2, 0.5f, NULL, sx, sy);
}
static void draw_bottom(void *unused); /* rendered by the shared gba component */
static void draw_game_top(void *unused); /* rendered by the shared gba component */

static char menu_title[256];
static void save_notice(const char *text){bb_pause_notice(text);message(text);}
/* A cartridge save (or clock) that is damaged and has no good copy: asked once before the game
 * runs (bb_save.h). Remote test loads never wait: a backup is put back, otherwise no start. */
#include "cold_code.h"
extern bool gba_save_declined;   /* sys_gba.c: the player went back at that question, no error */
/* A save file of a size another emulator writes for this cartridge (bb_save_damage.foreign): the same
 * question with "Use it" added. Restore (or Back without a backup) is chosen first; B goes back.
 * Words as literals (not arrays in this file's data): the frame code's data addresses stay put. */
#define FOREIGN_TITLE "This save looks different"
#define FOREIGN_RESTORE "It may be from another emulator. Use it, or go back to a BreadBox backup?"
#define FOREIGN_ALONE "It may be from another emulator. Use it, or start empty?"
#define ANSWER_USE "Use it"
#define ANSWER_RESTORE "Restore backup"
#define ANSWER_EMPTY "Start empty"
#define ANSWER_BACK "Back"
PSX3D_COLD static int save_foreign_ask(const bb_save_damage *d)
{
    const char *words[4];int answers[4],n=0;
    words[n]=ANSWER_USE;answers[n++]=BB_DAMAGE_USE;
    words[n]=ANSWER_EMPTY;answers[n++]=BB_DAMAGE_EMPTY;
    if(d->backup){words[n]=ANSWER_RESTORE;answers[n++]=BB_DAMAGE_RESTORE;}
    int safe=n-(d->backup?1:0);   /* Restore, or Back when there is no backup */
    words[n]=ANSWER_BACK;answers[n++]=BB_DAMAGE_BACK;
    int i=ui_confirm_pick_n(FOREIGN_TITLE,d->backup?FOREIGN_RESTORE:FOREIGN_ALONE,words,n,safe);
    return i<0?BB_DAMAGE_BACK:answers[i];
}
PSX3D_COLD static int save_damaged_ask(const bb_save_damage *d)
{
    const char *head="Game save problem",*empty="A  Start empty",*back="B  Back";
    if(dev_remote_game())return d->backup?BB_DAMAGE_RESTORE:BB_DAMAGE_BACK;
    if(d->foreign && !d->clock)return save_foreign_ask(d);
    if(d->clock)return ui_confirm_choice("Game clock problem","The game's clock file is damaged. Start the clock over? "
        "The damaged file stays on the SD card as .bad.","A  Start over",back,false)?BB_DAMAGE_EMPTY:BB_DAMAGE_BACK;
    if(d->backup) {
        char when[40],msg[200];ui_format_when(when,sizeof(when),d->when);
        snprintf(msg,sizeof(msg),"This game's save is damaged. A copy from %s is in Backups.",when);
        if(ui_confirm_choice(head,msg,"A  Restore","B  Other choices",false))return BB_DAMAGE_RESTORE;
    }
    return ui_confirm_choice(head,d->backup?"Start with an empty save instead? The damaged file stays on the SD card as .bad.":
        "This game's save is damaged and has no good backup. Start with an empty save? "
        "The damaged file stays on the SD card as .bad.",empty,back,false)?BB_DAMAGE_EMPTY:BB_DAMAGE_BACK;
}
PSX3D_COLD int gba_save_damaged(const bb_save_damage *d)
{
    int answer=save_damaged_ask(d);
    gba_save_declined=answer==BB_DAMAGE_BACK;
    if(gba_save_declined)log_printf("gba: quit at the damaged-save question\n");
    return answer;
}
static void state_restored(void)
{gba_speed_reset();if(audio_ready){audio_exit();audio_ready=audio_init(32768.0);audio_pause(true);}}
static bool save_battery(void){return bb_save_flush();}
static bool queue_battery(void){return bb_save_queue();}
static bool state_save(unsigned slot){return bb_save_state(slot,true);}
static bool state_load(unsigned slot){return bb_save_state(slot,false);}
static void session_pause(bool paused){audio_pause(paused);playlog_pause(paused);}
/* Save postcards are written from linear memory, never the game's heap. */
static void *picture_memory;
/* The last picture for save postcards, from the CPU copy of the LCD texture
 * (no GPU). The opt-in indexed GPU path has no CPU copy: no picture. */
static bool session_picture(uint16_t *px,unsigned *w,unsigned *h)
{
 if(!texture_ready || indexed_picture)return false;
 const u16 *in=texture.data;
 for(unsigned y=0;y<160;y++)for(unsigned x=0;x<240;x++)px[y*240+x]=in[((y>>3)*32+(x>>3))*64+morton(x,y)];
 *w=240;*h=160;return true;
}
static void session_rebase(void){clock_reset=true;}
static void session_reset(void)
{
 u32 rtc[3];bool has_rtc=gpsp_retro_get_rtc(rtc);
 gpsp_retro_reset();if(has_rtc)gpsp_retro_set_rtc(rtc);
 gba_speed_reset();
}
static int setting_row;
static flist_t settings_list;
static void draw_settings_help(void *unused);
static void draw_gba_settings(void *unused); /* rendered by the shared gba component */
static void apply_filter(void)
{
    if(gspHasGpuRight()){video_gpu_idle();C3D_TexSetFilter(&texture,gba_settings.smoothing?GPU_LINEAR:GPU_NEAREST,gba_settings.smoothing?GPU_LINEAR:GPU_NEAREST);}
}
static bool settings_menu(void)
{
    setting_row=0;
    while(aptMainLoop()) {
        if(dev_tick(true)) return true;
        input_poll();
        int rows=gba_setting_rows();
        static const ui_button_t bar[]={{KEY_B,"Back",UI_ICON_BACK,false},{KEY_LEFT,"Change",UI_ICON_LEFT,false},{KEY_RIGHT,"Change",UI_ICON_RIGHT,false}};
        u32 k=ui_list_input(&settings_list,rows,&setting_row,ui_bar_input(bar,3,input_keys_down()),false);
        k=ui_swap_keys(k,KEY_B|KEY_Y);
        if(k&KEY_B) return false;
        if(k&KEY_X){gba_setting_clear(setting_row);vars_dirty=true;
            if(gba_setting_field(setting_row)==GBA_SET_SMOOTH)apply_filter();}
        if(k&KEY_Y && bb_pause_confirm("Reset this game?","Use Game Boy Advance defaults for this game? Saves are kept.","Reset this game")) {
            for(int i=0;i<rows;i++) { gba_setting_clear(i); }
            vars_dirty=true;
            apply_filter();
        }
        if(k&(KEY_LEFT|KEY_RIGHT|KEY_A)) {
            int dir=(k&KEY_LEFT)?-1:1;
            gba_setting_change(setting_row,dir);
            vars_dirty=true;
            if(gba_setting_field(setting_row)==GBA_SET_SMOOTH)apply_filter();
            gba_settings_save();
        }
        if(!gspHasGpuRight())return true;
        flist_update(&settings_list,setting_row,rows,UI_LIST_ROWS);
        ui_begin(); video_present_vsync(false,draw_settings_help,draw_gba_settings,NULL);
    }
    return true; // system Close: the caller must not issue another GPU frame
}
/* libctru 2.7.0 dispatches these hooks synchronously from aptMainLoop
 * (aptHandleSleep/aptJumpToHomeMenu), on our main/core thread. No APT
 * worker writes save files or runs the core; see docs/gba-save-safety.md. */
static void apt_event(APT_HookType hook,void *unused)
{
    (void)unused;
    if(hook==APTHOOK_ONSUSPEND || hook==APTHOOK_ONSLEEP || hook==APTHOOK_ONEXIT) {gba_touch_reset();bb_pause_suspend(game_active && !run_stalled);}
    if(hook==APTHOOK_ONRESTORE || hook==APTHOOK_ONWAKEUP) {
        bb_pause_restore();
    }
}

/* Early validation/allocation failures still finish a pending remote load.
 * Otherwise its reply waits forever while the local error panel is open. */
static int start_failed(void)
{
    dev_game_ended(false);
    return -1;
}

static unsigned prepare_percent;
static void draw_prepare(void *unused); /* rendered by the shared gba component */
static bool prepare_progress(unsigned bytes, unsigned total)
{
    if (!aptMainLoop()) return false;
    prepare_percent = total ? (unsigned)((u64)bytes * 100 / total) : 0;
    // Preserve the source OUT picture through cartridge preparation. Sampling
    // WAIT here would spend the game reveal on this temporary progress card.
    if (!__atomic_load_n(&ui_resume_pending,__ATOMIC_ACQUIRE))
        video_present_vsync(false, ui_menu_top, draw_prepare, NULL);
    return true;
}

static u32 debug_key;static unsigned debug_key_frames;
bool gba_debug_key(const char *key) {
 if(!dev_remote || !game_active || bb_pause_active() || debug_key_frames)return false;
 static const char *names[]={"a","b","start","select","up","down","left","right","l","r"};
 static const u32 keys[]={KEY_A,KEY_B,KEY_START,KEY_SELECT,KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT,KEY_L,KEY_R};
 for(unsigned i=0;i<10;i++)if(!strcmp(key,names[i])) {debug_key=keys[i];debug_key_frames=6;return true;}
 return false;
}

bool gba_debug_state(int slot,bool save) {
 if(!game_active || slot<0 || slot>BB_SLOT_LAST)return false;   /* a file index: devcontrol maps the remote's card number (bb_slots.h) */
 bool ok=save?state_save(slot):state_load(slot);
 if(!save && ok) {clock_reset=true;if(audio_ready)audio_pause(false);}
 return ok;
}

/* gba_test=2: fast-forward 60-239, rewind 300-419, slot 9 save/load after it;
 * gba_test=3: Continue at start, then rewind 60-179 (past the history's start). */
__attribute__((noinline)) static void gba_speed_test(unsigned f)
{
    unsigned ff_from=test_mode==2?60:~0u, rw_from=test_mode==2?300:60;
    gba_speed_force(f>=ff_from && f<ff_from+180,f>=rw_from && f<rw_from+120);
    u32 rtc[3]={0};
    if(f==rw_from || f==rw_from+120) {int has=gpsp_retro_get_rtc(rtc);log_printf("gba: speedtest frame=%u rtc=%d %08lx%08lx status=%lu kib=%u\n",f,has,(unsigned long)rtc[1],(unsigned long)rtc[0],(unsigned long)rtc[2],gba_speed_history_kib());}
    if(test_mode==2 && f==450) log_printf("gba: speedtest save9=%d\n",state_save(9));
    if(test_mode==2 && f==480) {log_printf("gba: speedtest load9=%d\n",state_load(9));audio_pause(false);}
}
static unsigned pause_rows(void) {return gba_settings_solar_game?1:0;}   /* Sunlight: Boktai only */
/* One shown frame while the rewind hotkey is held: step back (about 6 times
 * a second of game time, shown at 20/s like PS1), then the frame runs muted. */
__attribute__((noinline)) static void gba_rewind_frame(bool *hint,unsigned *shown)
{
    bool restart;
    int r=gba_speed_rewind_frame(&restart);
    if(r==GBA_REWIND_OFF) {
        if(!*hint) message("Enable Rewind in Settings first");
        *hint=true;return;
    }
    speed_icon=-1;++*shown;
    if(r==GBA_REWIND_END) message("Rewound as far as it goes");
    else if(r==GBA_REWIND_FAILED && restart) {session_reset();message("Rewind failed: the game was restarted");}
    else if(r==GBA_REWIND_FAILED) message("Rewind failed: the game goes on from here");
}
int gba_run(const char *path)
{
    error_text[0]=0; notice_until=0;notice_visible=false; vars_dirty=false;theme_game_begin();
    skip_video=clock_reset=run_stalled=false;
    picture_ready=false;picture_null=picture_blank=picture_repeat=picture_hash=0;
    debug_key_frames=0;dev_drawn=0;
    frames=0; audio_frames=audio_nonzero=0; audio_hash=2166136261u;
    mkdir(APP_SD_ROOT "/saves",0777); mkdir(GBA_SAVES,0777);
    mkdir(APP_SD_ROOT "/states",0777); mkdir(GBA_STATES,0777);
    const char *base=strrchr(path,'/'); base=base?base+1:path;
    char name[256];snprintf(name,sizeof(name),"%s",base);
    char *dot=strrchr(name,'.');if(dot)*dot=0;
    set_menu_title(name);   // as the library lists it: the room's "Last played" and the pause card read this
    bb_pause_config pause_config={.system=&bb_system_gba,.title=menu_title,.draw_game=draw_top,
        .pause=session_pause,.rebase=session_rebase,.reset=session_reset,.settings=settings_menu,.rows=pause_rows};
    bb_pause_begin(&pause_config);
    const char *rom_path=NULL;
    u64 prepare_at=osGetTime();
    gba_cart_result cart=gba_cart_prepare_progress(path,&rom_path,prepare_progress);
    log_printf("gba: phase=prepare result=%d ms=%llu\n", cart, (unsigned long long)(osGetTime()-prepare_at));
    if(cart!=GBA_CART_OK) {snprintf(error_text,sizeof(error_text),"%s",gba_cart_error(cart));return start_failed();}
    unsigned char header[192];
    if(!gba_cart_header(rom_path,header)) {snprintf(error_text,sizeof(error_text),"%s",gba_cart_error(GBA_CART_INVALID));return start_failed();}
    FILE *rom=fopen(rom_path,"rb");
    if(!rom) {snprintf(error_text,sizeof(error_text),"%s",gba_cart_error(GBA_CART_IO_ERROR));return start_failed();}
    fseek(rom,0,SEEK_END);long rom_length=ftell(rom);fclose(rom);
    rom_identity=crc32(0,header,sizeof(header));rom_bytes=rom_length;
    uint32_t content_crc;
    if(!gba_saveid_resolve(rom_path,header,&content_crc,save_id)) {
        snprintf(error_text,sizeof(error_text),"Cartridge changed or is still being indexed. Rescan your collection and retry.");return start_failed();
    }
    snprintf(save_path,sizeof(save_path),GBA_SAVES "/%s.sav",save_id);
    snprintf(state_base,sizeof(state_base),GBA_STATES "/%s",save_id);
    if(!gba_saveid_migrate(path,save_id)) {
        snprintf(error_text,sizeof(error_text),"Could not adopt existing saves. Check the SD card.");return start_failed();
    }
    gba_settings_for_identity(path,save_id);
    /* No 3D setting: GBA 3D follows the slider; legacy saved stereo=0 is ignored. */
    gba_settings.stereo=1;
    bb_controls_device_open(&bb_active_controls,"gba",save_id,true);
    bb_controls_map(&bb_active_controls,descriptor.keys,&descriptor.turbo_a_key,&descriptor.turbo_b_key);
    if(dev_remote_game() && gba_launch_stereo>=0)gba_settings.stereo=gba_launch_stereo!=0;
    stereo_force=dev_remote_game() && gba_launch_stereo==2;
    memory_log("menu");
    extern void gba_library_release(void);
    gba_library_release();
    memory_log("before");
    if(!C3D_TexInit(&texture,256,256,GPU_RGB565)) {
        snprintf(error_text,sizeof(error_text),"Not enough memory to show the GBA game");return start_failed();
    }
    texture_ready=true;memset(texture.data,0,texture.size);
    C3D_TexSetFilter(&texture,gba_settings.smoothing?GPU_LINEAR:GPU_NEAREST,gba_settings.smoothing?GPU_LINEAR:GPU_NEAREST);
    C3D_TexSetWrap(&texture,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    ui_wants_3d=false;
    if(!bb_exec_held()) {snprintf(error_text,sizeof(error_text),"System memory is busy");goto prepare_failed;}
    gpsp_retro_set_solar_level(gba_settings.solar);
    if(!bb_retro_bind(&retro_host,&descriptor,&services))goto prepare_failed;
    retro_host.audit_audio=gba_remote_prof!=0;
    history_session_begin((gba_remote_cpuopt&4)!=0,save_id);
    gpsp_gba_cpu_config(gba_remote_cpuopt);
    journal_atlas=gba_remote_journal==2?linearAlloc(6*2048*64*4):NULL;
    indexed_picture=false;indexed_ready=gba_remote_indexed && !gba_settings.color_correction && !gba_settings.frame_blend && gba_index_gpu_init(gba_remote_indexed);
    gpsp_gba_journal_config(journal_atlas?2:indexed_ready||gba_remote_journal==1?1:0,journal_atlas);

    unsigned available=bb_heap_available();
    /* State validation can hold two complete snapshots plus the writer's
     * battery copies and audio buffers. Leave a full extra block for fragmented
     * heaps after library covers; 2 MiB was insufficient on Old Wario. */
    const unsigned reserve=3*1024*1024;
    unsigned cache=available>reserve?(available-reserve)/(1024*1024):0;
    if(cache<4) {snprintf(error_text,sizeof(error_text),"Not enough memory for the cartridge cache");goto prepare_failed;}
    gpsp_gba_rom_budget(cache);
    log_printf("gba: ROM cache budget %u MiB (3 MiB save/audio reserve)\n",cache);
    log_printf("gba: phase=core-init begin\n");
    gpsp_retro_init();
    log_printf("gba: phase=core-init done\n");
    struct retro_game_info game={.path=rom_path};
    log_printf("gba: phase=core-load begin %s\n",rom_path);
    bool loaded=gpsp_retro_load_game(&game), battery_ok=false, went_back=false;
    if(loaded)bb_gba_cheats_open(save_id);
    retro_host.loaded=loaded;
    log_printf("gba: phase=core-load done loaded=%d\n",loaded);
    if(!loaded) snprintf(error_text,sizeof(error_text),"Could not load the GBA game\nTry an uncompressed .gba file.");
    if(loaded) {
        picture_memory=linearAlloc(BB_THUMB_BYTES);   /* NULL: saves without pictures */
        bb_save_config save_config={.system=&bb_system_gba,.key=save_id,.history_key=save_id,.battery_path=save_path,
            .state_base=state_base,.identity=rom_identity,.content_bytes=rom_bytes,
            .writer_core=g_new3ds?2:1,.notice=save_notice,.restored=state_restored,
            .picture=session_picture,.picture_buffer=picture_memory,.played=playlog_played};
        battery_ok=bb_save_begin(&save_config,error_text,sizeof(error_text));
    }
    struct retro_system_av_info av;
    gpsp_retro_get_system_av_info(&av);
    retro_host.av=av;
    audio_ready=loaded && battery_ok && !sw_nosound && audio_init(av.timing.sample_rate);
    /* Diagnostic intervals belong to this session, not the previous game's
     * final partial interval (the shared audio device keeps its counters). */
    if(audio_ready) {audio_pause(false);char prior[256];audio_stats(prior,sizeof(prior));}
    log_printf("gba: audio output=%s\n",audio_ready?"ready":sw_nosound?"disabled":"FAILED");
    log_printf("gba: start %s; fps %.8f audio %.1f Hz; engine=%s svc=%d; linear free %u\n",path,av.timing.fps,av.timing.sample_rate,gpsp_dynarec_enable?"ARM dynarec":"interpreter",__ctr_svchax,(unsigned)linearSpaceFree());
    game_active=loaded && battery_ok;
    gba_settings_solar_game=loaded && gpsp_retro_has_solar_sensor();   /* Boktai: game code U3I/U32/U33 */
    memory_log("playing");
    if(game_active) {
        char code[5];memcpy(code,header+0xac,4);code[4]=0;
        for(unsigned i=0;i<4;i++) {
            if(code[i]>='a'&&code[i]<='z')code[i]-='a'-'A';
            if(!((code[i]>='A'&&code[i]<='Z') || (code[i]>='0'&&code[i]<='9'))) {code[0]=0;break;}
        }
        char raw_id[GBA_ID_LEN],metadata_id[GBA_ID_LEN+8];unsigned key=2166136261u;
        for(const unsigned char *p=(const unsigned char *)path;*p;p++)key=(key^*p)*16777619u;
        gba_saveid_header(header,key,raw_id);
        snprintf(metadata_id,sizeof(metadata_id),"gba:%s",raw_id);
        playlog_adopt_key(raw_id,metadata_id);
        playlog_adopt_key(code[0]?code:name,metadata_id);
        playlog_adopt_key(save_id,metadata_id);
        playlog_begin(metadata_id);
        /* Reply only after the optional state is restored below. */
    }
    aptHookCookie cookie;
    aptHook(&cookie,apt_event,NULL);
    if(loaded && battery_ok) {
        bool resume=!dev_remote_game() && (gba_launch_slot==BB_SLOT_CONTINUE || emu_continue_now || test_mode==3);
        if(!resume && !dev_remote_game() && !test_frames && gba_continue_available(path,NULL)) {
            int answer=bb_pause_prompt();   // B (or Close): back to the shelf, nothing played, Continue untouched
            resume=answer==BB_CONTINUE_YES;went_back=answer==BB_CONTINUE_BACK;
            if(went_back){playlog_drop();log_printf("gba: back at the Continue question\n");}
        }
        bool state_ok=true;
        if(dev_remote_game() && gba_launch_slot>=0)state_ok=state_load(gba_launch_slot);
        else if(resume)state_ok=state_load(10);
        if(test_mode==3)log_printf("gba: speedtest continue load=%d\n",state_ok);
        dev_game_started(base,state_ok);
        if(audio_ready)audio_pause(false);
        if(!went_back)home_note_game(path,name,menu_title,"gba-continue");
    }
    audio_muted=false;speed_icon=0;
    if(loaded && battery_ok) {
     gba_speed_begin(input_keys_held());touch_prepare();
     statsview_context(&gba_touch_settings.fps,g_new3ds && control_side(),save_fps);
     statsview_begin(name);statsview_redraw(0);
     if(gba_speed_rewind_allowed() && !bb_rewind_active())message("Rewind unavailable: not enough memory");
     touch_intro_pending=gba_touch_intro();touch_intro_painted=touch_intro_visible=false;
     if(touch_intro_pending)message(control_side() && g_new3ds?
      "Touch controls are on. Tap MENU (top right) to pause. Change them in Settings.":
      "Touch controls are on. Tap MENU (top left) to pause. Change them in Settings.");
     touch_intro_visible=touch_intro_pending;
    }
    bottom_dirty=top_dirty=true;
    unsigned ff_extra=0,rewind_shown=0;bool rewind_hint=false,was_ff=false;
    gpsp_retro_set_run_guard(frame_guard);run_stalled=false;last_video=svcGetSystemTick();
    gba_clock clock;
    gba_clock_reset(&clock,svcGetSystemTick(),SYSCLOCK_ARM11);
#ifdef BBGBA_TIMING
    gpsp_gba_timing_config(dev_remote_game() && gba_remote_prof);
    memset(cpu_diag,0,sizeof(cpu_diag));
#endif
    dev_drawn=0;
    mono_costs=(gba_costs){0};extra_costs=(gba_costs){0};
    present_costs=(gba_costs){0};skipped_costs=(gba_costs){0};
    stereo_mono_cost=stereo_extra_cost=stereo_present_cost=0;
    stereo_review=stereo_mode=stereo_phase=stereo_debt=0;
    lock_hold=probe_count=0;probe_frame=phase_lock=false;
    u64 report=clock.next, run_start=report, emu_ticks=0, present_ticks=0, max_emu=0;
    unsigned report_frames=0, rendered=0, skipped=0, rebases=0;
    u64 frame_previous=svcGetSystemTick(),max_frame=0;
    latch_waits=0;latch_ticks=0;
    unsigned stereo_shown=0, stereo_mono=0, stereo_late=0, stereo_rejected=0, stereo_blank=0, stereo_bitmap=0;
    u64 paused_ticks=0, active_end=clock.next;
    boot_begin(loaded && battery_ok);
    if(went_back)goto stopped;
    while(loaded && battery_ok && bb_save_runnable() && aptMainLoop()) {
        if(dev_tick(true)) break;
        if(bb_save_failed()) {
            message("Could not save. Check the SD card before quitting.");
        }
        if(clock_reset) {
            u64 resumed=svcGetSystemTick();
            if(resumed>active_end) paused_ticks+=resumed-active_end;
            gba_clock_reset(&clock,resumed,SYSCLOCK_ARM11);clock_reset=false;
            report=resumed;report_frames=rendered=0;dev_drawn=0;emu_ticks=present_ticks=max_emu=0;last_video=resumed;latch_waits=0;latch_ticks=0;
        }
        input_poll();
        if(touch_intro_painted) {gba_touch_intro_shown();touch_intro_painted=false;touch_intro_pending=false;}
        unsigned touch_events;
        if(touch_intro_pending || (touch_intro_visible && notice_visible && svcGetSystemTick()<notice_until)) {
         gba_touch_reset();touch_events=0;
        } else touch_events=gba_touch_step(input_contact());
        if(touch_events&GBA_TOUCH_SPEED_EVENT)gba_speed_touch(
         touch_events&GBA_TOUCH_TOGGLE_EVENT,touch_events&GBA_TOUCH_HOLD_EVENT,touch_events&GBA_TOUCH_REWIND_EVENT);
        held=gba_speed_input(input_keys_held());
        if(touch_events&GBA_TOUCH_STATS_EVENT) {statsview_cycle();statsview_redraw(0);touch_prepare();}
        if(test_mode>=2)gba_speed_test(frames);
        if(gpsp_retro_has_solar_sensor() && bb_active_controls.profile.value[3]==0 && !(bb_controls_hotkey(&bb_active_controls,14)&KEY_Y) && !(bb_controls_hotkey(&bb_active_controls,15)&KEY_Y) && !(bb_controls_hotkey(&bb_active_controls,16)&KEY_Y) && (input_keys_down()&KEY_Y)) {
            gba_settings_set(5,1);
            char light[48];snprintf(light,sizeof(light),"Sunlight: %d / 10",gba_settings.solar);message(light);
        }
        gpsp_retro_set_solar_level(gba_settings.solar);
        if(debug_key_frames) {held|=debug_key;debug_key_frames--;}
        int remote_ui=dev_ui_pending();
        unsigned actions=bb_controls_actions(&bb_active_controls,held,input_keys_down(),&held);
        if(actions&6) {
            u64 pause_start=svcGetSystemTick();
            bb_pause_quick((actions&2)!=0);
            u64 resumed=svcGetSystemTick();
            paused_ticks+=resumed-pause_start;
            report=resumed;report_frames=rendered=0;dev_drawn=0;emu_ticks=present_ticks=max_emu=0;last_video=resumed;latch_waits=0;latch_ticks=0;
            clock_reset=false;
            gba_clock_reset(&clock,resumed,SYSCLOCK_ARM11);held=0;frame_previous=resumed;
        }
        if((actions&1) || (touch_events&GBA_TOUCH_MENU_EVENT) || remote_ui==DEV_UI_PAUSE || remote_ui==DEV_UI_SETTINGS || remote_ui==DEV_UI_STATES) {
            if(remote_ui!=DEV_UI_NONE) dev_ui_take();
            u64 pause_start=svcGetSystemTick();
            flush_fps();
            bool quit=bb_pause_run();
            bb_controls_map(&bb_active_controls,descriptor.keys,&descriptor.turbo_a_key,&descriptor.turbo_b_key);
            u64 resumed=svcGetSystemTick();
            paused_ticks+=resumed-pause_start;
            report=resumed;report_frames=rendered=0;dev_drawn=0;emu_ticks=present_ticks=max_emu=0;last_video=resumed;latch_waits=0;latch_ticks=0;
            clock_reset=false; // a HOME restore inside this pause was accounted above
            if(quit) break;
            gba_speed_pause(input_keys_held());touch_prepare();
            statsview_context(&gba_touch_settings.fps,g_new3ds && control_side(),save_fps);statsview_begin(name);statsview_redraw(0);
            gba_clock_reset(&clock,resumed,SYSCLOCK_ARM11);held=0;last_video=resumed;
        }
        if(!bb_save_runnable())break;
        u64 frame_now=svcGetSystemTick();
        if(frame_now-frame_previous>max_frame)max_frame=frame_now-frame_previous;
        frame_previous=frame_now;
        held=(held & ~(GBA_TOUCH_BITS|KEY_TOUCH)) | gba_touch_keys();
        bool ff=gba_speed_ff(), rewinding=gba_speed_rewinding();
        if(notice_visible && frame_now>=notice_until) {notice_visible=false;bottom_dirty=top_dirty=true;}
        bottom_dirty|=(touch_events&GBA_TOUCH_DIRTY_EVENT)!=0;
        if(was_ff!=ff) {
         if(was_ff)clock_reset=true; /* normal speed from now, no catch-up */
         gba_touch_lit(ff);bottom_dirty=true;
        }
        was_ff=ff;
        run_deadline=svcGetSystemTick()+10ULL*SYSCLOCK_ARM11;
        speed_icon=ff?1:0;
        if(rewinding) gba_rewind_frame(&rewind_hint,&rewind_shown);
        else rewind_hint=false;
        audio_muted=ff || speed_icon<0;
        /* Fast-forward: up to 3 more frames per shown frame, no picture, no sound */
        for(unsigned extra=ff?GBA_FF_EXTRA:0;extra;extra--) {
            skip_video=true;probe_frame=false;
            bb_retro_run_frame(&retro_host,held,true);
            gba_speed_capture();ff_extra++;
        }
        u64 before=svcGetSystemTick();
        skip_video=!ff && gba_clock_skip(&clock,before);
        bool late=skip_video || before>clock.next+SYSCLOCK_ARM11/2000;
        gba_stereo_prepare(late);
        phase_lock=lock_hold && !ui_wants_3d && !gba_settings.frame_blend && !ff;
        probe_frame=false;
        if(phase_lock && (frames&1)) {probe_frame=!skip_video && (probe_count++&7)<2;skip_video=true;}
        picture_ready=false;
        if(indexed_ready)gba_index_gpu_fence();
        bb_retro_run_frame(&retro_host,held,skip_video);
        if(speed_icon>=0)gba_speed_capture();   /* not while stepping back */
        /* NULL/duplicate callbacks retain the complete previous LCD picture. */
        skip_video |= !picture_ready;
        if(boot_hold)boot_step(paused_ticks,frames,picture_ready);
        if(run_stalled || svcGetSystemTick()-last_video>10ULL*SYSCLOCK_ARM11) {
            run_stalled=true;
            snprintf(error_text,sizeof(error_text),"The GBA game stopped producing frames. Go to your collection and try another game.");
            log_printf("gba: frame watchdog stopped game\n");break;
        }
        frames++;report_frames++;
        if(!skip_video && gba_settings.stereo) {
            if(stereo_frame) ++stereo_shown;else ++stereo_mono;
            stereo_late+=late;stereo_rejected+=!stereo_attempt;
            unsigned flags=gpsp_gba_stereo_flags();
            stereo_blank+=(flags&1)!=0;stereo_bitmap+=(flags&2)!=0;
        }
        u64 after=svcGetSystemTick(), cost=after-before;
        if(skip_video) gba_cost_add(&skipped_costs,cost);
        else if(!stereo_attempt) gba_cost_add(&mono_costs,cost);
        emu_ticks+=cost;if(cost>max_emu)max_emu=cost;
        u64 latch_wait=0;
        // Include the final clear present: the compositor clears its state inside it.
        bottom_dirty |= __atomic_load_n(&ui_resume_pending,__ATOMIC_ACQUIRE)!=0;
        if(!skip_video) {
            if(!stereo_frame) latch_wait=wait_top_latch();
            ui_begin();video_present(false,draw_game_top,bottom_dirty?draw_bottom:NULL,NULL);bottom_dirty=top_dirty=false;rendered++;
        } else {
            skipped++;
            if(bottom_dirty || top_dirty) {
                ui_begin();video_present_screens(false,top_dirty,top_dirty?draw_game_top:NULL,bottom_dirty?draw_bottom:NULL,NULL);bottom_dirty=top_dirty=false;
            }
        }
        u64 present_cost=svcGetSystemTick()-after-latch_wait;
        if(!skip_video) {
            if(!stereo_attempt) gba_cost_add(&present_costs,present_cost);
            else if(stereo_frame && mono_costs.count) {
                u64 total=cost+present_cost, base=stereo_mono_cost+stereo_present_cost;
                gba_cost_add(&extra_costs,total>base?total-base:0);
            }
        }
        /* Demote cadence, never disparity. A game stays at 30 until Layer 3D
         * is disabled/re-enabled or reloaded; scene/cost noise cannot pump it. */
        if(stereo_mode==60 && !stereo_force && !ff && after+present_cost>clock.next+clock.whole+SYSCLOCK_ARM11/2000) {
            if(++stereo_debt>=6) {stereo_mode=30;stereo_debt=0;}
        } else stereo_debt=0;
        present_ticks+=present_cost;
        if(!(frames%180)) {u64 save_start=svcGetSystemTick();queue_battery();log_printf("gba: battery_ms=%.3f frames=%u\n",(svcGetSystemTick()-save_start)*1000.0/SYSCLOCK_ARM11,frames);}
        u64 now=svcGetSystemTick();
        if(now-report>=SYSCLOCK_ARM11) {
#ifdef BBGBA_TIMING
            if(gba_remote_prof) {
                u64 ticks[6];unsigned calls[6];gpsp_gba_timing_read(ticks,calls);
                double scale=1000.0/(SYSCLOCK_ARM11*(double)report_frames);
                log_printf("gba-cpu: frames=%u n=%u common=%.4f obj=%.4f blend=%.4f bg=%.4f order=%.4f begin=%.4f wait=%.4f upload=%.4f calls=%u/%u/%u/%u/%u\n",frames,report_frames,ticks[0]*scale,ticks[1]*scale,ticks[2]*scale,ticks[3]*scale,ticks[4]*scale,cpu_diag[0]*scale,cpu_diag[1]*scale,cpu_diag[2]*scale,calls[0],calls[1],calls[2],calls[3],calls[4]);
                memset(cpu_diag,0,sizeof(cpu_diag));
            }
#endif
            log_printf("gba: max_frame_ms=%.3f frames=%u\n",max_frame*1000.0/SYSCLOCK_ARM11,frames);max_frame=0;
            if(gba_remote_journal) {
                u64 jt[3];unsigned jc[10];gpsp_gba_journal_read(jt,jc);
                double scale=1000.0/(SYSCLOCK_ARM11*(double)report_frames);
                log_printf("gba-journal: frames=%u n=%u shown=%u consume=%.4f refs=%.4f convert=%.4f tiles=%u pal=%u oam=%u regs=%u decodes=%u refbuild=%u raster=%u slots=%u mode=%d\n",frames,report_frames,jc[0],jt[0]*scale,jt[1]*scale,jt[2]*scale,jc[1],jc[2],jc[3],jc[4],jc[5],jc[6],jc[7],jc[8],gba_remote_journal);
            }
            char sound[256];audio_stats(sound,sizeof(sound));
            statsview_sample sample={.fps=report_frames*(double)SYSCLOCK_ARM11/(now-report),.target=GBA_FPS,
             .frames=report_frames+ff_extra,.shown=rendered,.elapsed=(now-report)*1000/SYSCLOCK_ARM11,
             .emu=report_frames?emu_ticks*1000.0/(SYSCLOCK_ARM11*(double)report_frames):0,
             .present=report_frames?present_ticks*1000.0/(SYSCLOCK_ARM11*(double)report_frames):0,
             .max=max_emu*1000.0/SYSCLOCK_ARM11,.skipped=skipped,.audio=sound,.unavailable_timing=true,
             .detail={"Frameskip: n/a","GBA CPU renderer",NULL,NULL,NULL}};
            statsview_update(&sample);bottom_dirty|=statsview_redraw(0);
            if(dev_remote)log_printf("play-controls: system=gba bottom_draws=%u frames=%u\n",bottom_paints,frames);
            unsigned long long profile[20];gpsp_retro_profile_read(profile);
            log_printf("gba: core totals vram_bridge=%llu ram_flush=%llu rom_page=%llu\n",profile[16],profile[17],profile[18]);
            log_printf("gba: frames=%u fps=%.2f speed=%.2f%% shown=%u skip=%u emu_ms=%.3f present_ms=%.3f max_emu_ms=%.3f audio_frames=%llu nonzero=%llu hash=%08lx; %s\n",
                frames,report_frames*(double)SYSCLOCK_ARM11/(now-report),
                report_frames*(double)SYSCLOCK_ARM11*100/(GBA_FPS*(now-report)),rendered,skipped,
                emu_ticks*1000.0/(SYSCLOCK_ARM11*(double)report_frames),
                present_ticks*1000.0/(SYSCLOCK_ARM11*(double)report_frames),max_emu*1000.0/SYSCLOCK_ARM11,
                (unsigned long long)audio_frames,(unsigned long long)audio_nonzero,(unsigned long)audio_hash,sound);
            if(gba_settings.stereo) log_printf("gba: layer3d stereo=%u mono=%u pictures=%u mode=%s mono_median_ms=%.3f extra_ms=%.3f extra_samples=%u force=%d review=%u late=%u rejected=%u blank=%u bitmap=%u\n",
                stereo_shown,stereo_mono,rendered,stereo_mode==60?"60":stereo_mode==30?"30":"mono",
                gba_cost_median(&mono_costs)*1000.0/SYSCLOCK_ARM11,
                gba_cost_median(&extra_costs)*1000.0/SYSCLOCK_ARM11,extra_costs.count,stereo_force,stereo_review,stereo_late,stereo_rejected,stereo_blank,stereo_bitmap);
            if(dev_remote) log_printf("gba: presentation uploaded=%u presented=%u held=%u null=%u sample_blank=%u sample_repeat=%u stereo=%u mono=%u latch_pending=%u latch_ms=%.3f latch=%d phase_lock=%d\n",
                dev_drawn,rendered,report_frames-rendered,picture_null,picture_blank,picture_repeat,stereo_shown,rendered-stereo_shown,
                latch_waits,latch_ticks*1000.0/SYSCLOCK_ARM11,gba_remote_latch,phase_lock);
            if(ff_extra || rewind_shown) log_printf("gba: fast-forward extra_frames=%u rewind_frames=%u rewind_kib=%u\n",ff_extra,rewind_shown,gba_speed_history_kib());
            ff_extra=rewind_shown=0;
            latch_waits=0;latch_ticks=0;
            picture_null=picture_blank=picture_repeat=0;
            stereo_shown=stereo_mono=stereo_late=stereo_rejected=stereo_blank=stereo_bitmap=0;
            if(dev_enabled())dev_system_sample("gba",(now-report)/(double)SYSCLOCK_ARM11,report_frames,dev_drawn,rendered,
                emu_ticks*1000.0/(SYSCLOCK_ARM11*(double)report_frames),
                present_ticks*1000.0/(SYSCLOCK_ARM11*(double)report_frames),sound,
                GBA_FPS,ui_wants_3d?"layer3d=1 display3d=1":gba_settings.stereo?"layer3d=1 display3d=0":"layer3d=0 display3d=0");
            emu_ticks=present_ticks=max_emu=0;rendered=0;dev_drawn=0;
            report=now;report_frames=0;
        }
        if(test_mode==1 && frames==120) {state_save(9);}
        if(test_mode==1 && frames==180) {state_load(9);audio_pause(false);}
        if(test_frames && frames>=test_frames) break;
        now=svcGetSystemTick();
        if(gba_clock_advance(&clock,now)) rebases++;
        if(ff && clock.next<now) {clock.next=now;clock.fraction=0;}   /* fast-forward: never owe frames */
        if(clock.next>now) svcSleepThread((s64)((clock.next-now)*1000000000ULL/SYSCLOCK_ARM11));
        active_end=svcGetSystemTick();
    }
stopped:
    if(boot_hold)boot_end();   // left within the first pictures: no held dim into the next screen
    u64 wall_ticks=svcGetSystemTick()-run_start;
    u64 run_ticks=wall_ticks>paused_ticks?wall_ticks-paused_ticks:1;
    flush_fps();gba_speed_end();gba_touch_reset();statsview_context(NULL,false,NULL);audio_muted=false;speed_icon=0;   /* frees the history before the saves need memory */
    if(indexed_ready)gba_index_gpu_exit();indexed_ready=indexed_picture=false;
    gpsp_gba_journal_config(0,NULL);
    if(journal_atlas){linearFree(journal_atlas);journal_atlas=NULL;}
    gpsp_retro_set_run_guard(NULL);
    aptUnhook(&cookie);
    bool execution_ok=!loaded || !battery_ok || bb_save_runnable();
    if(!execution_ok)snprintf(error_text,sizeof(error_text),"Postcard load failed. Restart the game to continue.");
    log_printf("gba: phase=teardown begin stalled=%d transition=%d\n",run_stalled,dev_transition());
    if(loaded && battery_ok)queue_battery();   /* to the writer thread; not waited for (bb_save_end) */
    /* the Continue state: made now, its files written while the shelf comes back */
    if(loaded && battery_ok && !run_stalled && bb_save_runnable())bb_save_state_end(10,true);
    log_printf("gba: phase=teardown continue-done\n");
    if(loaded && battery_ok) save_battery();
    bool final_ok=bb_save_end(false);
    if(!final_ok && execution_ok)snprintf(error_text,sizeof(error_text),"The latest save could not be written. Check the SD card and retry in your collection.");
    if(picture_memory){linearFree(picture_memory);picture_memory=NULL;}
    history_session_begin(false,NULL);
    if(game_active) playlog_end();
    if(audio_ready) audio_exit();
    audio_ready=false;
    /* Linear memory goes BEFORE the cartridge cache: freeing a linear block
     * makes libctru allocate a free-list node on the heap. With the cache
     * already freed, those nodes landed inside it and split the heap the next
     * system needs (a PS1 state is one 4.4 MB block). */
    gpsp_gba_stereo_config(NULL,0);
    log_printf("gba: phase=gpu-release begin\n");
    if(gspHasGpuRight()) {video_gpu_idle();C3D_TexDelete(&texture);}
    log_printf("gba: phase=gpu-release done\n");
    if(right_ready && gspHasGpuRight()) C3D_TexDelete(&right_texture);
    free(right_pixels);right_pixels=NULL;right_ready=stereo_frame=false;ui_wants_3d=false;
    texture_ready=false;
    log_printf("gba: phase=core-unload begin\n");
    if(loaded){bb_gba_cheats_close();gpsp_retro_unload_game();}
    gpsp_retro_deinit();
    bb_retro_unbind(&retro_host);
    log_printf("gba: phase=core-unload done\n");
    game_active=false;gba_settings_solar_game=false;bb_session_set_stereo(false);
    memory_log("released");
    memory_log("after");
    dev_game_ended(loaded && battery_ok && final_ok);
    log_printf("gba: stopped frames=%u audio_frames=%llu nonzero=%llu hash=%08lx\n",frames,(unsigned long long)audio_frames,(unsigned long long)audio_nonzero,(unsigned long)audio_hash);
    log_printf("gba: elapsed_ms=%.3f speed=%.3f%% skipped=%u clock_rebases=%u paused_ms=%.3f wall_ms=%.3f\n",run_ticks*1000.0/SYSCLOCK_ARM11,frames*(double)SYSCLOCK_ARM11*100/(GBA_FPS*run_ticks),skipped,rebases,paused_ticks*1000.0/SYSCLOCK_ARM11,wall_ticks*1000.0/SYSCLOCK_ARM11);
    return loaded && battery_ok && final_ok && !run_stalled ? 0:-1;
prepare_failed:
    bb_retro_unbind(&retro_host);
    video_gpu_idle();C3D_TexDelete(&texture);texture_ready=false;
    
    return start_failed();
}
bool gba_continue_available(const char *path,unsigned *saved)
{
    char id[GBA_ID_LEN],file[PATH_LEN];
    if(!gba_saveid_path(path,id))return false;
    snprintf(file,sizeof(file),GBA_STATES "/%s.state10",id);
    struct stat legacy;
    if(stat(file,&legacy)) {
        /* Read-only availability before first launch/adoption. Validate the
         * legacy state's identity so another same-name game cannot offer it. */
        const char *base=strrchr(path,'/');base=base?base+1:path;
        char name[256];snprintf(name,sizeof(name),"%s",base);
        char *dot=strrchr(name,'.');if(dot)*dot=0;
        char owner[PATH_LEN],claimed[GBA_ID_LEN]={0};
        snprintf(owner,sizeof(owner),GBA_SAVES "/%s.id",name);
        FILE *claim=fopen(owner,"rb");
        if(claim) {
            size_t n=fread(claimed,1,sizeof(claimed)-1,claim);bool valid=!ferror(claim);fclose(claim);
            if(n && claimed[n-1]=='\n')claimed[n-1]=0;
            if(!valid || strcmp(claimed,id))return false;
        } else if(errno!=ENOENT)return false;
        snprintf(file,sizeof(file),GBA_STATES "/%s.state10",name);
        unsigned char header[192];struct {char magic[8];u32 version,bytes,rom,rom_size,crc;} h;
        FILE *f=fopen(file,"rb");
        bool ok=f && fread(&h,1,sizeof(h),f)==sizeof(h);
        if(f)fclose(f);
        if(!ok || !gba_cart_header(path,header) || memcmp(h.magic,"BBGBAST1",8) ||
           h.rom!=crc32(0,header,sizeof(header)))return false;
    }
    struct stat st;if(stat(file,&st))return false;
    if(saved)*saved=gba_state_time(file);
    return true;
}
/* RAM-only save service adapter. Failed restores also keep authoritative RAM. */
size_t gba_session_state_size(void) {return core_held()?gpsp_retro_serialize_size():0;}
bool gba_session_state_save(void *buf,size_t cap,size_t *len)
{return bb_retro_state_save(&retro_host,buf,cap,len);}
bb_state_result gba_session_state_load(const void *buf,size_t len,bool keep)
{bb_state_result result=bb_retro_state_load(&retro_host,buf,len,keep);if(result==BB_STATE_PARTLY)bb_save_invalidate();return result;}
bool gba_session_battery(void **data,size_t *size)
{return bb_retro_battery(&retro_host,data,size);}
size_t gba_session_rtc_size(void){u32 rtc[3];return gpsp_retro_get_rtc(rtc)?sizeof(rtc):0;}
bool gba_session_rtc_get(void *p,size_t n){return n==12 && gpsp_retro_get_rtc(p);}
bool gba_session_rtc_set(const void *p,size_t n){if(n!=12)return false;gpsp_retro_set_rtc(p);return true;}
uint32_t gba_session_generation(void) {return gpsp_gba_battery_generation();}
bool gba_switch(const char *arg)
{
    if(!strncmp(arg,"gba=",4)) snprintf(auto_path,sizeof(auto_path),"%s",arg+4);
    else if(!strncmp(arg,"gba_frames=",11)) test_frames=strtoul(arg+11,NULL,10);
    else if(!strncmp(arg,"gba_test=",9)) test_mode=strtoul(arg+9,NULL,10);
    else return false;
    return true;
}
const char *gba_autostart(void)
{
 return auto_path[0]?auto_path:NULL;
}
static u64 wait_top_latch(void)
{
    if(!gspIsPresentPending(GSP_SCREEN_TOP)) return 0;
    u64 start=svcGetSystemTick();latch_waits++;
    if(!gba_remote_latch) return 0;
    while(gspIsPresentPending(GSP_SCREEN_TOP) && svcGetSystemTick()-start<SYSCLOCK_ARM11/50) svcSleepThread(100000);
    u64 waited=svcGetSystemTick()-start;latch_ticks+=waited;
    return waited;
}
/* 20 sampled rows of the last two rendered frames, each hashed as two
 * halves: a half equal two frames back but not one frame back alternates.
 * Halves, not whole rows, because a ticking clock or score shares its row
 * with steadily alternating text (Classic NES Excitebike's race: the status
 * row alternates every frame left of the timer, and everything above
 * scrolls). Quarters were tried: Mario Kart and Sonic Advance then locked
 * falsely. Only consecutive rendered frames count (hashes further apart,
 * under frameskip or while locked, make slow animation and palette cycles
 * look alternating). Unlocked, a half must alternate in four consecutive
 * checks (ABABAB) to lock: texture under motion matches by chance now and
 * then (Kirby's animated game-select menu does so three times in a row),
 * but not four times. Locked, each two-frame probe gives three checks, and
 * three in a row keep the lock. Twenty complete runs of three
 * checks without it (about 5 s locked) unlock. */
static void gba_track_alternation(const void *data, size_t pitch)
{
    /* Same static sizes as the whole-row tracker: a word holds both halves'
     * 16-bit hashes and a byte both halves' streaks (4 bits, capped at 15).
     * Larger statics would move gba.c's other data and, through the section
     * anchors, change code in the pinned frame functions. */
    static u32 last[20], before[20];
    static u8 streak[20];
    static unsigned last_frame, before_frame, checks;
    bool consecutive=last_frame==frames-1 && before_frame==frames-2, evidence=false;
    for(unsigned i=0;i<20;i++) {
        const u32 *row=(const u32 *)((const u8 *)data+(4+i*8)*pitch);
        u32 hashes=0;unsigned streaks=0;
        for(unsigned half=0;half<2;half++) {
            u32 hash=0;
            for(unsigned x=0;x<60;x++) hash=(hash*31)^row[half*60+x];
            unsigned shift=half*16, h=(hash^hash>>16)&0xffff, run=streak[i]>>(half*4)&15;
            run=consecutive && h==(before[i]>>shift&0xffff) && h!=(last[i]>>shift&0xffff) ? (run<15?run+1:15) : 0;
            evidence|=run>=(lock_hold?3u:4u);
            hashes|=h<<shift;streaks|=run<<(half*4);
        }
        before[i]=last[i];last[i]=hashes;streak[i]=(u8)streaks;
    }
    before_frame=last_frame;last_frame=frames;
    checks=consecutive?checks+1:0;
    if(evidence) lock_hold=20;
    else if(checks>=3 && lock_hold) lock_hold--;
}

#include "components/gba.inc"

/* ---- the start's held dim (see BOOT_HOLD_FRAMES) ---- */
static bool boot_blank;
__attribute__((noinline)) static void boot_begin(bool on)
{
    boot_hold=on;boot_blank=true;
    if(on)ui_swap_hold();
}
/* sparse look at the picture just uploaded: all black or all white is the game still booting */
__attribute__((noinline)) static void boot_sample(const void *data,size_t pitch)
{
    bool black=true,white=true;
    for(unsigned y=0;y<160;y+=20) for(unsigned x=0;x<240;x+=30) {
        u16 pixel=*(const u16 *)((const u8 *)data+y*pitch+x*2);
        black &= pixel==0;white &= pixel==0xffff;
    }
    boot_blank=black || white;
}
__attribute__((noinline)) static void boot_step(u64 paused_ticks,unsigned frame,bool picture)
{
    if(paused_ticks){boot_hold=false;return;}   /* a pause cleared the held dim itself */
    if((picture && !boot_blank) || frame>=BOOT_HOLD_FRAMES){boot_hold=false;ui_swap_release();}
}
__attribute__((noinline)) static void boot_end(void)
{
    boot_hold=false;ui_swap_enter();
}
__attribute__((noinline)) static void set_menu_title(const char *name)
{
    ui_clean_title(name,menu_title,sizeof(menu_title));
}
