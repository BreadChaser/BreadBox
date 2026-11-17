// Shared browser presentation; content/state are provided by its controller.
static void draw_top(void *ud)
{
	(void)ud;
	const theme_t *th = theme();
	scene_draw(false);
	ui_text_ex(libui_font(), UI_BROWSER_LEFT, 14, 0.8f, th->heading, 0, "" APP_NAME "");
	char version[64];
	// one folder for every system: what plays comes from the registered systems (breadbox_formats.c)
	static char systems[96];
	if (!systems[0])
		bb_formats_systems(systems, sizeof(systems));
	snprintf(version, sizeof(version), "Version " BB_VERSION " - Pick the folder that holds your games.");
	ui_text_ex(NULL, UI_BROWSER_LEFT, 46, UI_FONT_META, th->text_muted, 0, version);
	ui_text_ex(NULL, UI_BROWSER_LEFT, 120, UI_FONT_META, th->text_muted, 0,
		ui_fit(systems, UI_FONT_META, 368));
	ui_text_ex(NULL, UI_BROWSER_LEFT, 70, 0.39f, th->heading_scope, 0, "Games folder");
	ui_text_ex(NULL, UI_BROWSER_LEFT, 84, UI_FONT_META, th->folder, 0,
		g_cfg.games_dir[0] ? ui_fit(g_cfg.games_dir, UI_FONT_META, 368) : "None chosen yet");
	if (slow_mode_notice(false))
		ui_text_ex(NULL, UI_BROWSER_LEFT, 108, UI_FONT_META, th->banner_text, 0,
			ui_fit(slow_mode_notice(true), UI_FONT_META, 368));
	ui_help_panel(150, "This folder", truncated ? "Over 4096 entries; the list is cut off" : NULL, cwd);
	ui_notice(msg, msg_frames);
}

static void draw_bottom(void *ud)
{
	(void)ud;
	const theme_t *th = theme();
	u32 muted = th->text_muted;ui_trace_browser(&lst,nentries,sel,row_label,row_value);
	scene_draw(true);
	const char *name = strrchr(cwd, '/');
	LIST_HEADER("Folders", ui_fit(name && name[1] ? name + 1 : cwd, UI_FONT_META, 170));
ui_list_draw(&lst, nentries + 1, sel, row_label, row_value, NULL, th->accent[sel & 3], NULL);
	if (msg_frames > 0)
		msg_frames--;
	if (!nentries)
		ui_text_ex(NULL, 12, 72, UI_FONT_SMALL, muted, 0, "No games or folders here");
	ui_button_t bar[4];
	ui_bar_draw(bar, browser_bar(bar));
}

static const char *row_label(int i, void *ud)
{
	(void)ud;
	if (!i)
  return "Use for all systems";
	return ui_fit(entries[i - 1].name, UI_FONT_BODY, entries[i - 1].dir ? 270 : 250);
}
