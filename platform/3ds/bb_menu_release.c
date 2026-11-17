/* A failed drain leaves the menu and all background-owned data intact. */
#include "cold_code.h"
#include "covers.h"
#include "bb_system.h"
#include "bb_save.h"
#include "bb_index.h"
#include "last_picture.h"
#include "platform_video.h"
#include "uisound.h"
#include "icons.h"
#include "shelf.h"
#include "room.h"
#include "bb_memprobe.h"
#include "libui.h"
#include "whatsnew.h"

PSX3D_COLD bool bb_platform_release_menu(void)
{
 if(!covers_stop_wait()){covers_resume();return false;}
 bb_save_settle();   /* the last game's files written after it (bb_later): on the card first */
 bb_last_picture_release();
 /* Drain importer cleanup before stopping its worker. */
 bb_index_shutdown();
 video_gpu_idle();covers_release_gpu();music_unload();
 icons_release();
 libui_release_menu();bb_whatsnew_exit();
 shelf_release();room_scene_release();room_hands_release();
 if(home_release_cache())room_scene_tv_maps_release();
 bb_mem_probe("menu released, system starting",NULL);
 return true;
}
