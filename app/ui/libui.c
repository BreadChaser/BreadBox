// Menu title fonts and backdrop; home and library share the selected look.
#include "libui.h"
#include "scene.h"

C2D_Font title_font;

// behind menus that draw no top screen of their own (Settings, the empty-library screen)
void libui_draw_backdrop(void)
{
	scene_draw(false);
}

C2D_Font libui_font(void)
{
	return title_font;
}
