// The few PS1 calls the shell makes: stats and audio time for the dev tools
// (emu.c), the Continue flag a game start reads, the Continue auto-save home
// shows (savestates.c) and the stereo 3D hooks the presentation calls
// (gpustereo.c). Everything else stays in systems/ps1/.
#pragma once
#include <3ds.h>
#include <stdbool.h>

// Last completed stats interval, main-thread access for remote stats.
// All costs are ms per emulated vblank. Audio is nested in run; present
// includes required offscreen submission on held frames. Misc includes the
// previous interval's reporting/SD writes. Pacing is excluded from work.
typedef struct {
	unsigned emulated, elapsed_ms, presented, held, top, bottom;
	float run_ms, present_ms, audio_ms, misc_ms, pace_ms;
	float spu_ms, spu_wait_ms, pica_ms;
	float phase_ms[8], draw_fraction;
	double picture_mean, picture_square;
	unsigned picture_intervals;
	unsigned skip_level, drawn, skipped;
} emu_frontend_sample;
const emu_frontend_sample *emu_frontend_stats(void);

u64 emu_audio_ticks(void);
bool emu_record_start(void);   // dev remote: start recording the running game

extern bool emu_continue_now;   // next game: load its Continue auto-save, no prompt

// savestates.c: the Continue auto-save's time and picture (home shows it)
bool states_auto_read(const char *prefix, u32 *saved, u16 *px, bool *picture, bool (*cancel)(void *), void *arg);

// gpustereo.c (3DS frontend)
void gpustereo_after_present(void);   // GPU thread, after C3D_FrameEnd
void gpustereo_game_end(void);        // GPU idle, before pica_exit
// presentation: the right eye's texture for the canonical/held picture, or NULL
void *gpustereo_right_for(void *picture_tex);
void gpustereo_frame_end(void);       // before a command list is submitted
extern volatile bool gpustereo_copy_bypass;   // thumbnails: plain copies out of VRAM
int gpustereo_mode(void);             // 0 off, 1 screen depth, 2 real 3D (0 on Old 3DS)
float gpustereo_flat(void);           // real 3D: share of screen depth (1 until the right eye is ready)
void gpustereo_status(char *out, int n);
