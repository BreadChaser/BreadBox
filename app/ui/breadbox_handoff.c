/* Cold transition diagnostics, and the PS1 teardown's GPU-right guard. BreadBox only; no
 * per-frame or core edits (the linker sends these calls here: build/3ds/Makefile --wrap). */
#include "app_policy.h"
#include "app_log.h"
#include <stdbool.h>
bool gspHasGpuRight(void);
void __real_retro_init(void);
void __wrap_retro_init(void) {
 log_printf("handoff: PS1 retro_init enter\n");log_soon();
 __real_retro_init();
 log_printf("handoff: PS1 retro_init leave\n");log_soon();
}
/* Closed from the HOME Menu (no GPU right, e.g. HOME > Close at the memory card question): the
 * PS1 core's and renderer's teardown would submit and wait for GPU frames forever. As at the
 * game's end (emu.c), they stay as they are; the system ends the app. */
static bool closed(const char *what)
{
 if(gspHasGpuRight())return false;
 log_printf("handoff: PS1 %s skipped: closing (no GPU right)\n",what);log_soon();
 return true;
}
void __real_retro_unload_game(void);
void __wrap_retro_unload_game(void) {if(!closed("retro_unload_game"))__real_retro_unload_game();}
void __real_video_game_end(void);
void __wrap_video_game_end(void) {if(!closed("video_game_end"))__real_video_game_end();}
void __real_retro_deinit(void);
void __wrap_retro_deinit(void) {
 if(closed("retro_deinit"))return;
 log_printf("handoff: PS1 retro_deinit enter\n");log_soon();
 __real_retro_deinit();
 log_printf("handoff: PS1 retro_deinit leave\n");log_soon();
}
