// Menu fonts load outside drawing and follow the selected look.
#include "app_log.h"
#include "libui.h"
#include "scene.h"
#include "theme.h"
#include "list_look.h"
#include "cold_code.h"

extern C2D_Font title_font;
static C2D_Font display_font __attribute__((section(".theme_game.bss")));
static bool font_ready __attribute__((section(".theme_game.bss")));
PSX3D_COLD void libui_look_changed(void)
{
 if(!font_ready)return;
 title_font=theme_look_get(theme_look())->display_font ? display_font : NULL;
}

PSX3D_COLD void libui_init(void)
{
 if(font_ready)return;
	if(!display_font)display_font = C2D_FontLoad("romfs:/font/title.bcfnt");
 title_font=display_font;
	if (!title_font)
		log_printf("libui: title font missing, using system font\n");
 list_look_prepare();
 if(!theme_is_room())shelf_scene_prepare();
 font_ready=true;libui_look_changed();
}

PSX3D_COLD void libui_release_menu(void)
{
 font_ready=false;list_look_release();title_font=display_font;
}

PSX3D_COLD void libui_exit(void)
{
 font_ready=false;list_look_release();
 if(display_font)C2D_FontFree(display_font);
 display_font=title_font=NULL;
}
