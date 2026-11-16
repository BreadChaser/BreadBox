// Shared settings_ui presentation; content/state are provided by its controller.
static void draw_settings(void *ud)
{
	settings_ui_t *s = ud;
	int sel = s->selected[s->page];
#ifdef APP_BREADBOX
    if (s->page == CAT_THEME) { theme_picker_draw(&s->list,sel,theme_row_of(s->theme_before)); return; }
#else
    if (s->page == CAT_THEME) { theme_picker_draw(&s->list,sel,s->theme_before); return; }
#endif
	const theme_t *th = theme();
	u32 muted = th->text_muted;
	if (!s->game) scene_draw(true);
	else C2D_DrawRectSolid(0, 0, .1f, UI_BOTTOM_SCREEN_WIDTH, UI_SCREEN_HEIGHT, th->surface);
	LIST_HEADER(categories[s->page], s->game ? ui_fit(emu_game_title(), UI_FONT_META, 150) :
#ifdef APP_BREADBOX
		system_settings ? "PlayStation defaults" : "App"
#else
		"All games"
#endif
	);
	const char *header = advanced_header(s);
	if (header)
#ifndef APP_BREADBOX
		ui_text_wrap(12, 32, .39f, header == advanced_tested ? th->help_impact : muted, 296.0f, header);
#else
 ui_list_context(categories[s->page],header);
#endif
	int rows = list_rows(s);
	#ifdef APP_BREADBOX
ui_list_draw(&s->list, s->count, sel, row_label, s->page == CAT_BINDINGS ? input_control_value : row_value, s, th->accent[sel & 3], heading);
#else
flist_draw_sections(&s->list, s->count, sel, 0, list_top(s), UI_BOTTOM_SCREEN_WIDTH, row_height(s), rows, row_label,
			   s->page == CAT_BINDINGS ? input_control_value : row_value, s,
			   th->accent[sel & 3], heading);
#endif
	#ifndef APP_BREADBOX
	if (s->count > rows) {
		float h = rows * 23.0f;
		C2D_DrawRectSolid(317, list_top(s) + h * s->list.scroll / s->count, .3f, 2, h * rows / s->count, muted);
	}
#endif
	if (!s->count)
		ui_text_ex(NULL, 160, 90, .48f, muted, C2D_AlignCenter,
				   s->page == CAT_CHEATS ? "No cheats for this game yet" : "No options available");
	int item = s->count ? s->items[sel] : -1;
	const char *tip = NULL;
	if (item >= CATEGORY_ROW)
		tip = category_tips[item - CATEGORY_ROW];
	else if (item >= CONTROL_ROW)
		tip = NULL;   // the buttons below say it
	else if (item >= CORE_ROW)
		tip = options_impact(item - CORE_ROW);
	else if (item == F_SCREEN && s->game && screen_note())
		tip = screen_note();
	else if (item >= 0 && item < F_COUNT)
		tip = tips[item];
#ifdef APP_BREADBOX
 if(message_frames>0) ui_list_context(message,NULL);
#else
	if (message_frames > 0)
		ui_text_ex(NULL, 12, 179, .39f, th->message, 0, message);
	else if (tip && s->page != CAT_HOME)
		ui_text_ex(NULL, 12, 179, .39f, muted, 0, tip);
	// In a game every value belongs to that game; unchanged ones follow all games.
	if (s->game && item >= 0 && item < CONTROL_ROW && item != F_REWIND && (item >= CORE_ROW || override_key(item)))
		ui_text_ex(NULL, 12, 193, UI_FONT_SMALL, th->text_bright, 0,
				   overridden(s, item) ? "* Changed for this game" : preset_row(s, item) ? "P Tested setting for this game" : "Same as all games");
#endif
	ui_button_t bar[UI_BAR_MAX];
	ui_bar_draw(bar, settings_bar(s, bar));
}

static void draw_category(int pic, int item)
{
	const theme_t *th = theme();
	libui_draw_backdrop();
	C2D_DrawRectSolid(0, 0, .3f, UI_TOP_SCREEN_WIDTH, UI_SCREEN_HEIGHT, theme_alpha(th->surface, 70));
	icons_draw(ICON_SLOT_TOP, pic, 200, 88, 116, true, 1, .5f);
	const char *name = item == F_FOLDER ? labels[F_FOLDER] : categories[item - CATEGORY_ROW];
	const char *tip = item == F_FOLDER ? tips[F_FOLDER] : category_tips[item - CATEGORY_ROW];
#ifdef APP_BREADBOX
 ui_companion_title(name,tip?ui_fit(tip,UI_FONT_VALUE,384):NULL);
#else
	float size = .8f, w = ui_text_width(libui_font(), size, name);
	if (w > 380)
		size *= 380 / w;
	ui_text_ex(libui_font(), 201, 173, size, theme_alpha(THEME_TITLE_SHADOW(theme()), 160), C2D_AlignCenter, name);
	ui_text_ex(libui_font(), 200, 172, size, th->game_title, C2D_AlignCenter, name);
	if (tip)
		ui_text_ex(NULL, 200, 206, UI_FONT_VALUE, th->game_sub, C2D_AlignCenter, ui_fit(tip, UI_FONT_VALUE, 384));
#endif
}

static void draw_help(void *ud)
{
	settings_ui_t *s = ud;
	if (s->page == CAT_THEME) {
        scene_draw(false);
        return;
    }
	int item = s->count ? s->items[s->selected[s->page]] : -1;
	int pic = category_picture(s, item);
	if (pic >= 0) {
		draw_category(pic, item);
		return;
	}
	if (draw_behind)
		draw_behind(NULL);
	if (item >= CATEGORY_ROW || item < 0 || item >= CHEAT_ROW)
		return;
	bool preset = preset_row(s, item);
	const char *title = item >= CONTROL_ROW ? input_control_label(item - CONTROL_ROW, NULL)
		: item >= CORE_ROW ? options_desc(item - CORE_ROW) : labels[item];
	const char *impact = item >= CORE_ROW && item < CONTROL_ROW ? options_impact(item - CORE_ROW)
		: item == F_SCREEN && s->game ? screen_note() : NULL;
	const char *help = item >= CONTROL_ROW ? input_controls_help()
		: item >= CORE_ROW ? options_info(item - CORE_ROW) : helps[item];
	char evidence_help[768];
	if (preset) {
		snprintf(evidence_help, sizeof(evidence_help), "Preset evidence: %s. %s",
			game_preset_evidence(preset_key(item)), help ? help : "");
		help = evidence_help;
		impact = "P Tested setting for this game; you can change it";
	}
	#ifdef APP_BREADBOX
	if (!preset) impact = s->game ? (overridden(s,item) ? "Source: This game" : "Source: PlayStation defaults")
		: system_settings ? "Source: PlayStation defaults" : "Source: App";
#endif
	ui_help_panel(preset ? 100 : 150, title, impact, help);
}

static const char *row_label(int i, void *ud)
{
	settings_ui_t *s = ud;
	int item = s->items[i];
#ifdef APP_BREADBOX
	if (s->page == CAT_THEME) return theme_row_label(item);
#else
	if (s->page == CAT_THEME) return theme_get(item)->name;
#endif
	if (item == CATEGORY_ROW + CAT_RESET && s->game)
		return "Reset this game";
#ifdef APP_BREADBOX
	if (item == CATEGORY_ROW + CAT_RESET && !s->game) return system_settings?"Reset PlayStation defaults":"Reset the theme";
#endif
	if (item >= CATEGORY_ROW)
		return categories[item - CATEGORY_ROW];
	if (item >= CHEAT_ROW) {
		return ui_fit(cheats_desc(item - CHEAT_ROW), UI_FONT_BODY, 250);
	}
	if (item >= CONTROL_ROW) {
		static char control[80];
		int row = item - CONTROL_ROW;
		snprintf(control, sizeof(control), "%s%s", input_control_label(row, NULL),
				 config_control_overridden(row) ? " *" : preset_row(s, item) ? " P" : "");
		return control;
	}
	static char text[80];
	snprintf(text, sizeof(text), "%s%s", item >= CORE_ROW ? options_desc(item - CORE_ROW) : labels[item],
			 overridden(s, item) ? " *" : preset_row(s, item) ? " P" : "");
	return text;
}

static const char *row_value(int i, void *ud)
{
	settings_ui_t *s = ud;
	int item = s->items[i];
	if (s->page == CAT_THEME) return "";
	if (item >= CATEGORY_ROW)
		return item == CATEGORY_ROW + CAT_RESET ? "" : ">";
	if (item >= CHEAT_ROW)
		return cheats_on(item - CHEAT_ROW) ? "On" : cheats_forced(item - CHEAT_ROW) ? "Auto" : "Off";
	if (item >= CONTROL_ROW)
		return input_control_value(item - CONTROL_ROW, NULL);
	if (item >= CORE_ROW)
		return options_value(item - CORE_ROW);
	switch (item) {
	case F_HEADING:
	case F_RECOMMENDED:
		return "";
	case F_CONTROLLER:
		return config_controller() ? "Analog" : "Digital";
	case F_SCREEN:
		return (const char *const[]){"Original", "Stretch", "Widescreen"}[config_screen_mode()];
	case F_SMOOTH:
		return g_cfg.smooth ? "On" : "Off";
	case F_FPS:
		return (const char *const[]){"Off", "FPS", "Detailed"}[g_cfg.show_fps];
	case F_STEREO:
		return g_cfg.stereo_screen ? "Screen depth" : "Real 3D (experimental)";
	case F_THEME:
		return theme()->name;
#ifdef APP_BREADBOX
	case F_SKIN:
		return hand_skin_name[(unsigned)g_cfg.hand_skin < HAND_SKINS ? g_cfg.hand_skin : 1];
	case F_GLOVES:
		return hand_glove_name[(unsigned)g_cfg.hand_glove < HAND_GLOVES ? g_cfg.hand_glove : 0];
#endif
	case F_FOLDER: {
		const char *dir = strrchr(g_cfg.games_dir, '/');
		return g_cfg.games_dir[0] ? ui_fit(dir && dir[1] ? dir + 1 : g_cfg.games_dir, UI_FONT_VALUE, 120) : "None";
	}
	case F_SIDE:
		return g_cfg.touch_click_left ? "Left side" : "Right side";
	case F_REWIND:
		return config_rewind_on() ? (rewind_active() ? "On" : "Unavailable") : "Off";
	default:
		return ">";
	}
}

static float list_top(settings_ui_t *s) {
#ifdef APP_BREADBOX
 (void)s;return UI_LIST_TOP;
#else
 return advanced_header(s) ? 62 : 40;
#endif
}

static float row_height(settings_ui_t *s) {
#ifdef APP_BREADBOX
 (void)s;return UI_LIST_ROW;
#else
 return s->page == CAT_HOME ? 20 : 23;
#endif
}

static int list_rows(settings_ui_t *s) {
#ifdef APP_BREADBOX
 (void)s;return UI_LIST_ROWS;
#else
 return s->page == CAT_HOME ? 8 : advanced_header(s) ? 5 : VISIBLE_ROWS;
#endif
}
