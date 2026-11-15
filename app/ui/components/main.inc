// Shared presentation.

static void draw_message(void *ud)
{
	const char *msg = ud, *nl = strchr(msg, '\n');
	const theme_t *th = theme();
	char title[UI_METRIC_96];
	snprintf(title, sizeof(title), "%.*s", nl ? (int)(nl - msg) : (int)strlen(msg), msg);
	C2D_DrawRectSolid(UI_METRIC_0, UI_METRIC_0, UI_METRIC_0_1F, UI_BOTTOM_SCREEN_WIDTH, UI_SCREEN_HEIGHT, th->surface);
	/* A first line the header would cut with "..." is read in full in the wrapped body instead. */
	bool cut = strcmp(ui_fit_ex(libui_font(), title, UI_HEADER_TITLE_SIZE, UI_TEXT_FLOOR,
		UI_HEADER_RIGHT - UI_HEADER_LEFT, NULL), title);
	ui_menu_header(cut ? "" : title, NULL);
	const char *body = cut ? msg : nl ? nl + UI_METRIC_1 : NULL;
	if (body)
		ui_text_wrap(UI_METRIC_14, UI_METRIC_44, UI_METRIC_0_48F, th->help_text, UI_METRIC_292, body);
	ui_bar_draw(error_bar, UI_METRIC_1);
}
