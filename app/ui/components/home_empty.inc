static PSX3D_COLD void empty_top(void *ud)
{
	(void)ud;
	libui_draw_backdrop();
	C2D_DrawRectSolid(0, 0, 0.3f, UI_TOP_SCREEN_WIDTH, UI_SCREEN_HEIGHT, theme_alpha(theme()->surface, 70));
	home_title("No games yet", home_empty_help(false), 1);
	home_top_common();
 bb_update_draw_notice();
	if (fade > 0.001f)   // fixed: fade from/to black
		C2D_DrawRectSolid(0, 0, 0.95f, UI_TOP_SCREEN_WIDTH, UI_SCREEN_HEIGHT, theme_alpha(theme()->shadow, (u8)(255 * fminf(fade, 1))));
}

static PSX3D_COLD void empty_bottom(void *ud)
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
		C2D_DrawCircleSolid(22, y + 8, 0.3f, 9, accent(i, 255));
		char n[2] = { (char)('1' + i), 0 };
		ui_text_ex(NULL, 22, y + 1, UI_FONT_META, th->surface, C2D_AlignCenter, n);
		ui_text_ex(NULL, 40, y, 0.48f, white, 0, steps[i]);
	}
	ui_text_wrap(14, 128, UI_FONT_SMALL, white, 292, systems);
	ui_text_wrap(14, 152, UI_FONT_SMALL, muted, 292, types);
		if (empty_msg_frames > 0)
		ui_text_wrap(8, 170, UI_FONT_SMALL, th->message, 304, empty_msg);   // two lines at most (the path is cut to its end), all above the button bar
	ui_bar_draw(empty_bar, 3);
	if (fade > 0.001f)   // fixed: fade from/to black
		C2D_DrawRectSolid(0, 0, 0.95f, UI_BOTTOM_SCREEN_WIDTH, UI_SCREEN_HEIGHT, theme_alpha(theme()->shadow, (u8)(255 * fminf(fade, 1))));
}
