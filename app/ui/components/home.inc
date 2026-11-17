// Shared home presentation; content/state are provided by its controller.
// The room's top screen has no wordmark and no version (the version is in Share and Credits):
// only the shared information footer and the first-start line.
static void room_top_notices(void)
{
	if (!have_games) {
		float y = 0;
		C2D_DrawRectSolid(0, y, 0.6f, UI_TOP_SCREEN_WIDTH, 2, accent(1, UI_ALPHA_OPAQUE));
	}
	home_slow_banner();
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
		C2D_DrawRectSolid(0, 0, 0.95f, UI_TOP_SCREEN_WIDTH, UI_SCREEN_HEIGHT, theme_alpha(theme()->shadow, (u8)(255 * fminf(fade, 1))));
	shelf_scene_intro(room_intro);   // the cold start: the room comes up out of the dark, the lamp first
	room_scene_overlay_end();
}

static void draw_top(void *ud)
{
	(void)ud;
 if(theme_is_room())room_top();
 else {bb_home_picture picture=home_list_picture();if(no_title)list_look_backdrop(false);else list_look_home_top(&picture);room_top_notices();bb_update_draw_notice();}

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
	const bb_home_place *focused=bb_home_find(room_quit_on ? E_QUIT : bb_home_place_at(place)->action);
 shelf_card c = {.title=focused ? focused->card_title : ""};
	bool postcard = false;
	(void)here;
	if (room_quit_on && room.mode == ROOM_HOME) {   // the bar's last tab: Quit

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

		break;
	case ROOM_SETTINGS:

		break;
	case ROOM_SHARE:

		c.row[c.nrows++] = (shelf_card_row){ "1  FBI on their 3DS" };
		c.row[c.nrows++] = (shelf_card_row){ "2  Remote Install" };
		c.row[c.nrows++] = (shelf_card_row){ "3  Scan this card" };
		break;
	case ROOM_TOGETHER:
		if (!HOME_PLAY_TOGETHER) {   // the tab is real, the game is coming: no actions
			c.hint = "Coming in a future update.";
			break;
		}
		c.list = true;
		c.row[c.nrows++] = (shelf_card_row){ "Host a game", NULL, here && sel_id == E_HOST, false };
		c.row[c.nrows++] = (shelf_card_row){ "Join a game", NULL, here && sel_id == E_JOIN, false };
		break;
 default:break;
	}
	if (!postcard) {
		place_rows(&c, place);
		shelf_scene_card(&c);
	}
	int n;
	const ui_button_t *act = room_actions(&n);
	if (act) {
		if(theme_is_room())ui_bar_draw(act,n);else list_look_footer(act,n);
	} else {
		int tabs[ROOM_PLACES], nt = room_tabs(tabs), lit = 0;


		ui_button_t b[ROOM_PLACES + 1];
		for (int i = 0; i < nt; i++) {
			b[i] = (ui_button_t){ 0, bb_home_place_at(tabs[i])->name, bb_home_place_at(tabs[i])->icon, false };
			if (tabs[i] == place) lit = i;
		}
		const bb_home_place *quit=bb_home_find(E_QUIT);
  b[nt] = (ui_button_t){ 0, quit->name, quit->icon, false };   // the last tab, after Share
		if (room_quit_on && room.mode == ROOM_HOME) lit = nt;
		// the chosen place's tab carries the A button's glyph: A enters it (or quits); not for a place where A does
		// nothing (an empty TV, Play together still to come). The other tabs are places, not buttons.
		bool a_acts = lit == nt || !((tabs[lit] == ROOM_CONTINUE && !cont_ok) || (tabs[lit] == ROOM_TOGETHER && !HOME_PLAY_TOGETHER));
		if (a_acts) b[lit].key = KEY_A;
		ui_tabs_icons(b, nt + 1, lit);
	}
	if (fade > 0.001f)   // fixed: fade from/to black
		C2D_DrawRectSolid(0, 0, 0.95f, UI_BOTTOM_SCREEN_WIDTH, UI_SCREEN_HEIGHT, theme_alpha(theme()->shadow, (u8)(255 * fminf(fade, 1))));
}

static void draw_bottom(void *ud)
{
	(void)ud;
 if(!theme_is_room() && (room.mode==ROOM_HOME || room.mode==ROOM_BACK)){bb_home_picture picture=home_list_picture();list_look_home_bottom(&picture);}
 else room_bottom();
#if defined(BB_UI_FIXTURES) || defined(BB_UI_TRACE_HOST)
 bb_home_picture trace=home_list_picture();int n;const ui_button_t *a=room_actions(&n);ui_trace_home(&trace,a,n);
#endif
	ui_motion_shade(UI_MOTION_DIM_ALPHA*(1-ui_motion_ease(room_intro)),UI_BOTTOM_SCREEN_WIDTH);

}






PSX3D_COLD void home_slow_banner(void)
{
	const char *m = slow_mode_notice(false);
	if (!m)
		return;
	ui_notice(slow_mode_notice(true), 60);
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
	home_title("Play together", "Coming soon", 1);
	home_top_common();
 bb_update_draw_notice();
}
