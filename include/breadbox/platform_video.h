// Video output, presentation and GPU command lists (video.c, gpu_cache.c, renderqueue.c)
#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stddef.h>

// video.c
bool video_init(void);
#define VIDEO_CMDBUF_BYTES 0x100000   // GPU command list bytes reserved by C3D_Init (c3d_abi.h checks it)
#define VIDEO_CMD_ROOM 4096          // command words free before any draw (cmdroom.c)
extern bool c3d_abi_failed;   // renderqueue.c, set when the citro3d private ABI does not match
bool video_game_begin(void);
void video_game_end(void);
bool renderer_is_gpu(void);   // GPU game buffers active; false in the library
void video_exit(void);
void *video_framebuffer(unsigned width, unsigned height, size_t *pitch);
void video_frame(const void *data, unsigned width, unsigned height, size_t pitch);
void video_gpu_frame_begin(void);

void video_hold(void);
bool video_display_changed(void);
void video_present_screens(bool show_game, bool repaint_top, void (*draw_top)(void *),
	void (*draw_bottom)(void *), void *ud);
void video_present(bool show_game, void (*draw_top)(void *),
	void (*draw_bottom)(void *), void *ud);
void video_present_vsync(bool show_game, void (*draw_top)(void *),
	void (*draw_bottom)(void *), void *ud);
C3D_RenderTarget *video_bottom_target(void);
extern void (*video_pre_draw)(void);
extern int ui_eye;
extern float ui_parallax;
extern bool ui_wants_3d;
extern bool ui_wants_plain;
// x shift for a UI layer at depth d (>0 behind the screen, <0 in front)
static inline float ui_depth(float d) { return ui_eye * d * ui_parallax; }
u64 video_take_wait_ticks(void);
u64 video_take_xfer_ticks(void);
void video_take_gpu_stats(float *gpu, u64 *gap);
u64 video_take_heap_flush_ticks(void);
void video_cache_targeted(bool enabled);
u8 video_cache_command_list(const void *commands, u32 bytes, u8 flags);
u8 video_cache_frame_end(u8 flags);

// Private extension implemented by our citro3d render queue.
#define VIDEO_FRAME_OVERLAP BIT(7)
void video_gpu_wait_previous(void);
bool video_gpu_has_previous(void);
u64 video_take_pending_wait_ticks(void);

int video_probe_top(void);

bool video_snapshot(u16 *out, int w, int h);

void video_gpu_idle(void);
bool video_gpu_poll_idle(void); /* UI thread, between frames, no active core */
void video_top_rebind(void);   // back to the top target of the eye being drawn
void video_room_plain(bool on); // the room menu: top screen at 1x on both models (with ui_wants_plain)
extern bool ui_room_eyes;       // this menu frame is the room's own: 3D also on the Old 3DS (round 16)

bool video_room_3d(void);       // ui_wants_3d for the room's loops
int video_ui_steps(void);       // vblanks of the last UI frame (2: the Old's room locked at 30 fps)
bool video_top_plain(void);     // this frame's top screen is at 1x
