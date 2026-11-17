#include <stdbool.h>
#include "theme.h"
#include "app_config.h"
#include "cold_code.h"

static const bb_look looks[BB_LOOK_COUNT] = {
 { "Room", "day", false, true },
 { "PSX3D", "psx3d", true, true },
 { "Simple", "simple", false, false },
};
PSX3D_COLD const bb_look *theme_look_get(int look)
{
 return &looks[look>=0 && look<BB_LOOK_COUNT ? look : BB_LOOK_ROOM];
}
PSX3D_COLD int theme_look(void) { return theme()->look; }
PSX3D_COLD bool theme_is_room(void) { return theme_look()==BB_LOOK_ROOM; }
PSX3D_COLD void theme_select(int index)
{
 g_cfg.theme=index>=0 && index<theme_count() ? index : theme_default();
 theme_menu_refresh();
}
PSX3D_COLD void theme_select_look(int look)
{
 theme_select(theme_from_saved_id(theme_look_get(look)->saved_id));
}
