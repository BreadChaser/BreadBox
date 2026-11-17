#include "ht_trace.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app_policy.h"
#include "app_config.h"
#include "app_input.h"
#include "bb_platform.h"
#include "bb_registry.h"
#include "bb_index.h"
#include "breadbox_formats.h"
#include "games_dir.h"
#include "devtools.h"
#include "home.h"
#include "whatsnew.h"
#include "shelf.h"
#include "library.h"
#include "libui.h"
#include "platform_video.h"
#include "settings_ui.h"
#include "scene.h"
#include "uisound.h"
#include "ui.h"
#include "ui_pause.h"
#include "ui_format.h"
#include "cold_code.h"
#include "bb_update.h"
#include "dev_switches.h"
static float fade;
static PSX3D_COLD u32 accent(int i,u8 a){return theme_alpha(theme()->accent[i&3],a);}
#include "home_presentation.h"
#define DEFAULT_GAMES_DIR "sdmc:/roms/psx"
static char empty_msg[PATH_LEN / 2 + 128];   // a path (cut at 80) and the keys that help, wrapped by the shared text
static int empty_msg_frames;
// "<what> <end of the folder path><then>": the path keeps its last folder names (whole UTF-8 characters), cut by
// RENDERED width so a long WWWW path and a CJK one both stay two lines (the text wraps at 304 px, from y 170,
// and must end above the button bar); wide glyphs just keep fewer characters
static PSX3D_COLD void empty_say(const char *what, const char *dir, const char *then)
{
	enum { LINE_PX = 304, MESSAGE_PX = 2 * LINE_PX - 120, PATH_PX = LINE_PX - 16 };
	char tail[PATH_LEN];
	for (int chars = 80; ; chars -= chars > 12 ? 4 : 1) {
		ui_path_tail(tail, sizeof(tail), dir, chars);
		snprintf(empty_msg, sizeof(empty_msg), "%s %.200s%s", what, tail, then);
		if (chars <= 1 || (ui_text_width(NULL, UI_FONT_SMALL, empty_msg) <= MESSAGE_PX && ui_text_width(NULL, UI_FONT_SMALL, tail) <= PATH_PX))
			break;
	}
}
#define EMPTY_RETRY ". Rescan tries again; Folders picks another folder."

static void empty_top(void *ud); /* shared presentation */

static const ui_button_t empty_bar[3] = {
	{ KEY_B, "Menu", 0, false }, { KEY_Y, "Folders", UI_ICON_FOLDER, false }, { KEY_SELECT, "Rescan", UI_ICON_RESCAN, false },
};

static void empty_bottom(void *ud); /* shared presentation */

PSX3D_COLD int home_empty_library_run(void)
{
 if(!theme_is_room())shelf_scene_prepare();

	fade = 1;
	empty_msg_frames = 0;
	if (g_cfg.games_dir[0]) {   // a folder was chosen but holds no games: say so, don't just look unchanged
		if (bb_games_dir_gone) {   // the saved choice is kept: the card may only be unreadable for now
			empty_say("Can't read", g_cfg.games_dir, EMPTY_RETRY);
			empty_msg_frames = 1 << 30;   // stays until the folder reads or another is chosen
		} else
		{
		empty_say("No games found in", g_cfg.games_dir, ".");
		empty_msg_frames = 300;
		}
	}
	music_play(true);
	ui_wants_3d = true;
	int r = 0;
	while (aptMainLoop()) {
 HT("loop");
		if(bb_index_poll(true))shelf_library_changed();
		if(bb_system_total()>0){r=3;break;}
		HT("preload/dev begin");
        if (dev_tick(false)) { r = 3; break; }
		HT("dev done");
        int screen = dev_ui_take(); bb_whatsnew_room(screen, false, NULL);
		if (screen == DEV_UI_SETTINGS) { settings_ui_run(false, empty_top); continue; }
		input_poll(); HT("input");
		u32 k = ui_header_back(ui_bar_input(empty_bar, 3, input_keys_down()));
        k=ui_swap_keys(k,UI_SWAP_MENU);
		if (k & KEY_B) { sfx_play(SFX_BACK); r = 1; break; }
		if (k & KEY_Y) { sfx_play(SFX_SELECT); r = 2; break; }
		if (k & KEY_X) { sfx_play(SFX_SELECT); settings_ui_run(false, empty_top); continue; }
		if (k & KEY_SELECT) {
			// the start's cheap look: the saved folder, else the usual roots and folders named like a system
			char found[PATH_LEN];
			sfx_play(SFX_SELECT);
			bb_games_dir_retry();   // the saved folder reads again: that one
			if (!bb_index_discover(g_cfg.games_dir, found, sizeof(found))) {
				if (g_cfg.games_dir[0]) empty_say("Can't read", g_cfg.games_dir, EMPTY_RETRY);
				else snprintf(empty_msg, sizeof(empty_msg), "No games folder found. Folders picks one.");
				empty_msg_frames = g_cfg.games_dir[0] ? 1 << 30 : 240;
			} else {
				bb_games_dir_found(found);   // the saved folder, or a fallback for this session only
				if (library_load(g_cfg.games_dir, true)) {
					r = 3;
					break;
				}
				if (!aptMainLoop()) { r = 0; break; }
				if(bb_index_status()==BB_INDEX_SCANNING)empty_say("Finding games in",g_cfg.games_dir,"...");
				else empty_say("Still no games in",g_cfg.games_dir,".");
				empty_msg_frames = 180;
			}
		}
		music_update(); HT("music");

		if (fade > 0) fade = fmaxf(0, fade - 3.0f / 60.0f);
		if (empty_msg_frames > 0) empty_msg_frames--;
		// HOME > Close can end a library_load above (it polls aptMainLoop): nothing may be drawn after it
		if (!aptMainLoop() || !gspHasGpuRight()) { r = 0; break; }
		ui_begin();
		video_present_vsync(false, empty_top, empty_bottom, NULL);
	}
	music_play(false);
	ui_wants_3d = false;
	return r;
}


#include "components/home_empty.inc"
