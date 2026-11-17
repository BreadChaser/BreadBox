// Shared presentation.

static void draw_message(void *ud)
{
	const char *msg = ud, *nl = strchr(msg, '\n');
	const theme_t *th = theme();
	char title[96];
	snprintf(title, sizeof(title), "%.*s", nl ? (int)(nl - msg) : (int)strlen(msg), msg);
	C2D_DrawRectSolid(0, 0, UI_Z_BACKDROP, UI_BOTTOM_SCREEN_WIDTH, UI_SCREEN_HEIGHT, th->surface);
	/* A first line the header would cut with "..." is read in full in the wrapped body instead. */
	bool cut = strcmp(ui_fit_ex(libui_font(), title, UI_HEADER_TITLE_SIZE, UI_TEXT_FLOOR,
		UI_HEADER_RIGHT - UI_HEADER_LEFT, NULL), title);
	ui_menu_header(cut ? "" : title, NULL);
	const char *body = cut ? msg : nl ? nl + 1 : NULL;
	if (body)
		ui_text_wrap(UI_WRAP_X, 44, 0.48f, th->help_text, UI_WRAP_W, body);
	ui_bar_draw(error_bar, 1);
}
