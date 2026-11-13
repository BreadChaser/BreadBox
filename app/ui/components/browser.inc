// Shared browser presentation; content/state are provided by its controller.
static void draw_top(void *ud)
{
	(void)ud;
	const theme_t *th = theme();
	scene_draw(false);
	ui_text_ex(libui_font(), UI_METRIC_16, UI_METRIC_14, UI_METRIC_0_8F, th->heading, UI_METRIC_0, "" APP_NAME "");
	char version[UI_METRIC_64];
	// one folder for every system: what plays comes from the registered systems (breadbox_formats.c)
	static char systems[UI_METRIC_96];
	if (!systems[UI_METRIC_0])
		bb_formats_systems(systems, sizeof(systems));
	snprintf(version, sizeof(version), "Version " BB_VERSION " - Pick the folder that holds your games.");
	ui_text_ex(NULL, UI_METRIC_16, UI_METRIC_46, UI_FONT_META, th->text_muted, UI_METRIC_0, version);
	ui_text_ex(NULL, UI_METRIC_16, UI_METRIC_120, UI_FONT_META, th->text_muted, UI_METRIC_0,
		ui_fit(systems, UI_FONT_META, UI_METRIC_368));
	ui_text_ex(NULL, UI_METRIC_16, UI_METRIC_70, UI_METRIC_0_39F, th->heading_scope, UI_METRIC_0, "Games folder");
	ui_text_ex(NULL, UI_METRIC_16, UI_METRIC_84, UI_FONT_META, th->folder, UI_METRIC_0,
		g_cfg.games_dir[UI_METRIC_0] ? ui_fit(g_cfg.games_dir, UI_FONT_META, UI_METRIC_368) : "None chosen yet");
	if (slow_mode_notice(false))
		ui_text_ex(NULL, UI_METRIC_16, UI_METRIC_108, UI_FONT_META, th->banner_text, UI_METRIC_0,
			ui_fit(slow_mode_notice(true), UI_FONT_META, UI_METRIC_368));
	ui_help_panel(UI_METRIC_150, "This folder", truncated ? "Over 4096 entries; the list is cut off" : NULL, cwd);
	ui_notice(msg, msg_frames);
}

static void draw_bottom(void *ud)
{
	(void)ud;
	const theme_t *th = theme();
	u32 muted = th->text_muted;
	scene_draw(true);
	const char *name = strrchr(cwd, '/');
	LIST_HEADER("Folders", ui_fit(name && name[UI_METRIC_1] ? name + UI_METRIC_1 : cwd, UI_FONT_META, UI_METRIC_170));
ui_list_draw(&lst, nentries + UI_METRIC_1, sel, row_label, row_value, NULL, th->accent[sel & UI_METRIC_3], NULL);
	if (msg_frames > UI_METRIC_0)
		msg_frames--;
	if (!nentries)
		ui_text_ex(NULL, UI_METRIC_12, UI_METRIC_72, UI_FONT_SMALL, muted, UI_METRIC_0, "No games or folders here");
	ui_button_t bar[UI_METRIC_4];
	ui_bar_draw(bar, browser_bar(bar));
}

static const char *row_label(int i, void *ud)
{
	(void)ud;
	if (!i)
  return "Use for all systems";
	return ui_fit(entries[i - UI_METRIC_1].name, UI_FONT_BODY, entries[i - UI_METRIC_1].dir ? UI_METRIC_270 : UI_METRIC_250);
}
