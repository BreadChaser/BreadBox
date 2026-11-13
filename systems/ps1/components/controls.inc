static void ps1_mark(int id,float x,float y,float pixels,float spacing)
{
 if(!ui_input_symbol(id,x,y,pixels*UI_METRIC_1_6F,g_game_palette.mark))
  ui_button_text(x,y,pixels,spacing,g_game_palette.mark,id);
}
/* Compatibility presentation entry point; shared panel owns all drawing. */
void touch_buttons_draw(void)
{
 ps1_panel_prepare();
 ui_play_draw(&panel);
}
