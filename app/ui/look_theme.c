#include <stdbool.h>
#include "theme.h"
#include "app_config.h"
#include "cold_code.h"

static const bb_look looks[BB_LOOK_COUNT] = {
 { "Room", "day", false, true },
 { "PSX3D", "psx3d", true, true },
 { "Simple", "simple", false, false },
};
static int room_style;
PSX3D_COLD const bb_look *theme_look_get(int look)
{
 return &looks[look>=0 && look<BB_LOOK_COUNT ? look : BB_LOOK_ROOM];
}
PSX3D_COLD int theme_look(void) { return theme()->look; }
PSX3D_COLD bool theme_is_room(void) { return theme_look()==BB_LOOK_ROOM; }
PSX3D_COLD void theme_select_look(int look)
{
 if(look<0 || look>=BB_LOOK_COUNT)look=BB_LOOK_ROOM;
 if(theme_is_room())room_style=theme_from_saved_id(theme()->id);
 g_cfg.theme=look==BB_LOOK_ROOM ? room_style : theme_from_saved_id(looks[look].saved_id);
 theme_menu_refresh();
}

PSX3D_COLD int theme_room_style(void)
{return theme_is_room()?g_cfg.theme:room_style;}
PSX3D_COLD void theme_select_room_style(int style)
{
 room_style=style>=0 && style<BB_ROOM_STYLE_COUNT?style:0;
 if(theme_is_room()){g_cfg.theme=room_style;theme_menu_refresh();}
}
