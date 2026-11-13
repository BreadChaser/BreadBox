#include "ht_trace.h"
// pace: pre-present diagnostic work intervals. refresh: post-vblank display gaps.
// Only refresh counts establish lost refreshes; pace parts help attribute them.
#include "app_policy.h"
#include <string.h>
#include "app_log.h"
#include "platform_video.h"
#include "app_main.h"
#include "room.h"
#include "room_refresh.h"
#include <citro3d.h>

static const char *const part_name[ROOM_PACE_PARTS] = { "before", "top", "bottom", "spines", "hands", "library", "tv", "preload", "labels", "logic", "cover" };
static struct {
	const char *label;
	u64 last, part[ROOM_PACE_PARTS];
	u64 sum, min, max, before_max, tv_max;
	unsigned frames, slow;
 bool diagnostic;
	u64 window;
} P;
static struct {
 u64 last, window, returned;
 unsigned hist[16], frames, lost, worst;
 const char *label;
 bool armed, diagnostic, previous_diagnostic;
} R;
void room_pace_diagnostic(void) { R.diagnostic = true; }
bool room_pace_return_pending(void) { return R.returned!=0; }
void room_pace_return_start(void) { R.returned = svcGetSystemTick(); R.last=0; }
static void refresh_report(void)
{
 if(!R.frames)return;
 unsigned n=0,p99=0,three_plus=0;
 for(unsigned i=3;i<16;i++)three_plus+=R.hist[i];
 for(unsigned i=0;i<16;i++){n+=R.hist[i];if(n*100>=R.frames*99){p99=i;break;}}
 log_printf("refresh: %s frames %u lost %u three_plus %u max %u p99 %u refreshes (post-vblank, diagnostics excluded)\n",
  R.label?R.label:"room",R.frames,R.lost,three_plus,R.worst,p99);
 memset(R.hist,0,sizeof(R.hist));R.frames=R.lost=R.worst=0;
}
void room_pace_presented(void)
{
 if(!R.armed){R.last=0;R.returned=0;return;}
 R.armed=false;
 u64 now=svcGetSystemTick();
 if(R.returned){
  unsigned gap=room_refresh_gap(now-R.returned,SYSCLOCK_ARM11);
  log_printf("return: first room frame +%lu ms from menu restore, first present gap %u lost %u refreshes since menu restore\n",
   (unsigned long)((now-R.returned)/(SYSCLOCK_ARM11/1000)),gap,room_refresh_lost(gap,video_ui_steps()));
  R.returned=0;
  // Restore/teardown latency is a transition measurement, not a steady frame.
  // Arm steady accounting at this first present; never freeze trace on it.
  R.last=0;R.window=now;
  memset(R.hist,0,sizeof(R.hist));R.frames=R.lost=R.worst=0;
 }
 if(R.last) {
  unsigned gap=room_refresh_gap(now-R.last,SYSCLOCK_ARM11), lost=room_refresh_lost(gap,video_ui_steps());
  if(R.diagnostic || R.previous_diagnostic)log_printf("refresh: diagnostic gap %u lost %u in %s\n",gap,lost,R.label);
  else {
   R.frames++;R.hist[gap<16?gap:15]++;R.lost+=lost;if(gap>R.worst)R.worst=gap;
   if(lost){ht_freeze();log_printf("refresh: lost %u gap %u in %s\n",lost,gap,R.label);}
  }
 }
 R.previous_diagnostic=R.diagnostic;R.diagnostic=false;R.last=now;
 if(now-R.window>SYSCLOCK_ARM11*5){refresh_report();R.window=now;}
}
static void (*chained)(void);

static double ms(u64 ticks) { return (double)ticks * 1000 / SYSCLOCK_ARM11; }

static void report(void)
{
	if (P.frames && P.label)
		log_printf("pace: %s%s %u frames avg %.2f min %.2f max %.2f ms, %u over %d ms\n", P.diagnostic?"diagnostic ":"", P.label, P.frames,
			ms(P.sum) / P.frames, ms(P.min), ms(P.max), P.slow, 20 * video_ui_steps());   // 40 in the Old's 30 fps room
	if(P.frames && P.label)log_printf("pace: %s%s work max before %.2f tv %.2f ms (diagnostic attribution)\n", P.diagnostic?"diagnostic ":"",P.label,ms(P.before_max),ms(P.tv_max));
	P.before_max=P.tv_max=0;
	P.frames = P.slow = 0; P.diagnostic=false;
	P.sum = P.max = 0;
	P.min = ~0ull;
}

// A screen starts (the shelf after a game): no frame from before it counts (a game ran between)
void room_pace_restart(void)
{
	report();
	refresh_report(); R.label=NULL;
	P.label = NULL;
	P.last = 0;
}

void room_pace_add(int part, u64 ticks)
{
	if ((unsigned)part < ROOM_PACE_PARTS) P.part[part] += ticks;
}

void room_pace_frame(const char *label)
{
	u64 now = svcGetSystemTick();
 R.armed=true;
 if(R.label!=label){refresh_report();R.label=label;R.window=now;}
	if (label != P.label) {   // another moment: its own numbers (labels are string constants)
		report();
		P.label = label;
		P.window = now;
		P.last = 0;
	}
 if(P.part[ROOM_PACE_BEFORE]>P.before_max)P.before_max=P.part[ROOM_PACE_BEFORE];
 if(P.part[ROOM_PACE_TV]>P.tv_max)P.tv_max=P.part[ROOM_PACE_TV];
 P.diagnostic |= R.diagnostic;
	if (P.last) {
		u64 dt = now - P.last;
		P.frames++;
		P.sum += dt;
		if (dt < P.min) P.min = dt;
		if (dt > P.max) P.max = dt;
		if (dt > SYSCLOCK_ARM11 / 50 * (u64)video_ui_steps()) {   // the Old's 30 fps room: frames are two vblanks long
			P.slow++;
			char parts[200];
			int n = 0;
			u64 sum = 0;
			for (int i = 0; i < ROOM_PACE_PARTS; i++) {
				n += snprintf(parts + n, sizeof(parts) - n, " %s %.2f", part_name[i], ms(P.part[i]));
				if (i != ROOM_PACE_HANDS && i != ROOM_PACE_SPINES && i != ROOM_PACE_COVER && i != ROOM_PACE_LABELS) sum += P.part[i];   // those are inside top / before
			}
			// what the parts do not cover (waiting: a lock, the SD card, the GPU), citro3d's CPU time and the GPU's last frame.
			// Layout: this is C3D_GetProcessingTime's only caller. Without one the linker drops it from .menu_text and six
			// pinned video frame functions after it move 24 bytes (tools/layout/baseline-check.sh fails). Keep a caller.
			log_printf("pace: %sslow frame %.2f ms in %s:%s wait %.2f cpu %.2f gpu %.2f\n", R.diagnostic ? "diagnostic " : "", ms(dt), label, parts,
				dt > sum ? ms(dt - sum) : 0.0, (double)C3D_GetProcessingTime(), (double)C3D_GetDrawingTime());
		}
	}
	P.last = now;
	memset(P.part, 0, sizeof(P.part));
	if (now - P.window > SYSCLOCK_ARM11 * 5) {
		report();
		P.window = now;
	}
}

// Old 3DS: the covers' workers share core 0 with this thread at its priority (they must
// not starve under a busy list), so a cover being decoded took whole frames from an
// animation. The room and the Library need 2-3 ms a frame: one priority above them, this
// thread draws on time and the workers get the rest of every frame (it sleeps on vsync).
bool room_pace_priority(bool on)
{
	static s32 base = -1;
	bool was = base >= 0;
	if (g_new3ds) return false;   // the New's workers have core 2
	if (on && base < 0 && R_SUCCEEDED(svcGetThreadPriority(&base, CUR_THREAD_HANDLE)) && base > 0x18)
		svcSetThreadPriority(CUR_THREAD_HANDLE, base - 1);
	else if (!on && base >= 0) {
		svcSetThreadPriority(CUR_THREAD_HANDLE, base);
		base = -1;
	}
	return was;
}

// the frame-start work (cover uploads, spine bakes) timed as "before"
static void timed_pre_draw(void)
{
	void (*next)(void) = chained;
	video_pre_draw = NULL;
	chained = NULL;
	u64 t0 = svcGetSystemTick();
	if (next) next();
	room_pace_add(ROOM_PACE_BEFORE, svcGetSystemTick() - t0);
}

void room_pace_hook(void)
{
	if (video_pre_draw == timed_pre_draw || !video_pre_draw) return;
	chained = video_pre_draw;
	video_pre_draw = timed_pre_draw;
}
