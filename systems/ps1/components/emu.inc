// Shared emu presentation; content/state are provided by its controller.
PSX3D_COLD_FN(alert_draw_top) static void alert_draw_top(void)
{
	float dx = ui_depth(-0.7f);
	C2D_DrawRectSolid(0, 0, UI_Z_NOTICE, UI_TOP_SCREEN_WIDTH, 30, g_game_palette.alert_bg);   // below the text depth (0.7)
	C2D_DrawRectSolid(0, 30, 0.67f, UI_TOP_SCREEN_WIDTH, 2, g_game_palette.alert_rule);
	ui_text_ex(NULL, 200 + dx, 8, UI_FONT_VALUE, g_game_palette.alert_text, C2D_AlignCenter,
		ui_fit(osd_q[0].text, UI_FONT_VALUE, 390));
}

static void menu_game(void *unused)
{
	(void)unused;
	video_draw_game();
}
static void menu_draw_top(void *ud)
{
	menu_t *m = ud;
	float box[4];
	video_game_box(box);
	bb_pause_present_top(card_title(), box, menu_game, &m->slots);   // the shared pause top (bb_pause.h)
}

static void menu_draw_bottom(void *ud)
{
	menu_t *m = ud;
	if (have_disk && disk.get_num_images)
		snprintf(m->disc, sizeof(m->disc), "Disc %u of %u", menu_disc + 1, disk.get_num_images());
	ui_button_t bar[UI_BAR_MAX];
	int nbar = menu_bar(m, bar);
	bb_pause_present_card(&m->fl, m->nitems, m->sel, menu_card_row, m, card_title(),
		svcGetSystemTick() < m->note_until ? m->note : NULL, bar, nbar);   // the shared pause card
}

static void game_draw_bottom(void *ud)
{
	(void)ud;
	C2D_DrawRectSolid(0, 0, 0.05f, UI_BOTTOM_SCREEN_WIDTH, UI_SCREEN_HEIGHT, g_game_palette.black);   // in game it's always black

	if (g_cfg.show_fps == 2 || touch_buttons_active() || g_cfg.show_fps == 1)
		touch_buttons_draw();
	statsview_draw();
	if (!has_audio)
		ui_text(4, 186, UI_FONT_META, g_game_palette.warning,
			"No sound: run DSP1 once (it saves the 3DS sound firmware)");
}

static void game_draw_top(void *ud)
{
	(void)ud;
	// Notices stay outside the interactive bottom controls, in every FPS mode.
	if (osd_n && !osd_alert_up())ui_game_notice(osd_q[0].text,osd_q[0].alert);
	if (osd_alert_up())
		alert_draw_top();
	if (!speed_icon)
		return;
	float x = UI_TOP_SCREEN_WIDTH - 46 + ui_depth(-0.6f), y = 10, h = 16, w = 13;
	u8 a = 255;
	u32 c = ((g_game_palette.speed_ink & 0xffffff) | ((u32)a << 24)), sh = g_game_palette.speed_shadow;
	C2D_DrawRectSolid(x - 6, y - 4, 0.8f, 2 * w + 12, h + 8, g_game_palette.speed_bg);
	for (int k = 0; k < 2; k++) {
		float x0 = x + k * w;
		for (int pass = 0; pass < 2; pass++) {
			float o = pass ? 0 : 1;
			u32 col = pass ? c : sh;
			if (speed_icon > 0)
				C2D_DrawTriangle(x0 + o, y + o, col, x0 + o, y + h + o, col, x0 + w + o, y + h / 2 + o, col, 0.81f + pass * 0.01f);
			else
				C2D_DrawTriangle(x0 + w + o, y + o, col, x0 + w + o, y + h + o, col, x0 + o, y + h / 2 + o, col, 0.81f + pass * 0.01f);
		}
	}
}
