/* Console budget checks are outside the portable registry/session controller. */
#include <malloc.h>
#include <reent.h>
#include "bb_arena.h"
#include "../../build/systems/arena-size.h"
#include "icons.h"
#include "app_log.h"
#include "app_config.h"
#include "last_picture.h"
#include "bb_platform.h"
#include "platform_video.h"
#include "ui.h"
#include "covers.h"
#include "uisound.h"
#include "playlog.h"
#include "app_main.h"
#include "bb_system.h"
#include "icons.h"
#include "scene.h"
#include "bb_index.h"
#include "shelf.h"
#include "room.h"
#include "bb_memprobe.h"
#include "bb_save.h"
extern char *fake_heap_end;
uint32_t bb_heap_available(void)
{
 return bb_heap_bytes((unsigned)mallinfo().fordblks,(uintptr_t)_sbrk_r(_impure_ptr,0),(uintptr_t)fake_heap_end);
}
unsigned bb_platform_model(void) {return g_new3ds?BB_NEW:BB_OLD;}
bool bb_platform_budget(const bb_system_t *system)
{
 unsigned model=g_new3ds?BB_NEW:BB_OLD;
 const bb_budget *b=&system->budget[model];
 if(!(system->models & (1u<<model)) || b->keep_kib)return false;
 /* after bb_platform_release_menu settled the queue: a final write that failed only now
  * (bb_platform_saves asked before) stops this start; the shell asks next time round */
 if(bb_save_final_failed()) {log_printf("save: the last game's battery is not written: no start\n");return false;}
 unsigned heap=bb_heap_available();
 return b->heap_kib<=heap/1024 &&
        b->linear_kib<=linearSpaceFree()/1024 && b->exec_kib<=BB_EXEC_BYTES/1024;
}

/* The last game's saves, one choke point (bb_save_resolve) for the shell: before any game starts
 * and at exit it waits for everything queued after the last game, then, if its final battery
 * write failed and was kept, the player decides; in a menu it only looks (asked once per
 * failure; "Later" leaves it for the next game start or the exit). Letting the progress go
 * needs a second yes; a system close (HOME > Close, power) gets one more try instead.
 * BB_SAVES_CLOSE: the app is closing: the caller stops at once, no GPU work, no game. */
static bool closing(void) { return aptShouldClose() || !gspHasGpuRight(); }
static int ask(void *ctx)
{
 if(bb_save_final_uncaptured()){
  ui_confirm_ok("Save not made","A save or Continue point could not be made. Existing saves are kept.",false);
  return BB_SAVE_ACK;
 }
 int moment=*(int *)ctx;
 const char *back=moment==BB_SAVES_MENU?"B  Later":moment==BB_SAVES_START?"B  Start anyway":"B  Quit anyway";
 if(ui_confirm_choice("Not saved","The save or Continue point could not be written to the SD card. Check the card, then try again.",
    "A  Try again",back,false))
  return BB_SAVE_RETRY;
 if(moment==BB_SAVES_MENU)return BB_SAVE_LATER;
 return ui_confirm_choice("Lose the progress?","The new save or Continue point will be lost.","A  Lose it","B  Back",false)
  ? BB_SAVE_LOSE : BB_SAVE_RETRY;   /* Back: one more try, then asked again */
}
int bb_platform_saves(int moment)
{
 static bool asked;
 if(moment==BB_SAVES_MENU) {
  if(!bb_save_final_failed()) {asked=false;return closing()?BB_SAVES_CLOSE:BB_SAVES_GO;}
  if(asked)return BB_SAVES_GO;
  asked=true;
 }
 bool go=bb_save_resolve(moment!=BB_SAVES_MENU,closing,ask,&moment);
 /* exit: everything queued has settled (a play-log write in flight has failed or not by now);
  * what failed gets one more try, waited for. Not more: the card may be gone */
 if(moment==BB_SAVES_EXIT){playlog_flush();bb_save_settle();}
 return !go?BB_SAVES_CLOSE:moment==BB_SAVES_MENU?BB_SAVES_ASKED:BB_SAVES_GO;
}

/* A GPU copy whose end is really waited for (video.c's thumbnail of the game's picture): through
 * citro3d's own queue, waited for at its frame boundary. C3D_SyncTextureCopy waits for a PPF
 * event, and one an earlier transfer left latched (the frame presented just before) let it
 * return while the copy still ran: the picture's first rows were read before they came (the
 * Continue picture's noisy top strip after a quit, Old 3DS). Outside video.c: its pinned code. */
void bb_sync_texture_copy(u32 *in, u32 indim, u32 *out, u32 outdim, u32 size, u32 flags)
{
 C3D_FrameBegin(0);
 GX_TextureCopy(in,indim,out,outdim,size,flags);
 C3D_FrameEnd(0);
 video_gpu_idle();
}

void bb_platform_release_menu(void)
{
 bb_save_settle();   /* the last game's files written after it (bb_later): on the card first */
 bb_last_picture_release();
 /* Drain importer cleanup before stopping its worker. */
 covers_stop_wait();
 bb_index_shutdown();
 video_gpu_idle();covers_release_gpu();music_unload();
 icons_release();
 shelf_release();room_scene_release();room_hands_release();
 if(home_release_cache())room_scene_tv_maps_release();
 bb_mem_probe("menu released, system starting",NULL);
}
void bb_platform_restore_menu(void)
{
 room_pace_return_start();
 if(!aptShouldClose()) {
  if(gspHasGpuRight())music_reload();
  if(gspHasGpuRight())icons_preload();
  /* the room's 400x240 target before the bookshelf's spine atlases take the
   * VRAM (after a game the library opens first): the order of a fresh start */
  if(gspHasGpuRight())room_scene_open();
  bb_index_start(g_cfg.games_dir,false);
  shelf_restore_request();
 }
}

bool bb_platform_claim(const bb_system_t *s)
{
 return bb_exec_claim(s);
}
void bb_platform_drop(void)
{
 bb_exec_release();
 bb_mem_probe("system ended",NULL);
}

int bb_platform_count(const bb_system_t *system)
{
 return bb_index_count(system);
}
