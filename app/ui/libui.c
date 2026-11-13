// The title font and the menus' top-screen backdrop. The game library itself is the room's bookcase (shelf.c);
// the 2D library screen that lived here is gone (review m5, 2026-11-06).
#include "app_log.h"
#include "libui.h"
#include "scene.h"

static C2D_Font title_font;

void libui_init(void)
{
	title_font = C2D_FontLoad("romfs:/font/title.bcfnt");
	if (!title_font)
		log_printf("libui: title font missing, using system font\n");
}

void libui_exit(void)
{
	if (title_font)
		C2D_FontFree(title_font);
}

// behind menus that draw no top screen of their own (Settings, the empty-library screen)
void libui_draw_backdrop(void)
{
	scene_draw(false);
}

C2D_Font libui_font(void)
{
	return title_font;
}
