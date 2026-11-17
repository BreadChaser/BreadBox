// GBA fast-forward/rewind controls (systems/gba/gba_speed.c) against fakes:
// app hotkey pairs, toggle on the press edge, the pairs leave the game's
// buttons, held menu buttons are ignored until released, rewind wins over
// fast-forward, the Old 3DS has no rewind, Rewind setting starts/stops the
// history within the memory budget, and step results reach the caller.
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include "app_log.h"
#include "app_input.h"
#include "app_main.h"
#include "gba_settings.h"
#include "retro_rewind.h"
#include "gba_speed.h"
bool g_new3ds = true;
GbaSettings gba_settings = GBA_SETTINGS_DEFAULT;
static u32 masks[2] = { KEY_SELECT | KEY_R, KEY_SELECT | KEY_L };
u32 input_hotkey_mask(int i) { return masks[i]; }
static uint32_t heap = 40u << 20;
uint32_t bb_heap_available(void) { return heap; }
void log_printf(const char *fmt, ...) { (void)fmt; }
size_t gba_session_state_size(void) { return 416 * 1024; }
bool gba_session_state_save(void *b, size_t n, size_t *len) { (void)b; *len = n; return true; }
static bb_state_result load_result = BB_STATE_OK;
static unsigned rtc_sets;
bb_state_result gba_session_state_load(const void *b, size_t n, bool keep) { (void)b; (void)n; assert(!keep); return load_result; }
bool gba_session_rtc_get(void *p, size_t n) { (void)p; return n == 12; }
bool gba_session_rtc_set(const void *p, size_t n) { (void)p; rtc_sets += n == 12; return true; }
// rewind engine fake: records start budget, steps return a script
static bool active; static size_t started_budget; static int next_step = 1; static unsigned resets;
static const bb_rewind_ops *engine;
bool bb_rewind_start(const bb_rewind_ops *o, size_t b, int core) { assert(core == 2); engine = o; started_budget = b; return active = true; }
void bb_rewind_stop(void) { active = false; }
bool bb_rewind_active(void) { return active; }
void bb_rewind_capture(void) {}
int bb_rewind_step(bb_state_result *r) { *r = BB_STATE_OK; if (next_step < 0) *r = engine->load(NULL, 0); return next_step; }
void bb_rewind_reset(void) { resets++; }
size_t bb_rewind_bytes(void) { return 0; }
#include "../../systems/gba/gba_speed.c"

static unsigned picture_resets;
void gba_picture_reset(void){picture_resets++;}
int main(void)
{
	gba_speed_begin(KEY_SELECT | KEY_R);   // still held from the menu
	assert(gba_speed_input(KEY_SELECT | KEY_R) == (KEY_SELECT | KEY_R) && !gba_speed_ff());
	assert(active && started_budget == 16u << 20);
	assert(gba_speed_input(0) == 0);
	assert(gba_speed_input(KEY_SELECT) == KEY_SELECT && !gba_speed_ff());   // half a pair goes to the game
	assert(gba_speed_input(KEY_SELECT | KEY_R | KEY_A) == KEY_A && gba_speed_ff());   // press: on
	assert(gba_speed_input(KEY_SELECT | KEY_R) == 0 && gba_speed_ff());   // still held: no second toggle
	assert(gba_speed_input(KEY_B) == KEY_B && gba_speed_ff());
	assert(gba_speed_input(KEY_SELECT | KEY_L) == 0 && gba_speed_rewinding() && !gba_speed_ff());   // rewind wins, ends FF
	assert(gba_speed_input(0) == 0 && !gba_speed_rewinding() && !gba_speed_ff());
	gba_speed_input(KEY_SELECT | KEY_R); gba_speed_input(0); assert(gba_speed_ff());
	gba_speed_pause(KEY_A); assert(!gba_speed_ff() && picture_resets==1);   // the menu ends fast-forward (PS1)
	// steps: two shown frames per step, then the engine; results reach the caller
	bool restart;
	assert(gba_speed_rewind_frame(&restart) == GBA_REWIND_STEPPED && gba_speed_rewind_frame(&restart) == GBA_REWIND_STEPPED);
	next_step = 0; assert(gba_speed_rewind_frame(&restart) == GBA_REWIND_END && !restart);
	next_step = -1; load_result = BB_STATE_PARTLY;
	gba_speed_rewind_frame(&restart); gba_speed_rewind_frame(&restart);
	assert(gba_speed_rewind_frame(&restart) == GBA_REWIND_FAILED && restart && rtc_sets == 1);   // RTC kept even then
	gba_speed_reset(); assert(resets == 1);
	// Rewind setting off: the history goes when the menu closes; on again: back
	gba_settings.rewind = 0; gba_speed_pause(0); assert(!active);
	assert(gba_speed_rewind_frame(&restart) == GBA_REWIND_OFF);
	gba_settings.rewind = 1; heap = 9u << 20; gba_speed_pause(0); assert(!active);   // no room: stays off
	heap = 12u << 20; gba_speed_apply(); assert(active && started_budget == 4u << 20);
	gba_speed_end(); assert(!active);
	// Old 3DS: no rewind at all, SELECT+L reaches the game
	g_new3ds = false; gba_speed_begin(0); assert(!active);
	assert(gba_speed_input(KEY_SELECT | KEY_L) == (KEY_SELECT | KEY_L) && !gba_speed_rewinding());
	assert(gba_speed_input(KEY_SELECT | KEY_R) == 0 && gba_speed_ff());   // fast-forward works on Old
	masks[0] = 0; gba_speed_begin(0); assert(gba_speed_input(KEY_SELECT | KEY_R) == (KEY_SELECT | KEY_R) && !gba_speed_ff());   // hotkey off
 /* Touch shares the same speed state; simultaneous toggle edges toggle once. */
 g_new3ds=true;gba_settings.rewind=1;masks[0]=KEY_SELECT|KEY_R;gba_speed_begin(0);
 gba_speed_touch(true,false,false);gba_speed_input(KEY_SELECT|KEY_R);assert(gba_speed_ff());
 gba_speed_touch(false,false,false);gba_speed_input(0);assert(gba_speed_ff());
 gba_speed_touch(true,false,false);gba_speed_input(0);assert(!gba_speed_ff());
 gba_speed_touch(false,true,false);gba_speed_input(0);assert(gba_speed_ff());
 gba_speed_touch(false,true,true);gba_speed_input(0);assert(!gba_speed_ff() && gba_speed_rewinding());
 gba_speed_pause(0);assert(!gba_speed_ff() && !gba_speed_rewinding());
 g_new3ds=false;gba_speed_begin(0);gba_speed_touch(false,false,true);gba_speed_input(0);assert(!gba_speed_rewinding());
 puts("GBA touch + physical speed: one toggle, hold, rewind priority, pause release and Old mask: PASS");
	puts("GBA fast-forward/rewind controls: hotkey pairs, edges, menu blocking, priority, Old 3DS, setting and memory budget: PASS");
	return 0;
}
