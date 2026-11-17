// Shared gba presentation; content/state are provided by its controller.
static void draw_bottom(void *unused)
{
    (void)unused;
    C2D_DrawRectSolid(0,0,UI_Z_BACKDROP,UI_BOTTOM_SCREEN_WIDTH,UI_SCREEN_HEIGHT,UI_GAME_CLEAR);
    bottom_paints++;
    if(touch_intro_pending || (touch_intro_visible && svcGetSystemTick()<notice_until)) {
     C2D_DrawRectSolid(UI_PLAY_NOTICE_X,UI_PLAY_NOTICE_Y,UI_Z_NOTICE,UI_PLAY_NOTICE_W,UI_PLAY_NOTICE_H,g_game_palette.panel);
     ui_text_wrap(UI_PLAY_NOTICE_TEXT_X,UI_PLAY_NOTICE_TEXT_Y,UI_FONT_VALUE,g_game_palette.text,UI_PLAY_NOTICE_TEXT_W,notice);
     if(touch_intro_pending)touch_intro_did_paint();
     return;
    }
    gba_touch_draw();
    statsview_draw();
}

// The game picture, and ▶▶ / ◀◀ while fast-forward or rewind is held
static void draw_game_top(void *unused)
{
    draw_top(unused);
    ui_speed_badge(speed_icon);
    // Ordinary messages belong above the game, outside the interactive controls.
    if(!touch_intro_pending && !touch_intro_visible && svcGetSystemTick()<notice_until)ui_game_notice(notice,false);
}

static void draw_gba_settings(void *unused)
{
    (void)unused;
    const theme_t *th=theme();
    C2D_DrawRectSolid(0,0,UI_Z_BACKDROP,UI_BOTTOM_SCREEN_WIDTH,UI_SCREEN_HEIGHT,th->surface);
    gba_settings_list_draw(&settings_list,setting_row,"This game · GBA");
    static const ui_button_t bar[] __asm__("bar.0")={{KEY_B,"Back",UI_ICON_BACK,false},{KEY_LEFT,"Change",UI_ICON_LEFT,false},{KEY_RIGHT,"Change",UI_ICON_RIGHT,false}};
    ui_bar_draw(bar,3);
}

static void draw_prepare(void *unused)
{
    (void)unused;
    ui_begin();char message[64];
    snprintf(message,sizeof(message),"Extracting ZIP: %u%%",prepare_percent);
    ui_progress_draw("Preparing GBA game",message,prepare_percent);
}

static void draw_settings_help(void *unused)
{
 (void)unused;
 draw_top(NULL);
 ui_help_panel(UI_HELP_TOP,gba_setting_label(setting_row,NULL),gba_setting_source(setting_row),"Left / Right: change · X: inherit · Y: reset this game");
}
