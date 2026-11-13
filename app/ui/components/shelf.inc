// Shared shelf presentation: the room and the desk are artwork (shelf_scene.c);
// the touch tabs are the shared style-7 bar in the room theme's colours.
static void draw_top(void *unused)
{
 (void)unused;
 shelf_scene_top(&view);
}

static void draw_bottom(void *unused)
{
 (void)unused;
 ui_begin();
 shelf_scene_bottom(&view);
 ui_bar_draw(bar(),UI_METRIC_4);
}
