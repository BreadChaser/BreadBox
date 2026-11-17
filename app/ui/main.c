#define _GNU_SOURCE   // strcasestr
#include "ht_trace.h"
#include "app_policy.h"
// BreadBox entry point.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "theme.h"
#include "icons.h"
#include "app_paths.h"
#include "cold_code.h"
#include "app_log.h"
#include "app_config.h"
#include "last_picture.h"
#include "bb_pause.h"
#include "bb_platform.h"
#include "platform_video.h"
#include "core_hooks.h"
#include "dev_switches.h"
#include "dsp_firmware.h"
#include "app_input.h"
#include "presets.h"
#include "ui_pause.h"
#include "ui.h"
#include "library.h"
#include "covers.h"
#include "uisound.h"
#include "libui.h"
#include "playlog.h"
#include "home.h"
#include "games_dir.h"
#include "browser.h"
#include "app_main.h"
#include "breadbox_import.h"
#include "welcome.h"
#include "whatsnew.h"
#include "icons.h"
#include "room.h"
#include "scene.h"
#include "bb_registry.h"
#include "system_3d.h"
int breadbox_systems_run(char *, size_t);
void breadbox_return_system(const bb_system_t *system,const char *path);
void room_walls_game_ended(const bb_system_t *system,const char *path,const char *title);
#include "bb_index.h"
#include "bb_save.h"
#include "bb_update.h"
#include "bb_update_phase.h"
#include "presetupdate.h"
#include "devtools.h"
#include "clip.h"
#include "../../core/plugins/gpu_pica/gpu_pica_debug.h"
#include "../../core/plugins/gpu_pica/gpu_pica.h"

// Main thread stack; the core warns below 1MB (dynarec recursion).
u32 __stacksize__ = 0x100000;

// Replaces libctru's heap setup (same steps, libctru 2.6). The default hands
// nearly all free memory to the heaps; we size them from what this console
// actually gives us (~124MB on New 3DS, far less on Old 3DS) and leave a
// reserve so the core can map PS1 RAM/BIOS/LUTs at fixed addresses, which
// makes the dynarec's memory accesses much faster.
extern u32 __ctru_heap, __ctru_heap_size, __ctru_linear_heap, __ctru_linear_heap_size;
extern char *fake_heap_start, *fake_heap_end;
void mappableInit(u32 addrMin, u32 addrMax);

#define MB (1024 * 1024)

void __system_allocateHeaps(void)
{
	Handle reslimit = 0;
	s64 max_commit = 0, cur_commit = 0;
	ResourceLimitType type = RESLIMIT_COMMIT;

	if (R_FAILED(svcGetResourceLimit(&reslimit, CUR_PROCESS_HANDLE)))
		svcBreak(USERBREAK_PANIC);
	svcGetResourceLimitLimitValues(&max_commit, reslimit, &type, 1);
	svcGetResourceLimitCurrentValues(&cur_commit, reslimit, &type, 1);
	svcCloseHandle(reslimit);

	u32 remaining = (u32)(max_commit - cur_commit) & ~0xFFF;
	// RAM 0x210000 + scratch 0x10000 + BIOS 0x80000 + LUTs 0x800000
	// need 10.625 MiB outside the heaps. An 8 MiB reserve made the LUT
	// mapping fall back to calloc, taking 8 MiB from the save-state heap.
	// With the 8 MiB static dynarec cache the Old 3DS can afford 12 MiB.
	u32 reserve = 12 * MB;
	u32 linear = remaining / 3;
	if (linear > 24 * MB) linear = 24 * MB;
	// GPU atlas/staging/UI consume ~13.5 MiB before core startup. Leave
	// another ~3 MiB for core VRAM/vout and ~1.4 MiB for eleven thumbnails.
	if (linear < 19 * MB) linear = 19 * MB;
	u32 heap = remaining > linear + reserve + 8 * MB ? remaining - linear - reserve : 8 * MB;
	if (heap > 64 * MB) heap = 64 * MB;
	if (heap + linear > remaining)
		heap = remaining - linear;   // tiny console: give up the reserve
	__ctru_heap_size = heap & ~0xFFF;
	__ctru_linear_heap_size = linear & ~0xFFF;

	if (R_FAILED(svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0, __ctru_heap_size,
			MEMOP_ALLOC, MEMPERM_READ | MEMPERM_WRITE)))
		svcBreak(USERBREAK_PANIC);
	if (R_FAILED(svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size,
			MEMOP_ALLOC_LINEAR, MEMPERM_READ | MEMPERM_WRITE)))
		svcBreak(USERBREAK_PANIC);
	mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);

	fake_heap_start = (char *)__ctru_heap;
	fake_heap_end = fake_heap_start + __ctru_heap_size;
}

// Set when we may use the kernel calls the dynarec needs (Luma3DS gives
// homebrew full SVC access). Without it the core uses the slow interpreter.
int __ctr_svchax;
int g_slow_reason;

const char *slow_mode_notice(bool shortform)
{
	switch (g_slow_reason) {
	case SLOW_EMULATOR:
		return shortform ? "PC emulator: games run slowly" :
			"Running in a PC emulator: fast mode is off, so games run much slower than on a real 3DS.";
	case SLOW_NO_LUMA:
		return shortform ? "Fast mode needs Luma3DS" :
			"Fast mode needs Luma3DS: games will run slowly.";
	}
	return NULL;
}
bool g_new3ds;
bool g_3d_screen;
int g_syscore_pct;   // syscore (core 1) time granted to us, percent

static bool detect_svc_access(void)
{
	s64 out = 0;
	// SystemInfo type 0x10000 is a Luma3DS extension; it errors elsewhere.
	return R_SUCCEEDED(svcGetSystemInfo(&out, 0x10000, 0));
}

// Citra/Azahar answer SystemInfo type 0x20000 ("is emulator"); real consoles
// return an error. Those emulators can't run Luma's cache-flush kernel call,
// so the dynarec's freshly written code goes stale and crashes there.
static bool running_in_emulator(void)
{
	s64 out = 0;
	// Citra reports 1, Azahar 2
	return R_SUCCEEDED(svcGetSystemInfo(&out, 0x20000, 0)) && out > 0;
}

// A failure the player can act on: a title line, then what happened and what to do.
static const ui_button_t error_bar[1] = { { KEY_A, "OK", UI_ICON_CHECK, false } };
static void draw_message(void *ud); /* shared presentation */

static void show_error(const char *msg)
{
	log_printf("error: %s\n", msg);
	bool released = false;
	while (aptMainLoop()) {
		dev_tick_keys();   // remote ui-key and shot still answer here
		input_poll();
		u32 k = ui_header_back(ui_bar_input(error_bar, 1, input_keys_down()));
        k=ui_swap_keys(k,UI_SWAP_MENU);
		if (released && (k & (KEY_A | KEY_B)))
			break;
		if (!(input_keys_held() & (KEY_A | KEY_B | KEY_TOUCH)))
			released = true;
		ui_begin();
		video_present_vsync(false, ui_menu_top, draw_message, (void *)msg);
	}
}

// Diagnostic switches, passed as arguments (3dslink ... -- nonet nosound):
// each turns one subsystem off so a problem can be bisected without
// rebuilding.
float sw_slider = -1;   // test: pretend the 3D slider is here
int sw_stereo = -1;     // test: 3D mode 0/1/2 instead of the setting (New 3DS)
bool sw_prof;           // log where frame time goes, once a second
bool sw_stats;          // log fps and renderer counters once a second (no timers)
bool sw_o3ds;           // test: behave like an Old 3DS (settings not saved)
bool sw_ff;
bool sw_pmuphase, sw_pmuclass;
bool sw_fs3;            // test: Old 3DS auto frameskip may skip up to 3 in a row
bool sw_fs1;            // test: force frameskip, every other frame
bool sw_noskip;         // test: no frameskip (benchmarks)
bool sw_nosputhread;    // test: Old 3DS: sound mixing on the main core
bool sw_acap;           // test: log a checksum of 10s of the core's audio, from frame 60
char sw_opt[8][48];     // test: o:<option>=<value> (pcsx_rearmed_ left out), up to 8
char sw_cdra[8];        // test: Old 3DS: CD read-ahead sectors (on the syscore)
char sw_clock[8];       // test: emulated PS1 CPU clock (pcsx_rearmed_psxclock), e.g. 40
int sw_gldbg;           // test: renderer diagnostics (gpu_pica_debug.h)
char sw_game[64];       // test: start the first library game whose title contains this
int sw_vramdump;         // test: dump PS1 VRAM this many frames after the game starts
int sw_pmu0 = -1, sw_pmu1 = -1;   // test: ARM11 performance counter events (pmu=AA,BB, hex)
int sw_autosave_test;    // test: periodic auto-save every N s into slot 9, also in remote games
int sw_state = -1;       // test: load this save state slot when the game starts (state0, state=N)
bool sw_nogputhread;
bool sw_gpu_async = true;
bool sw_gpu_flush = true;
bool sw_aa;              // test: Old 3DS: gameplay supersampled too
bool sw_csnd, sw_nonet, sw_nosound, sw_noirrst, sw_noaa, sw_nolcd, sw_nocovers, sw_sfxlog, sw_framelog;
char sw_presets_url[96];  // test: preset update server (numeric LAN HTTP), check now

extern int psx3d_idle_skip;
extern int gs_defer, gs_cap_auto;

static void parse_switches(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "nonet")) sw_nonet = true;
		else if (!strcmp(a, "csnd")) sw_csnd = true;   // test the no-firmware sound path
		else if (!strncmp(a, "slider=", 7)) sw_slider = strtof(a + 7, NULL);
		else if (!strncmp(a, "stereo=", 7)) sw_stereo = atoi(a + 7);
		else if (clip_switch(a)) {}
        else if (bb_system_switch(a)) {}
		else if (!strcmp(a, "prof")) sw_prof = true;
		else if (!strcmp(a, "stats")) sw_stats = true;
		else if (!strcmp(a, "wsbundle=on")) sw_wsbundle = true;
		else if (!strcmp(a, "wsbundle=off")) sw_wsbundle = false;
		else if (!strcmp(a, "wsscreen=4:3")) sw_wsscreen = 0;
		else if (!strcmp(a, "wsscreen=stretch")) sw_wsscreen = 1;
		else if (!strcmp(a, "wsscreen=wide")) sw_wsscreen = 2;
		else if (!strcmp(a, "o3ds")) sw_o3ds = true;
		else if (!strcmp(a, "fs1")) sw_fs1 = true;
		else if (!strcmp(a, "pmuclass=on")) sw_pmuclass = true;
		else if (!strcmp(a, "pmuclass=off")) sw_pmuclass = false;
		else if (!strcmp(a, "pmurender=on")) sw_pmuphase = true;
		else if (!strcmp(a, "pmurender=off")) sw_pmuphase = false;
		else if (!strcmp(a, "ff=on")) sw_ff = true;
		else if (!strcmp(a, "ff=off")) sw_ff = false;
		else if (!strcmp(a, "fs3")) sw_fs3 = true;
		else if (!strcmp(a, "idle=off")) psx3d_idle_skip = 0;
		else if (!strcmp(a, "idle=on")) psx3d_idle_skip = 1;
		else if (!strcmp(a, "stcap=main")) gs_defer = gs_cap_auto = 0;   // test: Real 3D's capture on the main thread
		else if (!strcmp(a, "stcap=gpu")) gs_defer = 1, gs_cap_auto = 0;
		else if (!strcmp(a, "stcap=auto")) gs_cap_auto = 1;
		else if (!strcmp(a, "noskip")) sw_noskip = true;
		else if (!strcmp(a, "nogputhread")) sw_nogputhread = true;
		else if (!strcmp(a, "gputhread")) sw_nogputhread = false;
		else if (!strcmp(a, "nosputhread")) sw_nosputhread = true;
		else if (!strcmp(a, "acap")) sw_acap = true;
		else if (!strcmp(a, "renderer=gpu")) { /* legacy key: GPU only */ }
		else if (!strcmp(a, "state0")) sw_state = 0;
		else if (!strncmp(a, "state=", 6)) sw_state = atoi(a + 6);
		else if (!strncmp(a, "autosavetest=", 13)) sw_autosave_test = atoi(a + 13);
		else if (!strcmp(a, "rtt=old")) sw_gldbg = (sw_gldbg | PICA_DEBUG_RTT_OLD) & ~PICA_DEBUG_RTT_NEW;
		else if (!strcmp(a, "rtt=new")) sw_gldbg = (sw_gldbg & ~PICA_DEBUG_RTT_OLD) | PICA_DEBUG_RTT_NEW;
		else if (!strcmp(a, "rtt=auto")) sw_gldbg &= ~(PICA_DEBUG_RTT_OLD | PICA_DEBUG_RTT_NEW);
		else if (!strcmp(a, "rttcache=old")) sw_gldbg |= PICA_DEBUG_NO_REGION_CACHE;
		else if (!strcmp(a, "rttcache=new")) sw_gldbg &= ~PICA_DEBUG_NO_REGION_CACHE;
		else if (!strcmp(a, "submitstate=old")) pica_submit &= ~PICA_SUBMIT_STATE;
		else if (!strcmp(a, "submitstate=new")) pica_submit |= PICA_SUBMIT_STATE;
		else if (!strcmp(a, "submitclip=old")) pica_submit &= ~PICA_SUBMIT_CLIP;
		else if (!strcmp(a, "submitclip=new")) pica_submit |= PICA_SUBMIT_CLIP;
		else if (!strcmp(a, "submittex=old")) pica_submit &= ~PICA_SUBMIT_TEX;
		else if (!strcmp(a, "submittex=new")) pica_submit |= PICA_SUBMIT_TEX;
		else if (!strcmp(a, "pica_prims_old")) pica_prims = 0;
		else if (!strcmp(a, "pica_prims_fast")) pica_prims = 7;
		else if (!strcmp(a, "pica_prims_copy")) pica_prims |= 1;
		else if (!strcmp(a, "pica_prims_clip")) pica_prims |= 2;
		else if (!strcmp(a, "pica_prims_round")) pica_prims |= 4;
		else if (!strcmp(a, "skipgate=old")) psx3d_skipgate = 0;
		else if (!strcmp(a, "skipgate=new")) psx3d_skipgate = 1;
		else if (!strcmp(a, "texmemo=old")) pica_texmemo = false;
		else if (!strcmp(a, "texmemo=new")) pica_texmemo = true;
		else if (!strcmp(a, "texmask=old")) pica_texmask = false;
		else if (!strcmp(a, "texmask=new")) pica_texmask = true;
		else if (!strncmp(a, "gldbg=", 6)) sw_gldbg = atoi(a + 6);
		else if (!strncmp(a, "vramdump=", 9)) sw_vramdump = atoi(a + 9);
		else if (!strncmp(a, "pmu=", 4)) sscanf(a + 4, "%x,%x", (unsigned *)&sw_pmu0, (unsigned *)&sw_pmu1);
		else if (!strncmp(a, "o:", 2)) {
			for (int k = 0; k < 8; k++)
				if (!sw_opt[k][0]) { snprintf(sw_opt[k], sizeof(sw_opt[k]), "%s", a + 2); break; }
		}
		else if (!strncmp(a, "cdra=", 5)) snprintf(sw_cdra, sizeof(sw_cdra), "%s", a + 5);
		else if (!strncmp(a, "clock=", 6)) snprintf(sw_clock, sizeof(sw_clock), "%s", a + 6);
		else if (!strncmp(a, "game=", 5)) snprintf(sw_game, sizeof(sw_game), "%s", a + 5);
		else if (!strcmp(a, "nosound")) sw_nosound = true;
		else if (!strcmp(a, "sfxlog")) sw_sfxlog = true;
		else if (!strcmp(a, "framelog")) sw_framelog = true;
		else if (!strcmp(a, "noirrst")) sw_noirrst = true;
		else if (!strcmp(a, "noaa")) sw_noaa = true;
		else if (!strcmp(a, "gpuasync")) sw_gpu_async = true;
		else if (!strcmp(a, "nogpuasync")) sw_gpu_async = false;
		else if (!strcmp(a, "gpuflush")) sw_gpu_flush = true;
		else if (!strcmp(a, "nogpuflush")) sw_gpu_flush = false;
		else if (!strcmp(a, "aa")) sw_aa = true;
		else if (!strcmp(a, "nolcd")) sw_nolcd = true;
		else if (!strcmp(a, "nocovers")) sw_nocovers = true;
		else if (!strncmp(a, "presets_url=", 12)) snprintf(sw_presets_url, sizeof(sw_presets_url), "%s", a + 12);
		else if (!strcmp(a, "minimal"))
			sw_nonet = sw_nosound = sw_noirrst = sw_noaa = sw_nolcd = sw_nocovers = true;
	}
}

bool sw_wsbundle = true;
int sw_wsscreen = -1;

void debug_parse_switch(const char *word)
{
	char *argv[2] = {NULL, (char *)word};
	parse_switches(2, argv);
}

// Developer switches for consoles started from the HOME Menu instead of 3dslink.
static void parse_switch_file(void)
{
	FILE *f = fopen(PSX3D_DIR "/switches.txt", "r");   // BreadBox's own file only; PSX3D's is never read
	if (!f)
		return;
	char word[384];
	while (fscanf(f, "%383s", word) == 1) {
		// Control/install permission is granted only by the on-card file.
		if (!strcmp(word, "bench-exit=home")) dev_bench_home = true;
		else if (!strcmp(word, "remote")) dev_remote = true;
		else if (!strcmp(word, "bench")) dev_bench = true;
		else if (!strncmp(word, "token=", 6)) snprintf(dev_token, sizeof(dev_token), "%.95s", word + 6);
		else if (!strncmp(word, "update=", 7)) snprintf(dev_update_url, sizeof(dev_update_url), "%.255s", word + 7);
		char *argv[2] = { NULL, word };
		parse_switches(2, argv);
	}
	fclose(f);
	dev_authorize();
}

float slider_get(void)
{
	return sw_slider >= 0 ? sw_slider : osGet3DSliderState();
}

// A game starts: the menus' pictures give their memory to it first.
static char launch_error[384];
static bool start_game(const char *path, const char *save_name, const char *title)
{
 if(bb_platform_saves(BB_SAVES_START)==BB_SAVES_CLOSE)return false;   /* the last game's saves first; closing: no game */
 const bb_system_t *system=bb_system_for_path(path);
 if(!system) {
  snprintf(launch_error,sizeof(launch_error),"Could not start the game\n" APP_NAME " can't tell which system this file is for. It may be damaged, or in a format " APP_NAME " doesn't open.");
  if(dev_remote_game())dev_game_ended(false);
  return false;
 }
 bb_launch launch={.path=path,.save_name=save_name,.title=title,.slot=BB_SLOT_NONE,.stereo=-1,.remote=dev_remote_game(),.resume=bb_session_resume_requested()};
 if(dev_remote_game()) {launch.slot=bb_remote_slot;launch.stereo=bb_remote_stereo;}
 snprintf(launch_error,sizeof(launch_error),"System could not start");
 if(system->identify && !system->identify(path,NULL,&launch.id,launch_error,sizeof(launch_error))) {
  if(dev_remote_game())dev_game_ended(false);return false;
 }
 if(!bb_session_begin(system,&launch)) {
  snprintf(launch_error,sizeof(launch_error),covers_wait_error()?covers_wait_error():(system->models & (g_new3ds?BB_MODEL_NEW:BB_MODEL_OLD))?"Could not start the game\nNot enough memory to start this system.":"This system needs a New 3DS");
  if(dev_remote_game())dev_game_ended(false);
  return false;
 }
 breadbox_return_system(system,path);
 bb_run_result result=system->run(&launch,launch_error,sizeof(launch_error));
 bool played=!bb_continue_went_back_take();   /* Back at the Continue question: nothing changed */
 /* the room's TV shows where you left off: systems without a Continue picture of
  * their own (the shared save service) leave their last frame on the top screen */
 if(played && result!=BB_RUN_FAILED && system->save && gspHasGpuRight() && !bb_pause_last_picture(path))
  bb_last_picture_from_screen(path);   /* also remote-started; quit from the pause card: the game's own frame */
 bb_session_end();
 if(played && result!=BB_RUN_FAILED)room_walls_game_ended(system,path,title);   /* the room's corkboard: this game's new postcards and its ticket */
 return result!=BB_RUN_FAILED;
}

static u64 boot_t0;
static unsigned boot_seen;   // the table covers the cold start only: closed once the first frame and the music are in it
static bool boot_closed, boot_logging;
static struct { const char *what; unsigned ms; } boot_early[8];   // marks before the log file is open
static int boot_nearly;
static unsigned boot_ms(void) { return (unsigned)((svcGetSystemTick() - boot_t0) / (SYSCLOCK_ARM11 / 1000)); }
void boot_mark(const char *what)
{
	if (boot_closed) return;
	unsigned ms = boot_ms();
	if (!boot_logging && strcmp(what, "log open")) {   // the log is not open yet: kept for it
		if (boot_nearly < 8) boot_early[boot_nearly++] = (typeof(boot_early[0])){ what, ms };
		return;
	}
	if (!boot_logging) {
		boot_logging = true;
		for (int i = 0; i < boot_nearly; i++) log_printf("boot: %s +%u ms\n", boot_early[i].what, boot_early[i].ms);
	}
	boot_seen |= !strcmp(what, "first frame") ? 1 : !strncmp(what, "music", 5) ? 2 : 0;
	boot_closed = boot_seen == 3;
	log_printf("boot: %s +%u ms\n", what, ms);
}

// A thread that did not stop (bb_exit_abandoned: a join timed out, or a system left its
// core running on HOME > Close) must not see anything it can reach freed. libctru's own
// exit after main returns frees plenty before it ends the process: __appExit's service
// teardown (archiveUnmountAll, fsExit, hidExit, aptExit joining APT's thread, srvExit),
// then __libctru_exit's two whole-heap unmaps (the crash after the 16424 update: the NDSP
// thread's stack went with them), envDestroyHandles, __sync_fini. So main ends the process
// here the way that exit's last two steps do (libctru 2.7.0 aptExit:
// APT_PrepareToCloseApplication; __libctru_exit's callback aptExitProcess:
// APT_CloseApplication, which has NS end us, then svcExitProcess), with nothing between:
// the kernel stops every thread before it frees their memory. Back to HOME, never a
// relaunch (the chainload is applied only by aptExit, which does not run). The socket
// service is closed first: a process that ended with it open crashed the 3DS's process
// manager (covers.c); socExit frees no memory of ours, a live worker's next
// socket call fails. The log writer gets a moment; lines still queued are lost.
// A 3dsx cannot do this: its loader goes on in this process and needs it whole, so there
// main returns with the thread still running, and the loader's heap unmap can fault it.
// That is a known limit of loader builds; the CIA is what ships.
PSX3D_COLD static void exit_terminal(void)
{
	log_printf("exit: a thread did not stop: the system ends the app\n");
	log_soon();
	svcSleepThread(50000000LL);
	if (envIsHomebrew())
		return;
	socExit();
	APT_PrepareToCloseApplication(true);
	APT_CloseApplication(NULL, 0, 0);
	svcExitProcess();
}

// A teardown that libctru cannot bound (ndspExit and gfxExit join their own
// threads without a timeout) runs on a helper thread; main waits at most timeout.
// Each helper gets its own step (read-only) as its argument: one that has timed out
// may not have started yet, and must still run the step it was given.
typedef struct { void (*fn)(void); } exit_step_t;
static const exit_step_t exit_audio = { uisound_exit }, exit_gpu = { gfxExit };
PSX3D_COLD static void exit_step_main(void *step) { ((const exit_step_t *)step)->fn(); }
PSX3D_COLD static bool exit_step(const exit_step_t *step, u64 timeout)
{
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	Thread t = threadCreate(exit_step_main, (void *)step, 16 * 1024, prio, -2, false);
	if (!t || threadJoin(t, timeout) != 0)   // a timeout is not R_FAILED
		return false;
	threadFree(t);
	return true;
}

int main(int argc, char **argv)
{
 ht_init();
	char path[PATH_LEN];
	boot_t0 = svcGetSystemTick();
	parse_switches(argc, argv);
	parse_switch_file();
	boot_mark("switches");

	// New 3DS: 804 MHz + L2 cache
	osSetSpeedupEnable(true);
	if (!sw_noirrst)
		irrstInit();
	// the core asks cfg:u for the console model to count CPU cores; without
	// it, it assumes one core and never starts its graphics thread
	cfguInit();
	boot_mark("services");

	mkdir(PSX3D_DIR, 0777);
	mkdir(PSX3D_BIOS_DIR, 0777);
	mkdir(PSX3D_SAVE_DIR, 0777);
	mkdir(PSX3D_STATE_DIR, 0777);
	boot_mark("folders");
	log_init();
	bb_save_apt_init();   // HOME / sleep wait for the saves queued after a game
	log_printf("" APP_NAME " " PSX3D_VERSION "\n");
	boot_mark("log open");
	{
		APT_CheckNew3DS(&g_new3ds);
		u8 model = CFG_MODEL_2DS;
		if (R_SUCCEEDED(CFGU_GetSystemModel(&model)))
			g_3d_screen = system_3d_screen_model(model, CFG_MODEL_2DS, CFG_MODEL_N2DSXL);
		if (sw_o3ds)
			g_new3ds = false;   // test: Old 3DS code paths at New 3DS clock
		// borrow the syscore: as much as app.rsf allows on Old 3DS (30%), 35% on New
		{
			static const u32 want_o3ds[] = { 80, 70, 60, 50, 40, 35, 30, 25 };
			u32 pct = 0;
			if (g_new3ds) {
				Result rc = APT_SetAppCpuTimeLimit(35);
				log_printf("syscore: set 35 -> %08lx\n", rc);
			} else {
				for (size_t k = 0; k < sizeof(want_o3ds) / sizeof(want_o3ds[0]); k++) {
					Result rc = APT_SetAppCpuTimeLimit(want_o3ds[k]);
					log_printf("syscore: set %lu -> %08lx\n", want_o3ds[k], rc);
					if (R_SUCCEEDED(rc))
						break;
				}
			}
			APT_GetAppCpuTimeLimit(&pct);
			g_syscore_pct = (int)pct;
			log_printf("syscore time: %lu%%\n", pct);
		}
		// the menu's helpers off the system core on the Old 3DS (its GPU / screen events wait for
		// our work there): the log's and the later thread on core 0 below the menu, New core 2
		log_writer_start(g_new3ds ? 2 : 0);
		bb_later_place(g_new3ds ? 2 : 0, g_new3ds ? 0x30 : 0x31);
		log_printf("%s 3DS, heap %luMB, linear %luMB (%luMB free)\n", g_new3ds ? "New" : "Old",
			__ctru_heap_size / MB, __ctru_linear_heap_size / MB, linearSpaceFree() / MB);
	}

	__ctr_svchax = detect_svc_access();
	if (__ctr_svchax && running_in_emulator()) {
		log_printf("running in Citra/Azahar: dynarec disabled\n");
		__ctr_svchax = 0;
		g_slow_reason = SLOW_EMULATOR;
	} else if (!__ctr_svchax)
		g_slow_reason = SLOW_NO_LUMA;
	log_printf("svc access: %d\n", __ctr_svchax);
	if (sw_stats) {
		// test: how fast the CPU reads and writes normal vs GPU-visible (linear) memory
		const int n = 256 * 1024;
		u32 *bufs[2] = { malloc(n), linearAlloc(n) };
		for (int k = 0; k < 2; k++) {
			volatile u32 *b = bufs[k];
			if (!b)
				continue;
			u64 t0 = svcGetSystemTick();
			for (int r = 0; r < 4; r++)
				for (int i = 0; i < n / 4; i++)
					b[i] = i;
			u64 t1 = svcGetSystemTick();
			u32 sum = 0;
			for (int r = 0; r < 4; r++)
				for (int i = 0; i < n / 4; i++)
					sum += b[i];
			u64 t2 = svcGetSystemTick();
			log_printf("memtest %s: write %.2f MB/s, read %.2f MB/s (%u)\n", k ? "linear" : "heap",
				4.0 * n / ((t1 - t0) / (double)SYSCLOCK_ARM11) / 1e6, 4.0 * n / ((t2 - t1) / (double)SYSCLOCK_ARM11) / 1e6, (unsigned)sum);
		}
		free(bufs[0]);
		if (bufs[1])
			linearFree(bufs[1]);
	}
	log_printf("switches: csnd=%d nonet=%d nosound=%d noirrst=%d noaa=%d nolcd=%d nocovers=%d\n",
		sw_csnd, sw_nonet, sw_nosound, sw_noirrst, sw_noaa, sw_nolcd, sw_nocovers);

	// defaults.ini (romfs) supplies the settings' starting values: before config.
	romfsInit();
	boot_mark("romfs");
	presets_update_init("romfs:/presets", PSX3D_DIR);   // finishes or undoes a cut-off install
	if (sw_presets_url[0]) presets_update_dev(sw_presets_url, true);
	game_presets_init("romfs:/presets", PSX3D_DIR "/presets");
	bool had_config = config_present();   // before the PSX3D import makes one: a truly first start shows the welcome
	config_psx3d_imported = config_import_psx3d();
	config_load(); bb_whatsnew_boot(had_config);
	bb_welcome_boot(had_config, config_psx3d_imported && g_cfg.games_dir[0] ? PSX_IMPORT_GAMES : 0);
	boot_mark("presets + config");
    breadbox_import_init(); /* Repair parked primaries before any pause/game path. */
    playlog_init();   // long-lived tables at the bottom of the heap, not mid-heap after a game
    char index_root[PATH_LEN];
    if (bb_index_discover(g_cfg.games_dir, index_root, sizeof(index_root))) {
        // The saved folder, its common root, or (first run) a common root are the choice. Any other root is a
        // fallback for this session only: a card that failed to read for a moment must not lose a custom folder.
        bb_games_dir_found(index_root);
        bb_index_start(g_cfg.games_dir, false);
    } else if (g_cfg.games_dir[0]) {
        // The chosen folder cannot be read and no other root was found: it is kept (a moved folder or a card
        // error may be temporary); the start shows the first-run screen until it works.
        bb_games_dir_unreadable();
    }
	boot_mark("index started");
	if (!video_init()) {
		log_printf("video_init failed\n");
        breadbox_import_shutdown();
        bb_index_shutdown();
        exit_terminal();   // the index worker, the log writer and GSP's thread (gfxInitDefault ran) may still run
		return 1;
	}
	ui_init();
	libui_init(); bb_whatsnew_prepare();
	icons_preload();
	boot_mark("icons ready");
	boot_mark("video + ui");
	home_dawn();   // a picture at once: the rest of the start (covers, sound, systems, the room) happens behind it
	if (!sw_nocovers)
		covers_init();
	if (!sw_nosound)
		dsp_firmware_ensure();   // first start: copy it from the HOME Menu
	boot_mark("covers + dsp");
	uisound_init();
	boot_mark("sound");
	bb_system_init();
	boot_mark("systems");
	dev_init();
	boot_mark("dev");
    const char *autostart=bb_system_autostart();
    if(autostart)start_game(autostart,NULL,NULL);

	// BreadBox opens to the main menu (home.c). Its Library is the game library
	// (needs a games folder); Settings > Games folder opens the folder browser,
	// which is where a games folder gets picked.
	// first run: look for the games instead of opening the folder browser
    bool use_library = g_cfg.games_dir[0] != 0; // Home opens while the index worker scans.
#define MAIN_STOP() (!aptMainLoop() || !gspHasGpuRight())
	bool at_home = true;
	for (;;) {
		if (MAIN_STOP()) break;
		dev_tick(false);
		if (dev_exiting())
			break;
		char remote_save[PATH_LEN], remote_title[PATH_LEN];
		if (dev_next_game(path, remote_save, remote_title)) {
			start_game(path, remote_save[0] ? remote_save : NULL, remote_title);
			dev_local_game();   // the library is local again (a batch reapplies its own)
			if (aptShouldClose())
				break;
			continue;
		}
		bool play;
		const game_t *picked = NULL;
        if (sw_game[0] && g_cfg.games_dir[0]) use_library = library_load(g_cfg.games_dir, false);
        if (MAIN_STOP()) break;
		if (use_library && sw_game[0]) {
			// test: straight into a game ("_" stands for a space in the switch)
			char want[64];
			snprintf(want, sizeof(want), "%s", sw_game);
			for (char *c = want; *c; c++)
				if (*c == '_') *c = ' ';
			sw_game[0] = 0;   // once
			for (int i = 0; i < library_count() && !picked; i++)
				if (strcasestr(library_get(i)->title, want))
					picked = library_get(i);
			if (picked && library_launch_path(picked, path, sizeof(path))) {
				log_printf("test: starting %s\n", picked->title);
				dev_local_game();
				if (!start_game(path, picked->cover_key, picked->title))
					if(!dev_remote_game())show_error(launch_error);
				if (aptShouldClose())
					break;
				continue;
			}
			if (picked)
				show_error("Could not start the game\n" APP_NAME " could not write the list of this game's discs. Is the SD card full or locked?");
			else
				log_printf("test: no game matches %s\n", want);
			picked = NULL;
		}
		if (at_home) {

			home_launch_t hl;
			int h = home_run(use_library, path, sizeof(path), &hl);
            if(bb_update_committed())break;
			if (MAIN_STOP()) break;
			if (h == HOME_QUIT)
				break;
			if (h == HOME_AGAIN)
				continue;
			if (h == HOME_FOLDERS) {
				// Settings > Games folder: the browser, then back to the main menu
				play = browser_run(path, sizeof(path));
				if (MAIN_STOP()) break;
				use_library = g_cfg.games_dir[0] && library_load(g_cfg.games_dir, false);
				if (MAIN_STOP()) break;
				if (dev_transition() || !play)
					continue;
				at_home = false;
				dev_local_game();
				if (!start_game(path, NULL, NULL))
					if(!dev_remote_game())show_error(launch_error);
				if (aptShouldClose())
					break;
				continue;
			}
			at_home = false;   // after a game: the library
			if (h == HOME_LIBRARY) {
                use_library = g_cfg.games_dir[0] && library_load(g_cfg.games_dir, false);
                if (MAIN_STOP()) break;
				continue;
            }
			dev_local_game();
			bb_system_resume(path, hl.resume);
			if (!start_game(path, hl.save[0] ? hl.save : NULL, hl.title[0] ? hl.title : NULL))
				if(!dev_remote_game())show_error(launch_error);
			bb_system_resume(path, false);
			if (aptShouldClose())
				break;
			continue;
		}
        // Completion can follow a cancelled progress screen or a system rescan.
        if (g_cfg.games_dir[0]) {
            bb_index_poll(true);
            use_library = bb_system_total()>0;
        }
		if (use_library
            || (g_cfg.games_dir[0] && !bb_games_dir_gone)
        ) {
			int r = breadbox_systems_run(path, sizeof(path));
			if (MAIN_STOP()) break;
			if (r == 3) continue;
			if (r == 5) { use_library = false; continue; }   // a rescan found nothing: the empty-library screen
			if (r == 4) { at_home = true; continue; }
			if (r == 0) {
				if (!aptShouldClose()) { at_home = true; continue; }   // Start closes the library screen: back in the room, the app stays
				break;
			}
			if (r == 2) {
				play = browser_run(path, sizeof(path));
				if (MAIN_STOP()) break;
				if (dev_transition()) continue;
				if (!play) {
					// back from the browser: the games folder may have changed
					use_library = g_cfg.games_dir[0] && library_load(g_cfg.games_dir, false);
					if (MAIN_STOP()) break;
					continue;
				}
			} else {
				play = true;
				picked = NULL;   // the bookcase hands over a path
			}
		} else {
			// no games yet: how to add them (B main menu, Y folder browser)
			int e = home_empty_library_run();
			if (MAIN_STOP()) break;
			if (e == 0)
				break;
			if (e == 1) { at_home = true; continue; }
			if (e == 3) {
				use_library = g_cfg.games_dir[0] && library_load(g_cfg.games_dir, false);
				if (MAIN_STOP()) break;
				continue;
			}
			play = browser_run(path, sizeof(path));
			if (MAIN_STOP()) break;
			if (dev_transition()) continue;
			if (!play) {
				use_library = g_cfg.games_dir[0] && library_load(g_cfg.games_dir, false);
				if (MAIN_STOP()) break;
				continue;
			}
		}
		// library games save under disc 1's name, same as starting that
		// disc from the folder browser
		dev_local_game();
		if (!start_game(path, picked ? picked->cover_key : NULL, picked ? picked->title : NULL))
			if(!dev_remote_game())show_error(launch_error);
		if (aptShouldClose())
			break;
	}

    if(bb_update_committed()) {
        /* AM replaced this title. No scene leave, drawing, config, font, sound playback,
         * cert, or ROMFS reads are allowed here. All workers were drained first.
         * Every step is bounded and journaled (updates/phase.txt). At the first one that
         * does not finish in time no further step runs: its thread and everything it can
         * reach stay as they are, the relaunch turns into a return to HOME and
         * exit_terminal ends the process. */
        bool clean = bb_update_shutdown_timeout(BB_UPDATE_JOIN_NS);
        bb_update_record(BB_UP_INSTALLER_JOIN, clean);
        if (clean) {
            clean = covers_network_exit(BB_UPDATE_JOIN_NS);
            bb_update_record(BB_UP_COVERS_EXIT, clean);
        }
        if (clean) {
            clean = dev_shutdown_timeout(BB_UPDATE_JOIN_NS);
            bb_update_record(BB_UP_DEVNET_EXIT, clean);
        }
        if (clean) {
            /* The NDSP thread (or CSND) must be gone before main returns; a reader
             * still running keeps ROMFS mounted. */
            clean = uisound_stop_reads(BB_UPDATE_JOIN_NS) && exit_step(&exit_audio, BB_UPDATE_JOIN_NS);
            bb_update_record(BB_UP_AUDIO_EXIT, clean);
        }
        if (clean) {
            romfsExit();
            bb_update_record(BB_UP_ROMFS_EXIT, 1);
        }
        bool relaunch = bb_update_handoff(clean); /* consumed by aptExit */
        if (clean) {
            /* After the handoff's GPU check: the GSP event thread, like a normal quit's. */
            clean = exit_step(&exit_gpu, BB_UPDATE_JOIN_NS);
            bb_update_record(BB_UP_GPU_EXIT, clean);
            if (!clean && relaunch) {
                aptClearChainloader();
                bb_update_record(BB_UP_HOME_RETURN, 0);
            }
        }
        if (!clean)
            exit_terminal();
        return 0;
    }
    if (!bb_exit_abandoned()) bb_update_shutdown();
	// Close in the HOME Menu leaves the GPU with it (video_exit) and skips
	// the restore hook that reopens the log.
	if (!gspHasGpuRight())
		log_resume();
	log_printf("exit: %s\n", aptShouldClose() ? "closed by the system" : "quit");
	// Remote UI captures may have enabled curl on the control server's SOC.
	// Close its connections before dev_shutdown releases that shared session.
    if (!bb_exit_abandoned()) {
        breadbox_import_shutdown();
        /* Importer and cover retirement drain first, while the index worker can still run it
         * (its retry may start the worker again); after the close no worker starts, so the
         * worker this shutdown waits for is the last one. */
        if(!covers_stop_wait_apt())bb_exit_abandon();
        if(!bb_exit_abandoned())bb_index_shutdown();
        if(!bb_exit_abandoned())bb_index_close();
        /* The shutdown waits 45 ms; a worker in a long job must still end before main returns. */
        for (u64 t0 = osGetTime(); !bb_exit_abandoned() && !bb_index_shutdown_done(); svcSleepThread(1000000LL))
            if (osGetTime() - t0 > 3000) {
                log_printf("exit: index worker did not stop\n");
                bb_exit_abandon();
                break;
            }
    }
    bb_platform_saves(BB_SAVES_EXIT);   /* waits for the last game's saves, then the player (or the close) decides */
    if (bb_exit_abandoned()) {
        /* The index worker or a closed game's core still runs: what is left to do is
         * saving; everything else could free what that thread uses. */
        config_save();
        exit_terminal();
        return 0;
    }
	dev_shutdown();
	// each step is logged: a hang at exit shows up as the last line
	log_printf("exit: config\n");
	config_save();
	log_printf("exit: sound\n");
	uisound_exit();   // also with nosound: uisound_init started NDSP all the same
	log_printf("exit: covers\n");
	if (!sw_nocovers && !covers_exit()) {
		exit_terminal();   // a cover worker still runs; covers_exit freed nothing
		return 0;
	}
	log_printf("exit: ui\n");
	libui_exit();
	romfsExit();
	bb_system_deinit();
	bb_whatsnew_exit(); ui_exit();
	log_printf("exit: video\n");
	video_exit();
	if (!sw_noirrst)
		irrstExit();
	cfguExit();
	log_printf("exit: done\n");
	log_close();
	return 0;
}

#include "components/main.inc"
