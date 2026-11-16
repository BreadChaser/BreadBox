// Shared home presentation; content/state are provided by its controller.
// The room's top screen has no wordmark and no version (the version is in Share and Credits):
// only the shared information footer and the first-start line.
static void room_top_notices(void)
{
	if (!have_games) {
		float y = 0;
		C2D_DrawRectSolid(0, y, 0.6f, UI_TOP_SCREEN_WIDTH, 2, accent(1, UI_ALPHA_OPAQUE));
	}
	slow_banner();
}

// The room in 3D and nothing written over it (owner 2026-11-05: no hover labels).
// The bottom card names the focused place and says what it holds; the room lights the place.
static void room_top(void)
{
	float picker_t = 0;
	const hands_clip *picker = room.mode == ROOM_AT && room.place == ROOM_SETTINGS ? room_picker(&picker_t) : NULL;
	static hands_track picker_track;   // in, idle and out blend into each other
	float now = (float)fmod((double)svcGetSystemTick() / SYSCLOCK_ARM11, 3600);
	hands_track_set(&picker_track, picker, picker_t, now);
	room_hands_use_track(&picker_track, now);
	if (room_intro == 0) { shelf_scene_dawn(true); return; }
	room_scene_draw(&room, picker, picker_t);
	room_scene_overlay_begin();
	room_top_notices();
 bb_update_draw_notice();
	if (room_loading)
		ui_notice("Opening your collection", 60);
	if (fade > 0.001f)   // fixed: fade from/to black
		C2D_DrawRectSolid(0, 0, UI_Z_COVER, UI_TOP_SCREEN_WIDTH, UI_SCREEN_HEIGHT, theme_alpha(theme()->shadow, (u8)(255 * fminf(fade, 1))));
	shelf_scene_intro(room_intro);   // the cold start: the room comes up out of the dark, the lamp first
	room_scene_overlay_end();
}

static void draw_top(void *ud)
{
	(void)ud;
	room_top();

}




// Room themes: the desk and the index card of the focused place (or the place you
// are at), the places as tabs, or the place's own actions (UI mock R9/R10).
static void room_bottom(void)
{
	if (room_intro == 0) { shelf_scene_dawn(false); return; }
	int place = room.mode == ROOM_HOME || room.mode == ROOM_BACK ? room.focus : room.place;
	bool here = room.mode == ROOM_AT || room.mode == ROOM_GO;
	// The place's name and what it holds at a glance (owner 2026-11-12: the games per system, the version and
	// updates, today's play): place_rows, nothing else (owner 2026-11-05): no kicker naming the furniture, no "A: ..."
	// hint the bar and the room already make plain. Kept: the Share steps, Play together's "coming" note.
	shelf_card c = { 0 };
	bool postcard = false;
	(void)here;
	if (room_quit_on && room.mode == ROOM_HOME) {   // the bar's last tab: Quit
		c.title = "Quit " APP_NAME;
		place = ROOM_QUIT_PLACE;
	}
	switch (place) {
	case ROOM_QUIT_PLACE:
		break;
	case ROOM_CONTINUE: {   // the Continue point as a postcard on the desk (owner 2026-11-12, mock A)
		char clean[PATH_LEN];
		ui_clean_title(cont_ok ? cont_title() : "", clean, sizeof(clean));
		pc_continue pc = { cont_tex_ok ? &cont_tex : card_tex_ok ? &card_tex : NULL, BB_PICTURE_W, BB_PICTURE_H, (float)cont_band,
			clean, cont_stamp, cont_saved, NULL, false, !cont_ok };
		pc_continue_card(&pc);
		postcard = true;
		break;
	}
	case ROOM_LIBRARY:
		c.title = "Collection";
		break;
	case ROOM_SETTINGS:
		c.title = "Settings";
		break;
	case ROOM_SHARE:
		c.title = "Share " APP_NAME;
		c.row[c.nrows++] = (shelf_card_row){ "1  FBI on their 3DS" };
		c.row[c.nrows++] = (shelf_card_row){ "2  Remote Install" };
		c.row[c.nrows++] = (shelf_card_row){ "3  Scan this card" };
		break;
	default:
		c.title = "Play together";
		if (!HOME_PLAY_TOGETHER) {   // the tab is real, the game is coming: no actions
			c.hint = "Coming in a future update.";
			break;
		}
		c.list = true;
		c.row[c.nrows++] = (shelf_card_row){ "Host a game", NULL, here && sel_id == E_HOST, false };
		c.row[c.nrows++] = (shelf_card_row){ "Join a game", NULL, here && sel_id == E_JOIN, false };
		break;
	}
	if (!postcard) {
		place_rows(&c, place);
		shelf_scene_card(&c);
	}
	int n;
	const ui_button_t *act = room_actions(&n);
	if (act) {
		ui_bar_draw(act, n);
	} else {
		int tabs[ROOM_PLACES], nt = room_tabs(tabs), lit = 0;
		static const char *const names[ROOM_PLACES] = { "Continue", "Collection", "Settings", "Together", "Share" };
		static const u8 icons[ROOM_PLACES] = { UI_ICON_TV, UI_ICON_BOOKS, UI_ICON_SCOPE, UI_ICON_TOGETHER, UI_ICON_QR };
		ui_button_t b[ROOM_PLACES + 1];
		for (int i = 0; i < nt; i++) {
			b[i] = (ui_button_t){ 0, names[tabs[i]], icons[tabs[i]], false };
			if (tabs[i] == place) lit = i;
		}
		b[nt] = (ui_button_t){ 0, "Quit", UI_ICON_EXIT, false };   // the last tab, after Share
		if (room_quit_on && room.mode == ROOM_HOME) lit = nt;
		// the chosen place's tab carries the A button's glyph: A enters it (or quits); not for a place where A does
		// nothing (an empty TV, Play together still to come). The other tabs are places, not buttons.
		bool a_acts = lit == nt || !((tabs[lit] == ROOM_CONTINUE && !cont_ok) || (tabs[lit] == ROOM_TOGETHER && !HOME_PLAY_TOGETHER));
		if (a_acts) b[lit].key = KEY_A;
		ui_tabs_icons(b, nt + 1, lit);
	}
	if (fade > 0.001f)   // fixed: fade from/to black
		C2D_DrawRectSolid(0, 0, UI_Z_COVER, UI_BOTTOM_SCREEN_WIDTH, UI_SCREEN_HEIGHT, theme_alpha(theme()->shadow, (u8)(255 * fminf(fade, 1))));
}

static void draw_bottom(void *ud)
{
	(void)ud;
	room_bottom();
	ui_motion_shade(UI_MOTION_DIM_ALPHA*(1-ui_motion_ease(room_intro)),UI_BOTTOM_SCREEN_WIDTH);

}






static void slow_banner(void)
{
	const char *m = slow_mode_notice(false);
	if (!m)
		return;
	ui_notice(slow_mode_notice(true), 60);
}

static void big_title(const char *title, const char *line, float ease)
{
	u8 a = (u8)(255 * ease);
	float size = 0.8f, w = text_w(libui_font(), size, title);
	if (w > 380)
		size *= 380 / w;
	float ty = 172 + (1 - ease) * 10, tx = 200 + ui_depth(-0.8f);
	const theme_t *th = theme();
	ui_text_ex(libui_font(), tx + 1, ty + 1, size, theme_alpha(THEME_TITLE_SHADOW(theme()), a * 5 / 8), C2D_AlignCenter, title);
	ui_text_ex(libui_font(), tx, ty, size, theme_alpha(th->game_title, a), C2D_AlignCenter, title);
	ui_text_ex(NULL, tx, ty + 34, UI_FONT_VALUE, theme_alpha(th->game_sub, a), C2D_AlignCenter, line);
}

static void top_common(void)
{
	// small PSX3D wordmark (the slow-mode banner takes its place)
	if (!slow_mode_notice(false)) {
		float w = text_w(libui_font(), UI_FONT_META, APP_WORDMARK_LEFT);
		ui_text_ex(libui_font(), 10, 6, UI_FONT_META, theme()->wordmark, 0, APP_WORDMARK_LEFT);
		ui_text_ex(libui_font(), 10 + w + 0.42f * 6, 6, UI_FONT_META, theme()->wordmark_accent, 0, APP_WORDMARK_RIGHT);
	}
	if (!have_games) {   // first start: one friendly line
		float y = 0;
		C2D_DrawRectSolid(0, y, 0.6f, UI_TOP_SCREEN_WIDTH, 2, accent(1, UI_ALPHA_OPAQUE));
	}
	slow_banner();

}

static void soon_bottom(void *ud)
{
	const theme_t *th = theme();
	scene_draw(true);
	ui_menu_header("Play together", ud);
	ui_text_wrap(16, 52, UI_FONT_BODY, th->help_text, 288, "Coming soon. Playing together with a friend's 3DS arrives in a later version.");
	static const ui_button_t back = { KEY_B, "Back", 0, false };
	ui_bar_draw(&back, 1);
}

static void soon_top(void *ud)
{
	(void)ud;
	libui_draw_backdrop();
	C2D_DrawRectSolid(0, 0, 0.3f, UI_TOP_SCREEN_WIDTH, UI_SCREEN_HEIGHT, theme_alpha(theme()->surface, 70));
	icons_draw(ICON_SLOT_TOP, ICON_TOGETHER, 200 + ui_depth(-1.0f), 90, 120, true, 1, 0.5f);
	big_title("Play together", "Coming soon", 1);
	top_common();
 bb_update_draw_notice();
}

static void empty_top(void *ud)
{
	(void)ud;
	libui_draw_backdrop();
	C2D_DrawRectSolid(0, 0, 0.3f, UI_TOP_SCREEN_WIDTH, UI_SCREEN_HEIGHT, theme_alpha(theme()->surface, 70));
	big_title("No games yet", empty_help(false), 1);
	top_common();
 bb_update_draw_notice();
	if (fade > 0.001f)   // fixed: fade from/to black
		C2D_DrawRectSolid(0, 0, UI_Z_COVER, UI_TOP_SCREEN_WIDTH, UI_SCREEN_HEIGHT, theme_alpha(theme()->shadow, (u8)(255 * fminf(fade, 1))));
}

static void empty_bottom(void *ud)
{
	(void)ud;
	scene_draw(true);
	ui_menu_header("Collection", "Empty");
	static const char *const steps[3] = {
		"Copy games to any folder on the SD card", "Press Rescan to look for them", "Or press Folders to pick their folder"
	};
	// what can be played comes from the registered systems (breadbox_formats.c), never a fixed list
	static char systems[96], types[96];
	if (!systems[0]) {
		bb_formats_systems(systems, sizeof(systems));
		bb_formats_extensions(types, sizeof(types));
	}
	const theme_t *th = theme();
	u32 white = th->text_bright, muted = th->text_muted;
	for (int i = 0; i < 3; i++) {
		float y = 42 + i * 28;
		C2D_DrawCircleSolid(22, y + 8, 0.3f, 9, accent(i, UI_ALPHA_OPAQUE));
		char n[2] = { (char)('1' + i), 0 };
		ui_text_ex(NULL, 22, y + 1, UI_FONT_META, th->surface, C2D_AlignCenter, n);
		ui_text_ex(NULL, 40, y, 0.48f, white, 0, steps[i]);
	}
	ui_text_wrap(UI_WRAP_X, 128, UI_FONT_SMALL, white, UI_WRAP_W, systems);
	ui_text_wrap(UI_WRAP_X, 152, UI_FONT_SMALL, muted, UI_WRAP_W, types);
	if (HOME_PLAY_TOGETHER)
		ui_text_wrap(UI_WRAP_X, 156, UI_FONT_SMALL, muted, UI_WRAP_W, "No games? You can still join a friend: B, then Play together.");
	if (empty_msg_frames > 0)
		ui_text_wrap(UI_HOME_MARGIN, 170, UI_FONT_SMALL, th->message, UI_HOME_TILE_W, empty_msg);   // two lines at most (the path is cut to its end), all above the button bar
	ui_bar_draw(empty_bar, 3);
	if (fade > 0.001f)   // fixed: fade from/to black
		C2D_DrawRectSolid(0, 0, UI_Z_COVER, UI_BOTTOM_SCREEN_WIDTH, UI_SCREEN_HEIGHT, theme_alpha(theme()->shadow, (u8)(255 * fminf(fade, 1))));
}


static float text_w(C2D_Font f, float size, const char *s)
{
	C2D_Text t;
	float w = 0;
	if (!mbuf && !(mbuf = C2D_TextBufNew(256)))
		return 0;
	C2D_TextBufClear(mbuf);
	if (!C2D_TextFontParse(&t, f, mbuf, s))
		return 0;
	C2D_TextGetDimensions(&t, size, size, &w, NULL);
	return w;
}

static void together_at(float x, float y, float w, float h)
{
	group = (tile_t){ -1, K_MID, x, y, w, h };
	have_group = true;
	float bw = (w - 18) / 2;
	tiles[ntiles++] = (tile_t){ E_HOST, K_SUB, x + 6, y + 30, bw, h - 36 };
	tiles[ntiles++] = (tile_t){ E_JOIN, K_SUB, x + 12 + bw, y + 30, bw, h - 36 };
}

static void layout(void)
{
	ntiles = 0;
	have_group = false;
	if (HOME_PLAY_TOGETHER) {
		if (!have_games) {
			// a friend who just installed PSX3D: joining needs no games
			tiles[ntiles++] = (tile_t){ E_JOIN, K_HERO, UI_HOME_MARGIN, UI_HOME_MARGIN, UI_HOME_TILE_W, UI_HOME_HERO_H };
			tiles[ntiles++] = (tile_t){ E_LIBRARY, K_MID, UI_HOME_MARGIN, UI_HOME_MID_Y, UI_HOME_TILE_W, UI_HOME_MID_H };
		} else if (cont_ok) {
			tiles[ntiles++] = (tile_t){ E_CONTINUE, K_HERO, UI_HOME_MARGIN, UI_HOME_MARGIN, UI_HOME_TILE_W, UI_HOME_HERO_H };
			tiles[ntiles++] = (tile_t){ E_LIBRARY, K_MID, UI_HOME_MARGIN, UI_HOME_MID_Y, UI_HOME_HALF_W, UI_HOME_MID_H };
			together_at(UI_HOME_COL2_X, UI_HOME_MID_Y, UI_HOME_HALF_W, UI_HOME_MID_H);
		} else {
			tiles[ntiles++] = (tile_t){ E_LIBRARY, K_HERO, UI_HOME_MARGIN, UI_HOME_MARGIN, UI_HOME_TILE_W, UI_HOME_HERO_H };
			together_at(UI_HOME_MARGIN, UI_HOME_MID_Y, UI_HOME_TILE_W, UI_HOME_MID_H);
		}
		tiles[ntiles++] = (tile_t){ E_SHARE, K_SMALL, UI_HOME_MARGIN, UI_HOME_SMALL_Y, UI_HOME_HALF_W, UI_HOME_SMALL_H };
		tiles[ntiles++] = (tile_t){ E_SETTINGS, K_SMALL, UI_HOME_COL2_X, UI_HOME_SMALL_Y, UI_HOME_HALF_W, UI_HOME_SMALL_H };
	} else if (have_games && cont_ok) {
		tiles[ntiles++] = (tile_t){ E_CONTINUE, K_HERO, UI_HOME_MARGIN, UI_HOME_MARGIN, UI_HOME_TILE_W, UI_HOME_HERO_H };
		tiles[ntiles++] = (tile_t){ E_LIBRARY, K_MID, UI_HOME_MARGIN, UI_HOME_MID_Y, UI_HOME_TILE_W, UI_HOME_MID_H };
		tiles[ntiles++] = (tile_t){ E_SHARE, K_SMALL, UI_HOME_MARGIN, UI_HOME_SMALL_Y, UI_HOME_HALF_W, UI_HOME_SMALL_H };
		tiles[ntiles++] = (tile_t){ E_SETTINGS, K_SMALL, UI_HOME_COL2_X, UI_HOME_SMALL_Y, UI_HOME_HALF_W, UI_HOME_SMALL_H };
	} else {
		tiles[ntiles++] = (tile_t){ E_LIBRARY, K_HERO, UI_HOME_MARGIN, UI_HOME_MARGIN, UI_HOME_TILE_W, 136 };
		tiles[ntiles++] = (tile_t){ E_SHARE, K_MID, UI_HOME_MARGIN, 152, UI_HOME_HALF_W, 80 };
		tiles[ntiles++] = (tile_t){ E_SETTINGS, K_MID, UI_HOME_COL2_X, 152, UI_HOME_HALF_W, 80 };
	}
	// the room's Play together tab (coming soon): a place of the room without the networking; the other themes do not show it
	if (!HOME_PLAY_TOGETHER)
		tiles[ntiles++] = (tile_t){ E_TOGETHER, K_SMALL, UI_HOME_MARGIN, UI_HOME_SMALL_Y, UI_HOME_HALF_W, UI_HOME_SMALL_H };
}




