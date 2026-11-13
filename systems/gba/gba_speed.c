/* GBA fast-forward and rewind, the PS1 way for the player: the app's hotkey
 * pairs (SELECT+R toggles fast-forward, SELECT+L held rewinds), fast-forward
 * runs up to 3 more frames per shown frame without sound, rewind keeps
 * recent moments on the New 3DS (per game, on unless turned off).
 * Rewind never rolls back the cartridge's battery save (it is not part of
 * gpSP's state, like PS1 memory cards) or the RTC clock. */
#include <stddef.h>
#include "app_log.h"
#include "app_input.h"
#include "app_main.h"
#include "gba.h"
#include "gba_settings.h"
#include "bb_system.h"
#include "retro_rewind.h"
#include "gba_speed.h"

/* New 3DS GBA keeps ~25-30 MB free after the 32 MB cartridge cache; PS1 uses
 * 24 MB. A history this size holds about a minute in action games. */
#define HISTORY_MAX (16u * 1024 * 1024)
#define HISTORY_MIN (2u * 1024 * 1024)
#define KEEP_FREE   (8u * 1024 * 1024)   /* save service, audio, Continue state */

extern size_t gba_session_state_size(void);
extern bool gba_session_state_save(void *, size_t, size_t *);
extern bb_state_result gba_session_state_load(const void *, size_t, bool);
extern bool gba_session_rtc_get(void *, size_t), gba_session_rtc_set(const void *, size_t);

static bool ff_on, rw_hold, previous_toggle, ff_arm, touch_ff, touch_rw, touch_toggle;
static u32 blocked;
static unsigned step_phase;

static bool save(void *buf, size_t n)
{
	size_t len = 0;
	return gba_session_state_save(buf, n, &len) && len == n;
}
static bb_state_result load(const void *buf, size_t n)
{
	/* The clock goes on: keep the game's RTC as it is now (as state loads do). */
	u32 rtc[3];
	bool has_rtc = gba_session_rtc_get(rtc, sizeof(rtc));
	bb_state_result r = gba_session_state_load(buf, n, false);
	if (has_rtc && r != BB_STATE_DAMAGED)
		gba_session_rtc_set(rtc, sizeof(rtc));
	return r;
}
static const bb_rewind_ops ops = {.size = gba_session_state_size, .save = save, .load = load};

bool gba_speed_rewind_allowed(void) { return g_new3ds && gba_settings.rewind; }

void gba_speed_apply(void)
{
	if (gba_speed_rewind_allowed() == bb_rewind_active())
		return;
	if (bb_rewind_active()) {
		bb_rewind_stop();
		return;
	}
	u32 free_heap = bb_heap_available();
	size_t budget = free_heap > KEEP_FREE + HISTORY_MIN ? free_heap - KEEP_FREE : 0;
	if (budget > HISTORY_MAX) budget = HISTORY_MAX;
	if (budget < HISTORY_MIN || !bb_rewind_start(&ops, budget, 2))
		log_printf("gba: rewind unavailable (heap free %u)\n", (unsigned)free_heap);
}

void gba_speed_begin(u32 held)
{
	ff_on = rw_hold = previous_toggle = false;
	touch_ff=touch_rw=touch_toggle=false;
 blocked = held;   // buttons still held from the menu must be released first
	step_phase = 0;
	gba_speed_apply();
}

void gba_speed_end(void)
{
	bb_rewind_stop();
	ff_on = rw_hold = false;
}

void gba_speed_pause(u32 held)
{
	ff_on = ff_arm;   // the menu ends fast-forward, like PS1 (unless its Fast forward row asked for it)
	rw_hold = previous_toggle = ff_arm = false;
	touch_ff=touch_rw=touch_toggle=false;
 blocked = held;
	gba_speed_apply();   // the player may have changed Rewind in Settings
}

void gba_speed_touch(bool toggle,bool hold,bool rewind)
{touch_toggle=toggle;touch_ff=hold;touch_rw=rewind;}

u32 gba_speed_input(u32 held)
{
	blocked &= held;
	u32 consumed = 0, ff = input_hotkey_mask(0), rw = g_new3ds ? input_hotkey_mask(1) : 0;
	bool toggle = ff && !(ff & blocked) && (held & ff) == ff;
	if (toggle) consumed |= ff;
	rw_hold = rw && !(rw & blocked) && (held & rw) == rw;
	if (rw_hold) consumed |= rw;
	if ((toggle && !previous_toggle) || touch_toggle) ff_on = !ff_on;
 touch_toggle=false;
 rw_hold = rw_hold || (touch_rw && gba_speed_rewind_allowed());
	previous_toggle = toggle;
	if (rw_hold) ff_on = false;   // rewind takes priority over fast-forward
	return held & ~consumed;
}

/* Developer smoke tests (gba_test=2/3): hold the hotkeys by script. */
void gba_speed_force(bool ff, bool rw) { ff_on = ff; rw_hold = rw && g_new3ds; }

void gba_speed_ff_next(void) { ff_arm = true; }

int gba_speed_back(unsigned steps)
{
	unsigned done = 0;
	bb_state_result r = BB_STATE_OK;
	while (done < steps && bb_rewind_active()) {
		int step = bb_rewind_step(&r);
		if (step < 0)   // never play on in a half-restored game (it could write its battery)
			return r == BB_STATE_PARTLY ? GBA_REWIND_RESTART : -1;
		if (!step)
			break;
		done++;
	}
	step_phase = 0;
	log_printf("gba: pause rewind steps=%u of %u history_kib=%u\n", done, steps, gba_speed_history_kib());
	return (int)done;
}

bool gba_speed_ff(void) { return (ff_on || touch_ff) && !rw_hold; }
bool gba_speed_rewinding(void) { return rw_hold; }

int gba_speed_rewind_frame(bool *restart)
{
	*restart = false;
	if (!bb_rewind_active())
		return GBA_REWIND_OFF;
	// step back about 6 times a second of game time, shown at 20/s (PS1)
	if (++step_phase < 3)
		return GBA_REWIND_STEPPED;
	step_phase = 0;
	bb_state_result r;
	int step = bb_rewind_step(&r);
	if (step > 0)
		return GBA_REWIND_STEPPED;
	if (!step)
		return GBA_REWIND_END;
	// never play on in a half-restored game (it could write its battery)
	*restart = r == BB_STATE_PARTLY;
	return GBA_REWIND_FAILED;
}

void gba_speed_capture(void) { bb_rewind_capture(); }
void gba_speed_reset(void) { bb_rewind_reset(); step_phase = 0; }
unsigned gba_speed_history_kib(void) { return (unsigned)(bb_rewind_bytes() / 1024); }
