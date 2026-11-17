PSX3D_COLD void home_title(const char *title, const char *line, float ease)
{
	u8 a = (u8)(255 * ease);
	float size = 0.8f, w = ui_text_width(libui_font(), size, title);
	if (w > 380)
		size *= 380 / w;
	float ty = 172 + (1 - ease) * 10, tx = 200 + ui_depth(-0.8f);
	const theme_t *th = theme();
	ui_text_ex(libui_font(), tx + 1, ty + 1, size, theme_alpha(THEME_TITLE_SHADOW(theme()), a * 5 / 8), C2D_AlignCenter, title);
	ui_text_ex(libui_font(), tx, ty, size, theme_alpha(th->game_title, a), C2D_AlignCenter, title);
	ui_text_ex(NULL, tx, ty + 34, UI_FONT_VALUE, theme_alpha(th->game_sub, a), C2D_AlignCenter, line);
}

PSX3D_COLD void home_top_common(void)
{
	// small PSX3D wordmark (the slow-mode banner takes its place)
	if (!slow_mode_notice(false)) {
		float w = ui_text_width(libui_font(), 0.42f, APP_WORDMARK_LEFT);
		ui_text_ex(libui_font(), 10, 6, UI_FONT_META, theme()->wordmark, 0, APP_WORDMARK_LEFT);
		ui_text_ex(libui_font(), 10 + w + 0.42f * 6, 6, UI_FONT_META, theme()->wordmark_accent, 0, APP_WORDMARK_RIGHT);
	}
	if (!bb_system_total()) {   // first start: one friendly line
		float y = 0;
		C2D_DrawRectSolid(0, y, 0.6f, UI_TOP_SCREEN_WIDTH, 2, accent(1, 255));
	}
	home_slow_banner();
	ui_text_ex(NULL, 396, 226, 0.35f, theme()->version_text, C2D_AlignRight, "v" PSX3D_VERSION);
}
