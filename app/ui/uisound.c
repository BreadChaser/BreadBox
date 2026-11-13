// Interface sounds and the library music (see tools/make_sounds.py).
// NDSP channel 0 belongs to the game; the UI uses 1 (music) and 2-4 (effects: a long page turn or toss
// is not cut by two quick moves after it).
// Without DSP firmware the same sounds go through CSND (csnd_out.c): the
// game takes the first two CSND channels, music the next two (left/right),
// effects the two after that.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_policy.h"
#include <stddef.h>
#include "app_log.h"
#include "app_config.h"
#include "dev_switches.h"
#include "csnd_out.h"
#include "uisound.h"
#include "app_main.h"
#include "bb_save.h"

#define CH_MUSIC 1
#define CH_SFX0  2
#define SFX_CHANNELS 3   // NDSP; CSND has two (channels 4 and 5 after the game and the music)
#define SFX_RATE 32000.0f

typedef struct { s16 *data; u32 frames; } clip_t;

// The sound files (romfs:/sfx/<name>.pcm, mono PCM16 at SFX_RATE) and which one each event plays. The paper set
// (CC0 recordings, CREDITS) the owner picked on 2026-11-11; swap a pick by changing its file here.
static const char *const sfx_files[] = { "move_1", "move_2", "move_3", "value_1", "value_2", "value_3", "select", "back",
	"launch", "tab", "toggle", "dialog", "confirm", "deny", "save", "toss", "load", "pause_open", "pause_close", "jump", "error" };
#define SFX_FILES (int)(sizeof(sfx_files) / sizeof(sfx_files[0]))
static const char *const sfx_names[SFX_COUNT] = { "move", "select", "back", "launch", "value", "toggle", "tab", "jump",
	"dialog", "confirm", "deny", "save", "toss", "load", "pause_open", "pause_close", "error" };
typedef struct { u8 file, variants; } sfx_event_t;   // variants > 1: that many files from `file` on, taken in turn
static const sfx_event_t sfx_events[SFX_COUNT] = {
	[SFX_MOVE] = { 0, 3 }, [SFX_VALUE] = { 3, 3 },   // a card flick, a pen nib: three takes each, so ten a second never sound mechanical
	[SFX_SELECT] = { 6, 1 }, [SFX_BACK] = { 7, 1 }, [SFX_LAUNCH] = { 8, 1 }, [SFX_TAB] = { 9, 1 }, [SFX_TOGGLE] = { 10, 1 },
	[SFX_DIALOG] = { 11, 1 }, [SFX_CONFIRM] = { 12, 1 }, [SFX_DENY] = { 13, 1 }, [SFX_SAVE] = { 14, 1 }, [SFX_TOSS] = { 15, 1 },
	[SFX_LOAD] = { 16, 1 }, [SFX_PAUSE_OPEN] = { 17, 1 }, [SFX_PAUSE_CLOSE] = { 18, 1 }, [SFX_JUMP] = { 19, 1 }, [SFX_ERROR] = { 20, 1 },
};

static clip_t clips[SFX_FILES], music;
static s16 *music_l, *music_r;     // CSND: the music split into two mono channels
static ndspWaveBuf sfx_wb[SFX_CHANNELS], music_wb;
static int next_sfx;
static bool ok, csnd, music_on;
static float music_vol, music_target;
// After a game the music (3 MB) is read back by the later thread (bb_save.h) and taken up
// by music_reload_step, not in one 0.3 s read between the game and the menu.
static struct { bb_later_file job; s16 *buf; bool play; } back;
static void music_reload_later(bool start);
static void reload_stop(void)
{
	if (back.buf) {
		bb_later_cancel(&back.job);
		linearFree(back.buf);
	}
	memset(&back, 0, sizeof(back));
}

static bool load(const char *path, clip_t *c, int channels)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	c->data = linearAlloc(size);
	if (!c->data || fread(c->data, 1, size, f) != (size_t)size) {
		fclose(f);
		return false;
	}
	fclose(f);
	c->frames = size / (2 * channels);
	if (csnd)
		GSPGPU_FlushDataCache(c->data, size);
	else
		DSP_FlushDataCache(c->data, size);
	return true;
}

static void setup_channel(int ch, bool stereo, float vol)
{
	ndspChnReset(ch);
	ndspChnSetInterp(ch, NDSP_INTERP_POLYPHASE);
	ndspChnSetRate(ch, SFX_RATE);
	ndspChnSetFormat(ch, stereo ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
	float mix[12] = { vol, vol };
	ndspChnSetMix(ch, mix);
}

// CSND plays mono only: split the interleaved music once
static void split_music(void)
{
	music_l = linearAlloc(music.frames * 2);
	music_r = linearAlloc(music.frames * 2);
	if (!music_l || !music_r) {
		if (music_l) linearFree(music_l);
		if (music_r) linearFree(music_r);
		music_l = music_r = NULL;
		return;
	}
	for (u32 i = 0; i < music.frames; i++) {
		music_l[i] = music.data[i * 2];
		music_r[i] = music.data[i * 2 + 1];
	}
	GSPGPU_FlushDataCache(music_l, music.frames * 2);
	GSPGPU_FlushDataCache(music_r, music.frames * 2);
}

void uisound_init(void)
{
	csnd = false;
	if (sw_csnd || R_FAILED(ndspInit())) {
		if (!csnd_acquire()) {
			log_printf("sound: no NDSP and no CSND, UI is silent\n");
			return;
		}
		csnd = true;
	}
	log_printf("sound: UI uses %s\n", csnd ? "CSND (no DSP firmware)" : "NDSP");
	u32 bytes = 0;
	for (int i = 0; i < SFX_FILES; i++) {
		char path[64];
		snprintf(path, sizeof(path), "romfs:/sfx/%s.pcm", sfx_files[i]);
		if (load(path, &clips[i], 1))
			bytes += clips[i].frames * 2;
	}
	log_printf("sound: %d effect files, %u KB\n", SFX_FILES, (unsigned)(bytes / 1024));
	ok = true;
	music_reload_later(false);   // cold start: the music's memory now (the heap's order stays), its 3 MB read after the first frame (music_stream)
}

// After a self-update's commit (main.c): the music is read back on the later thread.
// Wait for that thread a bounded time and drop a read it has not started; nothing is
// read here. uisound_exit then stops the hardware without touching ROMFS.
bool uisound_stop_reads(u64 timeout)
{
	if (!bb_save_settle_timeout(timeout))
		return false;
	reload_stop();
	return true;
}

void uisound_exit(void)
{
	if (!ok)
		return;
	reload_stop();
	if (csnd) {
		for (int i = 2; i < 6; i++)
			if (csnd_channel(i) >= 0)
				CSND_SetPlayState(csnd_channel(i), 0);
		csndExecCmds(true);
	} else {
		for (int ch = CH_MUSIC; ch < CH_SFX0 + SFX_CHANNELS; ch++)
			ndspChnWaveBufClear(ch);
	}
	for (int i = 0; i < SFX_FILES; i++)
		if (clips[i].data)
			linearFree(clips[i].data);
	if (music.data)
		linearFree(music.data);
	if (music_l) linearFree(music_l);
	if (music_r) linearFree(music_r);
	music_l = music_r = NULL;
	if (csnd)
		csnd_release();
	else
		ndspExit();
	ok = false;
}

static u8 sfx_turn[SFX_COUNT];   // the next variant of each event
static void play_file(int file)
{
	const clip_t *c = &clips[file];
	if (!c->data)
		return;
	if (csnd) {
		int chn = csnd_channel(4 + next_sfx % 2);
		next_sfx = (next_sfx + 1) % SFX_CHANNELS;
		if (chn < 0)
			return;
		csnd_setup(chn, false, (u32)SFX_RATE, 0.8f, 0.0f, c->data, c->frames * 2);
		csndExecCmds(false);
		return;
	}
	int ch = CH_SFX0 + next_sfx;
	next_sfx = (next_sfx + 1) % SFX_CHANNELS;
	setup_channel(ch, false, 0.8f);
	ndspWaveBuf *wb = &sfx_wb[ch - CH_SFX0];
	memset(wb, 0, sizeof(*wb));
	wb->data_pcm16 = c->data;
	wb->nsamples = c->frames;
	ndspChnWaveBufAdd(ch, wb);
}
// Plays now: the shared moves, and every flush. Silent with Menu sounds off and once the system closes the app.
void sfx_now(int event)
{
	if (!ok || !g_cfg.sounds || event < 0 || event >= SFX_COUNT || aptShouldClose())
		return;
	const sfx_event_t *e = &sfx_events[event];
	int file = e->file + (e->variants > 1 ? sfx_turn[event]++ % e->variants : 0);
	if (sw_sfxlog)
		log_printf("sfx: %s (%s)\n", sfx_names[event], sfx_files[file]);
	play_file(file);
}

// The frame's one sound (see uisound.h). Explicit sounds replace the gate's default; the latest explicit wins.
static int pending = -1;
void sfx_play(int event)
{
	if (event == SFX_LAUNCH) {   // the game takes over right after: never deferred
		pending = -1;
		sfx_now(event);
		return;
	}
	pending = event;
}
void sfx_default(int event)
{
	if (pending < 0)
		pending = event;
}
void sfx_cancel(void) { pending = -1; }
static bool card_closes;
void sfx_card_closes(void) { card_closes = true; }
bool sfx_close_said(void)
{
	bool said = card_closes;
	card_closes = false;
	return said;
}
void sfx_flush(void)
{
	int event = pending;
	pending = -1;
	if (event >= 0)
		sfx_now(event);
}

// start (fading in) or stop the library music
void music_play(bool on)
{
	if (back.buf)
		back.play = on;   // still coming back: starts when it is all there
	if (!ok || !music.data || (csnd && !music_l))
		return;
	on = on && g_cfg.music;
	if (on && !music_on) {
		if (csnd) {
			int l = csnd_channel(2), r = csnd_channel(3);
			if (l < 0 || r < 0)
				return;
			csnd_setup(l, true, (u32)SFX_RATE, 0.0f, -1.0f, music_l, music.frames * 2);
			csnd_setup(r, true, (u32)SFX_RATE, 0.0f, 1.0f, music_r, music.frames * 2);
			csndExecCmds(true);
		} else {
			setup_channel(CH_MUSIC, true, 0.0f);
			memset(&music_wb, 0, sizeof(music_wb));
			music_wb.data_pcm16 = music.data;
			music_wb.nsamples = music.frames;
			music_wb.looping = true;
			ndspChnWaveBufAdd(CH_MUSIC, &music_wb);
		}
		music_vol = 0.0f;
		music_target = 0.45f;
		boot_mark("music playing");
	} else if (!on && music_on) {
		if (csnd) {
			CSND_SetPlayState(csnd_channel(2), 0);
			CSND_SetPlayState(csnd_channel(3), 0);
			csndExecCmds(true);
		} else {
			ndspChnWaveBufClear(CH_MUSIC);
		}
	}
	music_on = on;
}

// The music is several MB of GPU-visible memory: games get it back
// (the hardware renderer needs it on an Old 3DS) and the library reloads it.
void music_unload(void)
{
	reload_stop();   // a game before it was all back
	if (!ok || !music.data)
		return;
	music_play(false);
	svcSleepThread(30000000LL);   // let the DSP stop reading it
	linearFree(music.data);
	music.data = NULL;
	if (music_l) linearFree(music_l);
	if (music_r) linearFree(music_r);
	music_l = music_r = NULL;
}

static void flush_music(bb_later_file *r)   // on the reading thread: 3 MB took one 20 ms frame here
{
	if (csnd)
		GSPGPU_FlushDataCache(r->buf, r->size);
	else
		DSP_FlushDataCache(r->buf, r->size);
}
static void music_reload_later(bool start)
{
	if (!ok || music.data || back.buf)
		return;
	FILE *f = fopen("romfs:/sfx/music.pcm", "rb");
	if (!f)
		return;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fclose(f);
	back.buf = size > 0 ? linearAlloc(size) : NULL;
	if (!back.buf)
		return;
	back.job = (bb_later_file){ "romfs:/sfx/music.pcm", back.buf, (size_t)size, 0, flush_music };
	if (start)
		bb_later_read(&back.job);
}
// The cold start's music: its memory was made in uisound_init, the later thread reads it from here
// (called once the first room frame is on the screen, so the SD card is the room's until then).
void music_stream(void)
{
	if (back.buf && bb_later_state(&back.job) == BB_LATER_IDLE)
		bb_later_read(&back.job);
}
void music_reload_step(void)
{
	int st = back.buf ? bb_later_state(&back.job) : BB_LATER_IDLE;
	if (st == BB_LATER_FAILED)
		reload_stop();
	if (st != BB_LATER_DONE)
		return;
	music.data = back.buf;
	music.frames = back.job.size / 4;
	bool play = back.play;
	memset(&back, 0, sizeof(back));
	if (csnd)
		split_music();
	if (play)
		music_play(true);
}
void music_reload(void)
{
	music_reload_later(true);
}

// call once per UI frame: eases the music volume
void music_update(void)
{
	music_reload_step();
	if (!ok || !music_on)
		return;
	float before = music_vol;
	music_vol += (music_target - music_vol) * 0.03f;
	if (csnd) {
		// only send volume changes that are audible: each is a CSND command
		if ((int)(before * 200) != (int)(music_vol * 200)) {
			csnd_set_volume(csnd_channel(2), music_vol, -1.0f);
			csnd_set_volume(csnd_channel(3), music_vol, 1.0f);
			csndExecCmds(false);
		}
		return;
	}
	float mix[12] = { music_vol, music_vol };
	ndspChnSetMix(CH_MUSIC, mix);
}
