// Shared emu presentation; content/state are provided by its controller.
PSX3D_COLD_FN(alert_draw_top) static void alert_draw_top(void)
{
	float dx = ui_depth(-UI_METRIC_0_7F);
	C2D_DrawRectSolid(UI_METRIC_0, UI_METRIC_0, UI_METRIC_0_66F, UI_TOP_SCREEN_WIDTH, UI_METRIC_30, g_game_palette.alert_bg);   // below the text depth (0.7)
	C2D_DrawRectSolid(UI_METRIC_0, UI_METRIC_30, UI_METRIC_0_67F, UI_TOP_SCREEN_WIDTH, UI_METRIC_2, g_game_palette.alert_rule);
	ui_text_ex(NULL, UI_METRIC_200 + dx, UI_METRIC_8, UI_FONT_VALUE, g_game_palette.alert_text, C2D_AlignCenter,
		ui_fit(osd_q[UI_METRIC_0].text, UI_FONT_VALUE, UI_METRIC_390));
}

static void menu_game(void *unused)
{
	(void)unused;
	video_draw_game();
}
static void menu_draw_top(void *ud)
{
	menu_t *m = ud;
	float box[UI_METRIC_4];
	video_game_box(box);
	bb_pause_present_top(card_title(), box, menu_game, &m->slots);   // the shared pause top (bb_pause.h)
}

static void menu_draw_bottom(void *ud)
{
	menu_t *m = ud;
	if (have_disk && disk.get_num_images)
		snprintf(m->disc, sizeof(m->disc), "Disc %u of %u", menu_disc + UI_METRIC_1, disk.get_num_images());
	ui_button_t bar[UI_BAR_MAX];
	int nbar = menu_bar(m, bar);
	bb_pause_present_card(&m->fl, m->nitems, m->sel, menu_card_row, m, card_title(),
		svcGetSystemTick() < m->note_until ? m->note : NULL, bar, nbar);   // the shared pause card
}

static void game_draw_bottom(void *ud)
{
	(void)ud;
	C2D_DrawRectSolid(UI_METRIC_0, UI_METRIC_0, UI_METRIC_0_05F, UI_BOTTOM_SCREEN_WIDTH, UI_SCREEN_HEIGHT, g_game_palette.black);   // in game it's always black

	if (g_cfg.show_fps == UI_METRIC_2 || touch_buttons_active() || g_cfg.show_fps == UI_METRIC_1)
		touch_buttons_draw();
	statsview_draw();
	if (!has_audio)
		ui_text(UI_METRIC_4, UI_METRIC_186, UI_FONT_META, g_game_palette.warning,
			"No sound: run DSP1 once (it saves the 3DS sound firmware)");
}

static void game_draw_top(void *ud)
{
	(void)ud;
	// Notices stay outside the interactive bottom controls, in every FPS mode.
	if (osd_n && !osd_alert_up())ui_game_notice(osd_q[UI_METRIC_0].text,osd_q[UI_METRIC_0].alert);
	if (osd_alert_up())
		alert_draw_top();
	if (!speed_icon)
		return;
	float x = UI_TOP_SCREEN_WIDTH - UI_METRIC_46 + ui_depth(-UI_METRIC_0_6F), y = UI_METRIC_10, h = UI_METRIC_16, w = UI_METRIC_13;
	u8 a = UI_METRIC_255;
	u32 c = ((g_game_palette.speed_ink & UI_METRIC_0XFFFFFF) | ((u32)a << UI_METRIC_24)), sh = g_game_palette.speed_shadow;
	C2D_DrawRectSolid(x - UI_METRIC_6, y - UI_METRIC_4, UI_METRIC_0_8F, UI_METRIC_2 * w + UI_METRIC_12, h + UI_METRIC_8, g_game_palette.speed_bg);
	for (int k = UI_METRIC_0; k < UI_METRIC_2; k++) {
		float x0 = x + k * w;
		for (int pass = UI_METRIC_0; pass < UI_METRIC_2; pass++) {
			float o = pass ? UI_METRIC_0 : UI_METRIC_1;
			u32 col = pass ? c : sh;
			if (speed_icon > UI_METRIC_0)
				C2D_DrawTriangle(x0 + o, y + o, col, x0 + o, y + h + o, col, x0 + w + o, y + h / UI_METRIC_2 + o, col, UI_METRIC_0_81F + pass * UI_METRIC_0_01F);
			else
				C2D_DrawTriangle(x0 + w + o, y + o, col, x0 + w + o, y + h + o, col, x0 + o, y + h / UI_METRIC_2 + o, col, UI_METRIC_0_81F + pass * UI_METRIC_0_01F);
		}
	}
}
