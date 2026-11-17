// Shared shelf presentation: the room and the desk are artwork (shelf_scene.c);
// the touch tabs are the shared style-7 bar in the room theme's colours.
static void draw_top(void *unused)
{
 (void)unused;
 if(theme_is_room())shelf_scene_top(&view);else list_look_library_top(&view);
 home_slow_banner();
 bb_update_draw_notice();
}

static void draw_bottom(void *unused)
{
 (void)unused;
 ui_begin();
 if(theme_is_room()){shelf_scene_bottom(&view);ui_bar_draw(bar(),BB_LIBRARY_ACTIONS);}
 else list_look_library_bottom(&view,bar(),BB_LIBRARY_ACTIONS);
 ui_trace_library(&view,bar(),BB_LIBRARY_ACTIONS);
}
